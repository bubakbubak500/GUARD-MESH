// SPDX-License-Identifier: GPL-3.0-or-later
#include "WifiFormsScreen.h"

#include "../i18n.h"
#include "../platform/UiPlatform.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <new>

namespace ui { namespace screens {
using namespace theme;
using namespace widgets;

namespace {
void copyText(char *out, size_t capacity, const char *text) {
  if (capacity) std::snprintf(out, capacity, "%s", text ? text : "");
}
bool sameSsid(const char *a, const char *b) { return a && b && std::strcmp(a, b) == 0; }
lv_obj_t *captionButton(lv_obj_t *parent, const char *text, lv_coord_t width,
                        lv_coord_t height, lv_coord_t y, lv_event_cb_t callback, void *user) {
  auto *button = lv_btn_create(parent);
  lv_obj_set_size(button, width, height);
  lv_obj_set_pos(button, 0, y);
  styleButton(button);
  if (callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user);
  auto *label = lv_label_create(button);
  useChainedFont(label);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return button;
}
} // namespace

WifiFormsScreen::WifiFormsScreen(Host host) : _host(host) {}
WifiFormsScreen::~WifiFormsScreen() {
  _destroying = true;
  detach();
}

bool WifiFormsScreen::still(uint32_t generation) const {
  return !_destroying && _generation == generation;
}
void WifiFormsScreen::alert(const char *message, unsigned duration) {
  if (_host.alert) _host.alert(_host.context, message, duration);
}
void WifiFormsScreen::navDirty() {
  if (_host.navDirty) _host.navDirty(_host.context);
}
bool WifiFormsScreen::snapshot(PageSnapshot &out) {
  out = PageSnapshot{};
  if (!_host.readSnapshot || !_host.readSnapshot(_host.context, out)) return false;
  if (out.savedCount > SavedCapacity) out.savedCount = SavedCapacity;
  if (out.scannedCount > ScanCapacity) out.scannedCount = ScanCapacity;
  for (auto &network : out.saved) network.ssid[sizeof network.ssid - 1] = 0;
  for (auto &ssid : out.scanned) ssid[sizeof ssid - 1] = 0;
  out.connectedSsid[sizeof out.connectedSsid - 1] = 0;
  out.status[sizeof out.status - 1] = 0;
  return true;
}
void WifiFormsScreen::releaseRows() {
  if (_rows) {
    for (unsigned i = 0; i < RowCapacity; ++i) _rows[i].~RowContext();
    platform::release(_rows);
  }
  _rows = nullptr;
  _rowCount = 0;
}
void WifiFormsScreen::detachTree(lv_obj_t *object, bool deletingRoot) {
  if (!object) return;
  const uint32_t count = lv_obj_get_child_cnt(object);
  for (uint32_t i = 0; i < count; ++i) detachTree(lv_obj_get_child(object, i));
  if (deletingRoot) return; // Keep the current DELETE descriptor for later observers.
  for (auto callback : {bodyDeleted, sheetDeleted, radioChanged, connectClicked,
                        forgetClicked, joinClicked, autoJoinChanged, revealClicked,
                        closeClicked})
    while (lv_obj_remove_event_cb_with_user_data(object, callback, this)) {}
  if (_rows)
    for (unsigned i = 0; i < _rowCount; ++i)
      while (lv_obj_remove_event_cb_with_user_data(object, rowClicked, &_rows[i])) {}
}
void WifiFormsScreen::clearPage(bool deleting) {
  ++_generation;
  ++_listGeneration;
  if (!deleting) detachTree(_body.get());
  _body.set(nullptr); _list.set(nullptr); _radio.set(nullptr); _status.set(nullptr);
  releaseRows();
  _width = 0;
  _listY = 0;
  _radioEnabled = false;
}
void WifiFormsScreen::clearSheet(bool deleting) {
  ++_generation;
  if (!deleting) detachTree(_sheetRoot.get());
  _sheetRoot.set(nullptr); _ssidField.set(nullptr); _passwordField.set(nullptr);
  _autoJoin.set(nullptr); _eyeGlyph.set(nullptr);
  _sheetKind = SheetKind::None;
  _sheetSsid[0] = 0;
  _sheetTitle[0] = 0;
  _autoJoinEnabled = false;
}
void WifiFormsScreen::closeSheet() {
  lv_obj_t *root = _sheetRoot.get();
  clearSheet(false);
  if (!root) return;
  const uint32_t generation = _generation;
  if (_host.endPageIfOwned) _host.endPageIfOwned(_host.context);
  if (_generation == generation && _host.hideKeyboard) _host.hideKeyboard(_host.context);
  if (_host.closeRoot) _host.closeRoot(_host.context, &root);
  else lv_obj_del_async(root);
  if (_generation == generation && !_destroying) {
    if (_host.updateStatusBar) _host.updateStatusBar(_host.context);
    navDirty();
  }
}
void WifiFormsScreen::detach() {
  const uint32_t expected = _generation + 1;
  closeSheet();
  if (_generation != expected) return; // Host close callback opened a replacement.
  clearPage(false);
}
void WifiFormsScreen::bodyDeleted(lv_event_t *event) {
  auto *self = static_cast<WifiFormsScreen *>(lv_event_get_user_data(event));
  if (!self || self->_destroying) return;
  self->detachTree(lv_event_get_current_target(event), true);
  self->clearPage(true);
  self->closeSheet();
}
void WifiFormsScreen::sheetDeleted(lv_event_t *event) {
  auto *self = static_cast<WifiFormsScreen *>(lv_event_get_user_data(event));
  if (!self || self->_destroying) return;
  self->detachTree(lv_event_get_current_target(event), true);
  self->clearSheet(true);
  const uint32_t generation = self->_generation;
  if (self->_host.endPageIfOwned) self->_host.endPageIfOwned(self->_host.context);
  if (self->_generation != generation) return;
  if (self->_host.hideKeyboard) self->_host.hideKeyboard(self->_host.context);
  if (self->_generation != generation) return;
  if (self->_host.updateStatusBar) self->_host.updateStatusBar(self->_host.context);
  self->navDirty();
}

void WifiFormsScreen::build(lv_obj_t *body, lv_coord_t width) {
  if (_destroying) return;
  const bool reuse = body && body == _body.get();
  uint32_t expected = _generation + 1;
  closeSheet();
  if (_generation != expected) return;
  expected = _generation + 1;
  clearPage(false);
  if (!body || _generation != expected || !lv_obj_is_valid(body)) return;
  if (reuse) lv_obj_clean(body);
  if (!_body.set(body)) return;
  _width = width;
  lv_obj_add_event_cb(body, bodyDeleted, LV_EVENT_DELETE, this);
  const uint32_t generation = _generation;

  auto *radio = lv_switch_create(body);
  _radio.set(radio);
  lv_obj_align(radio, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_add_event_cb(radio, radioChanged, LV_EVENT_VALUE_CHANGED, this);
  auto *status = lv_label_create(body);
  _status.set(status);
  lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
  lv_obj_set_width(status, width - SC(54));
  lv_obj_set_height(status, SC(16));
  lv_obj_set_pos(status, 2, SC(7));
  lv_obj_set_style_text_color(status, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(status, &font12(), LV_PART_MAIN);
  lv_label_set_text(status, TR("Loading..."));

  auto *list = lv_obj_create(body);
  _list.set(list);
  lv_obj_remove_style_all(list);
  lv_obj_set_width(list, width);
  lv_obj_set_height(list, LV_SIZE_CONTENT);
  lv_obj_set_pos(list, 0, SC(30));
  lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  // The host keeps radio readiness, link-drop, and worker-queue policy. This
  // mirrors the existing page-open scan without letting a widget own the job.
  if (_host.requestScan) _host.requestScan(_host.context);
  if (still(generation)) rebuildList();
}

void WifiFormsScreen::refreshStatus() {
  if (!_body.get() || !_status.get() || !_radio.get()) return;
  const uint32_t generation = _generation;
  PageSnapshot page;
  if (!snapshot(page) || !still(generation) || !_status.get() || !_radio.get()) return;
  _radioEnabled = page.radioEnabled;
  if (page.radioEnabled) lv_obj_add_state(_radio.get(), LV_STATE_CHECKED);
  else lv_obj_clear_state(_radio.get(), LV_STATE_CHECKED);
  lv_label_set_text(_status.get(), page.status[0] ? page.status : TR("Loading..."));
}

void WifiFormsScreen::appendHeader(const char *text) {
  auto *list = _list.get();
  if (!list) return;
  auto *label = lv_label_create(list);
  lv_label_set_text(label, TR(text));
  lv_obj_set_style_text_font(label, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_pos(label, 2, _listY + SC(2));
  _listY += SC(15);
}
void WifiFormsScreen::appendRow(const char *text, const char *right, RowKind kind,
                                const char *identity, uint32_t generation) {
  auto *list = _list.get();
  if (!list) return;
  auto *row = lv_btn_create(list);
  lv_obj_set_size(row, _width, SC(30));
  lv_obj_set_pos(row, 0, _listY);
  styleButton(row);
  lv_obj_set_style_bg_color(row, lv_color_hex(colors().COLOR_CONTROL), LV_PART_MAIN);
  if (_rows && _rowCount < RowCapacity) {
    auto &context = _rows[_rowCount++];
    context.owner = this;
    context.generation = generation;
    context.kind = kind;
    copyText(context.ssid, sizeof context.ssid, identity);
    lv_obj_add_event_cb(row, rowClicked, LV_EVENT_CLICKED, &context);
  }
  auto *label = lv_label_create(row);
  lv_label_set_text(label, text);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(label, _width - SC(44));
  lv_obj_set_style_text_color(label, lv_color_hex(kind == RowKind::Hidden ? colors().COLOR_ACCENT :
                                                 colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(label, &font14(), LV_PART_MAIN);
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, 0);
  if (right && *right) {
    auto *suffix = lv_label_create(row);
    lv_label_set_text(suffix, right);
    lv_obj_set_style_text_color(suffix, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(suffix, &font14(), LV_PART_MAIN);
    lv_obj_align(suffix, LV_ALIGN_RIGHT_MID, -8, 0);
  }
  _listY += SC(33);
}

void WifiFormsScreen::rebuildList() {
  if (!_body.get() || !_list.get()) return;
  const uint32_t generation = _generation;
  PageSnapshot page;
  if (!snapshot(page) || !still(generation) || !_list.get()) return;
  if (_status.get()) lv_label_set_text(_status.get(), page.status[0] ? page.status : TR("Loading..."));
  _radioEnabled = page.radioEnabled;
  if (_radio.get()) {
    if (page.radioEnabled) lv_obj_add_state(_radio.get(), LV_STATE_CHECKED);
    else lv_obj_clear_state(_radio.get(), LV_STATE_CHECKED);
  }
  lv_indev_reset(nullptr, nullptr); // Drop a scroll target before deleting its row.
  if (!still(generation) || !_list.get()) return;
  lv_obj_clean(_list.get());
  if (!still(generation) || !_list.get()) return;
  releaseRows();
  _rows = static_cast<RowContext *>(platform::allocate(sizeof(RowContext) * RowCapacity, true));
  if (_rows)
    for (unsigned i = 0; i < RowCapacity; ++i) new (&_rows[i]) RowContext{};
  const uint32_t rowGeneration = ++_listGeneration;
  _listY = 0;

  unsigned order[SavedCapacity] = {};
  for (unsigned i = 0; i < page.savedCount; ++i) order[i] = i;
  for (unsigned i = 0; i < page.savedCount; ++i)
    for (unsigned j = i + 1; j < page.savedCount; ++j)
      if (page.saved[order[j]].rank > page.saved[order[i]].rank) {
        const unsigned old = order[i]; order[i] = order[j]; order[j] = old;
      }
  if (page.savedCount) {
    appendHeader("Saved networks");
    for (unsigned i = 0; i < page.savedCount; ++i) {
      const auto &network = page.saved[order[i]];
      const bool connected = page.connected && sameSsid(page.connectedSsid, network.ssid);
      appendRow(network.ssid, connected ? LV_SYMBOL_OK "  " LV_SYMBOL_RIGHT : LV_SYMBOL_RIGHT,
                RowKind::Saved, network.ssid, rowGeneration);
    }
  }
  appendHeader(page.savedCount ? "Other networks" : "Networks");
  unsigned other = 0;
  for (unsigned i = 0; i < page.scannedCount; ++i) {
    const char *ssid = page.scanned[i];
    if (!*ssid) continue;
    bool saved = false;
    for (unsigned j = 0; j < page.savedCount; ++j)
      if (sameSsid(ssid, page.saved[j].ssid)) { saved = true; break; }
    if (saved) continue;
    appendRow(ssid, LV_SYMBOL_RIGHT, RowKind::Scanned, ssid, rowGeneration);
    ++other;
  }
  if (!other) {
    auto *label = lv_label_create(_list.get());
    lv_label_set_text(label, page.scanning ? TR("Scanning\xe2\x80\xa6") : TR("No other networks found"));
    lv_obj_set_style_text_font(label, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_pos(label, 6, _listY + SC(2));
    _listY += SC(22);
  }
  _listY += SC(6);
  char rescan[48];
  std::snprintf(rescan, sizeof rescan, LV_SYMBOL_REFRESH "  %s",
                page.scanning ? TR("Scanning\xe2\x80\xa6") : TR("Scan again"));
  appendRow(rescan, nullptr, RowKind::Rescan, nullptr, rowGeneration);
  _listY += SC(3);
  char hidden[64];
  std::snprintf(hidden, sizeof hidden, LV_SYMBOL_PLUS "  %s", TR("Other (hidden) network\xe2\x80\xa6"));
  appendRow(hidden, nullptr, RowKind::Hidden, nullptr, rowGeneration);
  lv_obj_set_height(_list.get(), _listY + SC(2));
  navDirty();
}

void WifiFormsScreen::rowClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *context = static_cast<RowContext *>(lv_event_get_user_data(event));
  if (!context || !context->owner) return;
  auto *self = context->owner;
  if (self->_destroying || !self->_list.get() ||
      context->generation != self->_listGeneration) return;
  // Copy the row identity before a callback can rebuild the list and free its
  // context buffer. A fresh scan may change row indexes but never this SSID.
  const RowKind kind = context->kind;
  char ssid[33]; copyText(ssid, sizeof ssid, context->ssid);
  if (kind == RowKind::Saved) self->openDetails(ssid);
  else if (kind == RowKind::Scanned) self->openJoin(ssid, false);
  else if (kind == RowKind::Hidden) self->openJoin(nullptr, true);
  else if (kind == RowKind::Rescan) {
    const uint32_t generation = self->_generation;
    PageSnapshot page;
    if (!self->snapshot(page) || !self->still(generation) || page.scanning) return;
    if (self->_host.requestScan && self->_host.requestScan(self->_host.context) && self->still(generation))
      self->rebuildList();
  }
}
void WifiFormsScreen::radioChanged(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  auto *self = static_cast<WifiFormsScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying) self->setRadio(
      lv_obj_has_state(lv_event_get_current_target(event), LV_STATE_CHECKED));
}
void WifiFormsScreen::setRadio(bool enabled) {
  if (!_radio.get()) return;
  const bool prior = _radioEnabled;
  const uint32_t generation = _generation;
  const bool accepted = _host.setRadioEnabled && _host.setRadioEnabled(_host.context, enabled);
  if (!still(generation) || !_radio.get()) return;
  if (!accepted) {
    if (prior) lv_obj_add_state(_radio.get(), LV_STATE_CHECKED);
    else lv_obj_clear_state(_radio.get(), LV_STATE_CHECKED);
    return;
  }
  _radioEnabled = enabled;
  refreshStatus();
}

lv_obj_t *WifiFormsScreen::openSheet(const char *title, SheetKind kind) {
  const uint32_t expected = _generation + 1;
  closeSheet();
  if (!still(expected)) return nullptr;
  const uint32_t generation = _generation;
  const int status = _host.statusHeight ? _host.statusHeight(_host.context) : 0;
  if (!still(generation)) return nullptr;
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr), sh = lv_disp_get_ver_res(nullptr);
  auto *root = lv_obj_create(lv_scr_act());
  if (!_sheetRoot.set(root)) { lv_obj_del(root); return nullptr; }
  lv_obj_add_event_cb(root, sheetDeleted, LV_EVENT_DELETE, this);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, sw, sh - status);
  lv_obj_set_pos(root, 0, status);
  lv_obj_set_style_bg_color(root, lv_color_hex(colors().COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  copyText(_sheetTitle, sizeof _sheetTitle, title);
  _sheetKind = kind;
  if (_host.beginPage) _host.beginPage(_host.context, _sheetTitle);
  if (!still(generation) || _sheetRoot.get() != root) return nullptr;
  if (_host.updateStatusBar) _host.updateStatusBar(_host.context);
  if (!still(generation) || _sheetRoot.get() != root) return nullptr;

  auto *body = lv_obj_create(root);
  lv_obj_remove_style_all(body);
  lv_obj_set_size(body, sw, sh - status);
  lv_obj_set_pos(body, 0, 0);
  lv_obj_set_style_pad_all(body, 12, LV_PART_MAIN);
  lv_obj_set_style_pad_top(body, status + 8, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(body, SC(64), LV_PART_MAIN);
  return body;
}
void WifiFormsScreen::openJoin(const char *ssid, bool hidden) {
  if (_destroying || !_body.get()) return;
  char target[33]; copyText(target, sizeof target, ssid);
  const char *title = hidden ? TR("Hidden network") : (target[0] ? target : TR("Join network"));
  auto *body = openSheet(title, hidden ? SheetKind::Hidden : SheetKind::Join);
  if (!body) return;
  const uint32_t generation = _generation;
  copyText(_sheetSsid, sizeof _sheetSsid, target);
  const lv_coord_t width = lv_disp_get_hor_res(nullptr) - 24;
  lv_coord_t y = 0;
  if (hidden) {
    auto *label = lv_label_create(body);
    lv_label_set_text(label, TR("Network name (SSID)"));
    lv_obj_set_style_text_font(label, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_pos(label, 0, y); y += SC(16);
    auto *field = lv_textarea_create(body); _ssidField.set(field);
    lv_obj_set_size(field, width, SC(32)); lv_obj_set_pos(field, 0, y);
    styleCard(field); lv_textarea_set_one_line(field, true);
    taSetPlaceholder(field, TR("Network name"));
    lv_textarea_set_max_length(field, 31);
    if (_host.attachTextArea) _host.attachTextArea(_host.context, field);
    if (!still(generation) || _sheetRoot.get() == nullptr) return;
    y += SC(40);
  }
  auto *label = lv_label_create(body);
  lv_label_set_text(label, TR("Password (empty = open)"));
  lv_obj_set_style_text_font(label, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_pos(label, 0, y);
  auto *eye = lv_btn_create(body);
  lv_obj_set_size(eye, SC(34), SC(22));
  lv_obj_set_pos(eye, width - SC(34), y - SC(4));
  lv_obj_set_style_bg_opa(eye, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(eye, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(eye, 0, LV_PART_MAIN);
  auto *glyph = lv_label_create(eye); _eyeGlyph.set(glyph);
  lv_label_set_text(glyph, LV_SYMBOL_EYE_OPEN);
  lv_obj_set_style_text_font(glyph, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(glyph, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_center(glyph);
  lv_obj_add_event_cb(eye, revealClicked, LV_EVENT_CLICKED, this);
  y += SC(16);
  auto *password = lv_textarea_create(body); _passwordField.set(password);
  lv_obj_set_size(password, width, SC(32)); lv_obj_set_pos(password, 0, y);
  styleCard(password); lv_textarea_set_one_line(password, true);
  lv_textarea_set_password_mode(password, true);
  taSetPlaceholder(password, "PSK");
  lv_textarea_set_max_length(password, 63);
  if (_host.attachTextArea) _host.attachTextArea(_host.context, password);
  if (!still(generation) || !_passwordField.get()) return;
  if (_host.attachSymbolButton) _host.attachSymbolButton(_host.context, password);
  if (!still(generation) || !_passwordField.get()) return;
  if (target[0] && _host.loadPassword) {
    char saved[65] = {};
    const bool found = _host.loadPassword(_host.context, target, saved, sizeof saved);
    if (still(generation) && _passwordField.get() && found)
      lv_textarea_set_text(_passwordField.get(), saved);
    std::memset(saved, 0, sizeof saved);
  }
  if (!still(generation) || !_passwordField.get()) return;
  y += SC(44);
  auto *joinButton = captionButton(body, TR("Join"), width, SC(38), y,
                                   joinClicked, this);
  lv_obj_set_style_bg_color(joinButton, lv_color_hex(colors().COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_set_style_text_color(joinButton, lv_color_hex(colors().COLOR_ON_STATUS_OK), LV_PART_MAIN);
  lv_obj_add_event_cb(password, joinClicked, LV_EVENT_READY, this);
  y += SC(46);
  captionButton(body, TR("Cancel"), width, SC(34), y, closeClicked, this);
  lv_obj_move_foreground(_sheetRoot.get());
  navDirty();
}
void WifiFormsScreen::openDetails(const char *ssid) {
  if (_destroying || !_body.get() || !ssid || !*ssid) return;
  const uint32_t start = _generation;
  PageSnapshot page;
  if (!snapshot(page) || !still(start)) return;
  const SavedNetwork *target = nullptr;
  for (unsigned i = 0; i < page.savedCount; ++i)
    if (sameSsid(page.saved[i].ssid, ssid)) { target = &page.saved[i]; break; }
  if (!target) return; // A replaced saved slot must never open another network.
  char identity[33]; copyText(identity, sizeof identity, target->ssid);
  const bool autoJoin = target->autoJoin;
  const bool connected = page.connected && sameSsid(page.connectedSsid, identity);
  auto *body = openSheet(identity, SheetKind::Details);
  if (!body) return;
  copyText(_sheetSsid, sizeof _sheetSsid, identity);
  const lv_coord_t width = lv_disp_get_hor_res(nullptr) - 24;
  lv_coord_t y = 0;
  auto *status = lv_label_create(body);
  lv_label_set_text(status, connected ? TR("Connected") : TR("Saved network"));
  lv_obj_set_style_text_font(status, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(status,
      lv_color_hex(connected ? colors().COLOR_STATUS_OK_TEXT : colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_pos(status, 0, y); y += SC(24);
  auto *label = lv_label_create(body);
  lv_label_set_text(label, TR("Auto-Join"));
  lv_obj_set_style_text_font(label, &font14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_pos(label, 0, y + SC(4));
  auto *toggle = lv_switch_create(body); _autoJoin.set(toggle);
  lv_obj_set_pos(toggle, width - SC(52), y);
  _autoJoinEnabled = autoJoin;
  if (autoJoin) lv_obj_add_state(toggle, LV_STATE_CHECKED);
  lv_obj_add_event_cb(toggle, autoJoinChanged, LV_EVENT_VALUE_CHANGED, this);
  y += SC(44);
  if (!connected) {
    auto *connect = captionButton(body, TR("Connect"), width, SC(38), y,
                                  connectClicked, this);
    lv_obj_set_style_bg_color(connect, lv_color_hex(colors().COLOR_STATUS_OK), LV_PART_MAIN);
    lv_obj_set_style_text_color(connect, lv_color_hex(colors().COLOR_ON_STATUS_OK), LV_PART_MAIN);
    y += SC(46);
  }
  auto *forget = captionButton(body, TR("Forget network"), width, SC(38), y,
                               forgetClicked, this);
  lv_obj_set_style_bg_color(forget,
      lv_color_hex(themeRole(0xC44B55, colors().COLOR_STATUS_DANGER)), LV_PART_MAIN);
  lv_obj_set_style_bg_color(forget,
      lv_color_hex(themeRole(0xA13F47, colors().COLOR_STATUS_DANGER_PRESSED)),
      LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(forget, lv_color_hex(colors().COLOR_ON_STATUS_DANGER), LV_PART_MAIN);
  lv_obj_move_foreground(_sheetRoot.get());
  navDirty();
}

void WifiFormsScreen::joinClicked(lv_event_t *event) {
  const auto code = lv_event_get_code(event);
  if (code != LV_EVENT_CLICKED && code != LV_EVENT_READY) return;
  auto *self = static_cast<WifiFormsScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying) self->join();
}
void WifiFormsScreen::join() {
  if (!sheetOpen() || (_sheetKind != SheetKind::Join && _sheetKind != SheetKind::Hidden)) return;
  const uint32_t generation = _generation;
  if (_host.syncKeyboard) _host.syncKeyboard(_host.context);
  if (!still(generation) || !sheetOpen()) return;
  char ssid[33] = {}, password[65] = {};
  if (_ssidField.get()) copyText(ssid, sizeof ssid, lv_textarea_get_text(_ssidField.get()));
  else copyText(ssid, sizeof ssid, _sheetSsid);
  if (_passwordField.get())
    copyText(password, sizeof password, lv_textarea_get_text(_passwordField.get()));
  if (!ssid[0]) { alert(TR("Enter a network name"), 1200); return; }
  const bool saved = _host.saveAndConnect &&
      _host.saveAndConnect(_host.context, ssid, password, true);
  std::memset(password, 0, sizeof password);
  if (!still(generation) || !sheetOpen()) return;
  if (!saved) { alert(TR("Save failed"), 1200); return; }
  const uint32_t expected = _generation + 1;
  closeSheet();
  if (!still(expected)) return;
  alert(TR("Connecting\xe2\x80\xa6"), 1400);
  if (still(expected)) rebuildList();
}
void WifiFormsScreen::connectClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *self = static_cast<WifiFormsScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying) self->connectSaved();
}
void WifiFormsScreen::connectSaved() {
  if (!sheetOpen() || _sheetKind != SheetKind::Details || !_sheetSsid[0]) return;
  char ssid[33]; copyText(ssid, sizeof ssid, _sheetSsid);
  const uint32_t generation = _generation;
  const bool connected = _host.connectSaved && _host.connectSaved(_host.context, ssid);
  if (!still(generation) || !sheetOpen()) return;
  if (!connected) { alert(TR("Save failed"), 1200); return; }
  const uint32_t expected = _generation + 1;
  closeSheet();
  if (!still(expected)) return;
  alert(TR("Connecting\xe2\x80\xa6"), 1400);
  if (still(expected)) rebuildList();
}
void WifiFormsScreen::forgetClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *self = static_cast<WifiFormsScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying) self->forgetSaved();
}
void WifiFormsScreen::forgetSaved() {
  if (!sheetOpen() || _sheetKind != SheetKind::Details || !_sheetSsid[0]) return;
  char ssid[33]; copyText(ssid, sizeof ssid, _sheetSsid);
  const uint32_t generation = _generation;
  const bool forgotten = _host.forgetSaved && _host.forgetSaved(_host.context, ssid);
  if (!still(generation) || !sheetOpen()) return;
  if (!forgotten) { alert(TR("Save failed"), 1200); return; }
  const uint32_t expected = _generation + 1;
  closeSheet();
  if (still(expected)) rebuildList();
}
void WifiFormsScreen::autoJoinChanged(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  auto *self = static_cast<WifiFormsScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying)
    self->setAutoJoin(lv_obj_has_state(lv_event_get_current_target(event), LV_STATE_CHECKED));
}
void WifiFormsScreen::setAutoJoin(bool enabled) {
  if (!sheetOpen() || _sheetKind != SheetKind::Details || !_autoJoin.get()) return;
  char ssid[33]; copyText(ssid, sizeof ssid, _sheetSsid);
  const uint32_t generation = _generation;
  const bool changed = _host.setAutoJoin && _host.setAutoJoin(_host.context, ssid, enabled);
  if (!still(generation) || !_autoJoin.get()) return;
  if (!changed) {
    if (_autoJoinEnabled) lv_obj_add_state(_autoJoin.get(), LV_STATE_CHECKED);
    else lv_obj_clear_state(_autoJoin.get(), LV_STATE_CHECKED);
    alert(TR("Save failed"), 1200);
  } else _autoJoinEnabled = enabled;
}
void WifiFormsScreen::revealClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *self = static_cast<WifiFormsScreen *>(lv_event_get_user_data(event));
  if (!self || self->_destroying || !self->_passwordField.get()) return;
  const bool hidden = lv_textarea_get_password_mode(self->_passwordField.get());
  lv_textarea_set_password_mode(self->_passwordField.get(), !hidden);
  if (self->_eyeGlyph.get())
    lv_label_set_text(self->_eyeGlyph.get(), hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
  if (self->_host.setPasswordMirror)
    self->_host.setPasswordMirror(self->_host.context, !hidden);
}
void WifiFormsScreen::closeClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *self = static_cast<WifiFormsScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying) self->closeSheet();
}

} } // namespace ui::screens

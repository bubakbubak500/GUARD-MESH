// SPDX-License-Identifier: GPL-3.0-or-later
#include "AdminSessionScreen.h"

#include "../device_caps.h"
#include "../i18n.h"
#include "../platform/UiPlatform.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"
#include <cstdio>
#include <cstring>

namespace ui { namespace screens {
using namespace theme;
using namespace widgets;

namespace {
struct Command { const char *label; const char *text; };
const Command commands[] = {
  {"[ INFO ]", nullptr}, {"ver - firmware version", "ver"},
  {"board - hardware id", "board"}, {"clock - show clock", "clock"},
  {"[ RADIO / MESH ]", nullptr}, {"advert - flood self advert", "advert"},
  {"advert.zerohop - 0-hop advert", "advert.zerohop"},
  {"neighbors - list neighbours", "neighbors"},
  {"discover.neighbors - active scan", "discover.neighbors"},
  {"neighbor.remove <hex>", "neighbor.remove "},
  {"tempradio <freq> <bw> <sf> <cr> <m>", "tempradio "},
  {"clear stats", "clear stats"},
  {"[ RADIO PREFS ]", nullptr}, {"set name <new>", "set name "},
  {"set radio <freq> <bw> <sf> <cr>", "set radio "},
  {"set repeat on/off", "set repeat "},
  {"set advert.interval <minutes>", "set advert.interval "},
  {"set flood.advert.interval <hours>", "set flood.advert.interval "},
  {"set dutycycle <1-100>", "set dutycycle "},
  {"set af <factor>", "set af "},
  {"[ ADMIN ]", nullptr}, {"get acl - list clients", "get acl"},
  {"setperm <pubkey> <perms>", "setperm "},
  {"password <new>", "password "},
  {"set guest.password <new>", "set guest.password "},
  {"clock sync", "clock sync"}, {"time <epoch_secs>", "time "},
  {"start ota", "start ota"}, {"reboot", "reboot"},
  {"poweroff", "poweroff"},
};
constexpr unsigned commandCount = sizeof commands / sizeof commands[0];
static_assert(commandCount <= 32, "Admin picker row contexts");
const char help[] =
  "[logged in]\nInfo:\n  ver       firmware version\n  board     hardware id\n"
  "  clock     show clock\nRadio / mesh:\n  advert            flood advert\n"
  "  advert.zerohop    0-hop advert\n  neighbors         list neighbours\n"
  "  discover.neighbors  active scan\n  neighbor.remove <hex>\n"
  "  tempradio f bw sf cr mins\n  clear stats\nSettings:\n"
  "  get <key> / set <key> <val>\n  sensor list / get / set\nAdmin:\n"
  "  get acl       list clients\n  setperm <pubkey> <perms>\n"
  "  password <new>\n  reboot / poweroff\n  clock sync / time <epoch>\n  start ota";

void safeCopy(char *dst, size_t cap, const char *src) {
  if (!cap) return;
  std::snprintf(dst, cap, "%s", src ? src : "");
}
bool same(const uint8_t a[32], const uint8_t b[32]) { return std::memcmp(a, b, 32) == 0; }
void waitRelease() { if (auto *i = lv_indev_get_act()) lv_indev_wait_release(i); }
} // namespace

AdminSessionScreen::AdminSessionScreen(Host host) : _host(host) {
  _rootContexts[0] = {this, RootKind::Prompt};
  _rootContexts[1] = {this, RootKind::Console};
  _rootContexts[2] = {this, RootKind::Picker};
  for (unsigned i = 0; i < commandCount; ++i) _rows[i] = {this, i};
}
AdminSessionScreen::~AdminSessionScreen() {
  _destroying = true;
  closePicker(); closePrompt(); closeConsole(); clearPending();
  if (_log) platform::release(_log);
}
void AdminSessionScreen::alert(const char *message, unsigned ms) {
  if (_host.alert) _host.alert(_host.context, message, ms);
}
bool AdminSessionScreen::current(const uint8_t key[32]) const {
  return _targetValid && key && same(key, _target.key);
}
bool AdminSessionScreen::pendingKey(const uint8_t key[32]) const {
  return _pending != Pending::None && key && same(key, _pendingKey);
}
void AdminSessionScreen::cancelTimer() {
  if (_timer) { lv_timer_del(_timer); _timer = nullptr; }
}
void AdminSessionScreen::clearPending() {
  cancelTimer(); _pending = Pending::None;
  std::memset(_pendingKey, 0, sizeof _pendingKey);
  std::memset(_password, 0, sizeof _password);
  _rememberPassword = false;
}
void AdminSessionScreen::cancelDismiss() {
  if (!_dismissPending) return;
  lv_async_call_cancel(dismissDeferred, this);
  _dismissPending = false;
  if (_host.setKeyboardDismissPending)
    _host.setKeyboardDismissPending(_host.context, false);
}
void AdminSessionScreen::dismissDeferred(void *data) {
  auto *self = static_cast<AdminSessionScreen *>(data);
  if (!self || !self->_dismissPending) return;
  const uint32_t generation = self->_generation;
  self->_dismissPending = false;
  if (self->_host.setKeyboardDismissPending)
    self->_host.setKeyboardDismissPending(self->_host.context, false);
  if (!self->_destroying && self->_generation == generation && self->promptOpen() && self->_host.hideKeyboard)
    self->_host.hideKeyboard(self->_host.context);
}
void AdminSessionScreen::armTimer() {
  cancelTimer();
  _timer = lv_timer_create(timeout, 20000, this);
  if (_timer) lv_timer_set_repeat_count(_timer, 1);
}
void AdminSessionScreen::timeout(lv_timer_t *timer) {
  auto *self = static_cast<AdminSessionScreen *>(timer->user_data);
  if (!self || self->_destroying) return;
  self->_timer = nullptr; // LVGL deletes this one-shot timer.
  if (self->_pending == Pending::RoomRelogin) {
    uint8_t key[32]; std::memcpy(key, self->_pendingKey, 32);
    self->clearPending();
    self->openJoinByKey(key, true);
    return;
  }
  if (self->_pending != Pending::Password) return;
  self->clearPending();
  char when[24] = "clock degraded";
  if (self->_host.deviceClockLabel)
    self->_host.deviceClockLabel(self->_host.context, when, sizeof when);
  char message[160];
  std::snprintf(message, sizeof message,
                TR("No reply. Check password, server,\nor device clock (device: %s)"), when);
  self->alert(message, 5000);
}

// Remove only our callbacks. Other DELETE observers must still run when the
// host closes the root or an external owner deletes it.
void AdminSessionScreen::detachTree(lv_obj_t *object) {
  if (!object) return;
  const uint32_t count = lv_obj_get_child_cnt(object);
  for (uint32_t i = 0; i < count; ++i) detachTree(lv_obj_get_child(object, i));
  lv_obj_remove_event_cb_with_user_data(object, closeClicked, this);
  lv_obj_remove_event_cb_with_user_data(object, submitClicked, this);
  lv_obj_remove_event_cb_with_user_data(object, sendClicked, this);
  lv_obj_remove_event_cb_with_user_data(object, pickerClicked, this);
  for (unsigned i = 0; i < commandCount; ++i)
    lv_obj_remove_event_cb_with_user_data(object, rowClicked, &_rows[i]);
  for (auto &root : _rootContexts)
    lv_obj_remove_event_cb_with_user_data(object, deleted, &root);
}
void AdminSessionScreen::closeRoot(RootKind kind) {
  ++_generation;
  const uint32_t closedGeneration = _generation;
  ObjectRef *ref = kind == RootKind::Prompt ? &_promptRoot :
                   kind == RootKind::Console ? &_consoleRoot : &_pickerRoot;
  lv_obj_t *root = ref->get();
  if (root) detachTree(root);
  ref->set(nullptr);
  if (kind == RootKind::Prompt) {
    _promptText.set(nullptr); _remember.set(nullptr);
    if (_pending == Pending::Password) clearPending();
    cancelDismiss();
  } else if (kind == RootKind::Console) {
    _logBox.set(nullptr); _logLabel.set(nullptr); _commandText.set(nullptr);
  } else _rowCount = 0;
  // Callbacks are detached before keyboard and host closure, both of which may
  // pump LVGL or re-enter this owner.
  if (root) {
    if (kind != RootKind::Picker && _host.hideKeyboard &&
        _generation == closedGeneration) _host.hideKeyboard(_host.context);
    if (_host.closeRoot) _host.closeRoot(_host.context, &root);
    else { waitRelease(); lv_obj_del_async(root); }
  }
}
void AdminSessionScreen::externalDelete(RootKind kind, lv_obj_t *root) {
  ObjectRef *ref = kind == RootKind::Prompt ? &_promptRoot :
                   kind == RootKind::Console ? &_consoleRoot : &_pickerRoot;
  if (ref->get() || !root) return; // ObjectRef clears before this observer.
  ++_generation;
  // DELETE is already being dispatched on root. Removing its current event
  // descriptor can prevent later observers from receiving DELETE; detach only
  // descendants, whose deletion has not begun yet.
  const uint32_t count = lv_obj_get_child_cnt(root);
  for (uint32_t i = 0; i < count; ++i) detachTree(lv_obj_get_child(root, i));
  if (kind == RootKind::Prompt) {
    _promptText.set(nullptr); _remember.set(nullptr);
    if (_pending == Pending::Password) clearPending();
    cancelDismiss();
  } else if (kind == RootKind::Console) {
    _logBox.set(nullptr); _logLabel.set(nullptr); _commandText.set(nullptr);
    closePicker();
  } else _rowCount = 0;
}
void AdminSessionScreen::deleted(lv_event_t *event) {
  auto *ctx = static_cast<RootContext *>(lv_event_get_user_data(event));
  if (ctx && ctx->owner && !ctx->owner->_destroying)
    ctx->owner->externalDelete(ctx->kind, lv_event_get_current_target(event));
}
void AdminSessionScreen::closePicker() { closeRoot(RootKind::Picker); }
void AdminSessionScreen::closePrompt() { closeRoot(RootKind::Prompt); }
void AdminSessionScreen::closeConsole() {
  const uint32_t expected = _generation + 1;
  closePicker();
  if (_generation == expected) closeRoot(RootKind::Console);
}
void AdminSessionScreen::closeClicked(lv_event_t *event) {
  auto *self = static_cast<AdminSessionScreen *>(lv_event_get_user_data(event));
  if (!self || self->_destroying || lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  lv_obj_t *target = lv_event_get_current_target(event);
  // Only the backdrop itself dismisses on background tap.
  if ((target == self->_promptRoot.get() || target == self->_pickerRoot.get()) &&
      lv_event_get_target(event) != target) return;
  waitRelease();
  while (target) {
    if (target == self->_pickerRoot.get()) { self->closePicker(); return; }
    if (target == self->_promptRoot.get()) { self->closePrompt(); return; }
    if (target == self->_consoleRoot.get()) { self->closeConsole(); return; }
    target = lv_obj_get_parent(target);
  }
}
void AdminSessionScreen::submitClicked(lv_event_t *event) {
  auto *self = static_cast<AdminSessionScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying &&
      (lv_event_get_code(event) == LV_EVENT_CLICKED || lv_event_get_code(event) == LV_EVENT_READY)) self->submit();
}
void AdminSessionScreen::sendClicked(lv_event_t *event) {
  auto *self = static_cast<AdminSessionScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying &&
      (lv_event_get_code(event) == LV_EVENT_CLICKED || lv_event_get_code(event) == LV_EVENT_READY)) self->sendCommand();
}
void AdminSessionScreen::pickerClicked(lv_event_t *event) {
  auto *self = static_cast<AdminSessionScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying && lv_event_get_code(event) == LV_EVENT_CLICKED) self->openPicker();
}
void AdminSessionScreen::rowClicked(lv_event_t *event) {
  auto *ctx = static_cast<RowContext *>(lv_event_get_user_data(event));
  if (!ctx || !ctx->owner || ctx->owner->_destroying || lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *self = ctx->owner;
  if (!self->pickerOpen() || ctx->index >= commandCount) return;
  const char *command = commands[ctx->index].text;
  if (command && self->_commandText.get()) {
    lv_textarea_set_text(self->_commandText.get(), command);
    lv_textarea_set_cursor_pos(self->_commandText.get(), LV_TEXTAREA_CURSOR_LAST);
  }
  const uint32_t expected = self->_generation + 1;
  self->closePicker();
  if (!self->_destroying && self->_generation == expected && self->_host.hideKeyboard)
    self->_host.hideKeyboard(self->_host.context);
}

void AdminSessionScreen::openAdmin(const Contact &contact) {
  openPrompt(contact, PromptKind::Admin, false);
}
void AdminSessionScreen::openJoin(const Contact &contact, bool afterReloginFailed) {
  if (!contact.room) { alert(TR("Contact gone"), 1200); return; }
  openPrompt(contact, PromptKind::Join, afterReloginFailed);
}
void AdminSessionScreen::openJoinByKey(const uint8_t key[32], bool afterReloginFailed) {
  Contact contact{};
  const uint32_t generation = _generation;
  const bool found = _host.resolve && _host.resolve(_host.context, key, contact);
  if (_destroying || _generation != generation) return;
  if (!found || !contact.room || !same(key, contact.key)) {
    alert(TR("Contact gone"), 1200); return;
  }
  openJoin(contact, afterReloginFailed);
}
void AdminSessionScreen::beginRoomRelogin(const uint8_t key[32]) {
  if (_destroying || !key) return;
  uint8_t copiedKey[32];
  std::memcpy(copiedKey, key, sizeof copiedKey);
  Contact contact{};
  const uint32_t initialGeneration = _generation;
  const bool found = _host.resolve && _host.resolve(_host.context, copiedKey, contact);
  if (_destroying || _generation != initialGeneration) return;
  if (!found || !contact.room || !same(copiedKey, contact.key)) {
    alert(TR("Contact gone"), 1200); return;
  }
  if (promptOpen()) {
    const uint32_t expected = _generation + 1;
    closePrompt();
    if (_destroying || _generation != expected) return;
  }
  clearPending();
  _pending = Pending::RoomRelogin;
  std::memcpy(_pendingKey, copiedKey, 32);
  const uint32_t generation = _generation;
  const bool sent = _host.sendRoomRelogin && _host.sendRoomRelogin(_host.context, copiedKey);
  if (_destroying || _generation != generation || _pending != Pending::RoomRelogin || !pendingKey(copiedKey)) return;
  if (!sent) { clearPending(); alert(TR("Couldn't send login"), 1400); return; }
  if (_host.markMeshRequest) _host.markMeshRequest(_host.context);
  if (_destroying || _generation != generation || !pendingKey(copiedKey)) return;
  alert(TR("Logging in again..."), 1400);
  if (_destroying || _generation != generation || !pendingKey(copiedKey)) return;
  armTimer();
}

void AdminSessionScreen::submit() {
  if (!promptOpen() || !_targetValid || _pending != Pending::None ||
      _dismissPending || !_promptText.get()) return;
  const uint32_t startGeneration = _generation;
  if (_host.syncKeyboard) _host.syncKeyboard(_host.context);
  if (_destroying || _generation != startGeneration || !promptOpen()) return;
  waitRelease();
  _dismissPending = true;
  if (_host.setKeyboardDismissPending)
    _host.setKeyboardDismissPending(_host.context, true);
  if (_destroying || _generation != startGeneration || !promptOpen()) return;
  if (lv_async_call(dismissDeferred, this) != LV_RES_OK) {
    _dismissPending = false;
    if (_host.setKeyboardDismissPending)
      _host.setKeyboardDismissPending(_host.context, false);
    if (_destroying || _generation != startGeneration || !promptOpen()) return;
    if (_host.hideKeyboard) _host.hideKeyboard(_host.context);
  }
  if (_destroying || _generation != startGeneration || !promptOpen() || !_promptText.get()) return;
  safeCopy(_password, sizeof _password, lv_textarea_get_text(_promptText.get()));
  _rememberPassword = _remember.get() && lv_obj_has_state(_remember.get(), LV_STATE_CHECKED);
  _pending = Pending::Password;
  std::memcpy(_pendingKey, _target.key, 32);
  const uint32_t generation = _generation;
  const bool sent = _host.sendLogin && _host.sendLogin(_host.context, _pendingKey, _password);
  if (_destroying || _generation != generation || _pending != Pending::Password) return;
  if (!sent) { clearPending(); alert(TR("Send failed"), 1200); return; }
  if (_host.markMeshRequest) _host.markMeshRequest(_host.context);
  if (_destroying || _generation != generation || _pending != Pending::Password) return;
  alert(TR("Logging in\xe2\x80\xa6"), 1500);
  if (_destroying || _generation != generation || _pending != Pending::Password) return;
  armTimer();
}
void AdminSessionScreen::onLoginResult(const uint8_t key[32], bool room, bool success, uint8_t perms) {
  if (_destroying || !pendingKey(key)) return;
  if (_pending == Pending::RoomRelogin) {
    if (!room) return;
    const uint32_t generation = _generation;
    clearPending();
    if (success) {
      Contact contact{};
      if (_host.resolve && _host.resolve(_host.context, key, contact) && contact.room) {
        if (_destroying || _generation != generation || !same(contact.key, key)) return;
        char message[80];
        std::snprintf(message, sizeof message, TR("Reconnected to %.40s"), contact.name);
        alert(message, 1400);
      }
    } else openJoinByKey(key, true);
    return;
  }
  if (_pending != Pending::Password || !current(key) || room != _target.room || !promptOpen()) return;
  Contact target = _target;
  const PromptKind kind = _promptKind;
  const uint32_t generation = _generation;
  // The successful password is persisted before wiping the attempt. A failed
  // login never replaces a previously remembered password.
  if (success && _host.storePassword)
    _host.storePassword(_host.context, key, _rememberPassword ? _password : "");
  if (_destroying || _generation != generation || !pendingKey(key) || !current(key) || !promptOpen()) return;
  clearPending();
  const uint32_t expectedClose = _generation + 1;
  closePrompt();
  if (_destroying || _generation != expectedClose) return;
  if (!success) { alert(TR("Login failed"), 2000); return; }
  if (kind == PromptKind::Join) {
    Contact live{};
    const uint32_t beforeResolve = _generation;
    const bool found = _host.resolve && _host.resolve(_host.context, key, live);
    if (_destroying || _generation != beforeResolve) return;
    if (!found || !live.room || !same(live.key, key) ||
        !_host.openRoomChat || !_host.openRoomChat(_host.context, key)) {
      alert(TR("Contact gone"), 1200); return;
    }
    if (_destroying || _generation != beforeResolve) return;
    char message[80];
    std::snprintf(message, sizeof message, TR("Joined %.40s"), live.name);
    alert(message, 1400);
  } else {
    openConsole(target);
    if (_destroying || !consoleOpen() || !current(key)) return;
    char message[80];
    std::snprintf(message, sizeof message, TR("Login OK (perms %u)"), unsigned(perms));
    alert(message, 1200);
  }
}
void AdminSessionScreen::onCommandReply(const uint8_t key[32], const char *text) {
  if (consoleOpen() && current(key)) appendLog("", text ? text : "");
}
void AdminSessionScreen::appendLog(const char *prefix, const char *text) {
  if (!_log) return; // Console remains usable if the optional log allocation fails.
  // Advance by actual bytes copied; snprintf's would-have-written count is
  // unsafe here because a long CLI reply can overflow the next append.
  auto push = [this](char ch) {
    if (_logLength >= 1024 - 2) {
      const size_t discard = 1024 / 2;
      std::memmove(_log, _log + discard, _logLength - discard);
      _logLength -= discard;
    }
    _log[_logLength++] = ch;
    _log[_logLength] = '\0';
  };
  if (prefix) for (const char *p = prefix; *p; ++p) push(*p);
  if (text) for (const char *p = text; *p; ++p) push(*p);
  push('\n');
  if (_logLabel.get()) lv_label_set_text(_logLabel.get(), _log);
  if (_logBox.get()) lv_obj_scroll_to_y(_logBox.get(), LV_COORD_MAX, LV_ANIM_OFF);
}
void AdminSessionScreen::sendCommand() {
  if (!consoleOpen() || !_commandText.get() || !_targetValid) return;
  const uint32_t beforeSync = _generation;
  if (_host.syncKeyboard) _host.syncKeyboard(_host.context);
  if (_destroying || _generation != beforeSync || !consoleOpen() || !_commandText.get()) return;
  const char *raw = lv_textarea_get_text(_commandText.get());
  if (!raw || !*raw) return;
  char command[65]; safeCopy(command, sizeof command, raw);
  const uint32_t generation = _generation;
  Contact live{};
  const bool found = _host.resolve && _host.resolve(_host.context, _target.key, live);
  if (_destroying || _generation != generation || !consoleOpen() || !_commandText.get()) return;
  if (!found || !same(live.key, _target.key)) {
    appendLog("[err] ", "contact missing");
    return; // Keep the draft for retry, as the previous console did.
  }
  const bool sent = _host.sendCommand && _host.sendCommand(_host.context, _target.key, command);
  if (_destroying || _generation != generation || !consoleOpen()) return;
  char line[80]; std::snprintf(line, sizeof line, "> %s", command);
  appendLog("", line);
  if (!sent) appendLog("[err] ", "send failed");
  if (_commandText.get()) lv_textarea_set_text(_commandText.get(), "");
}

void AdminSessionScreen::openPrompt(const Contact &contact, PromptKind kind, bool afterReloginFailed) {
  if (_destroying) return;
  Contact snapshot = contact;
  snapshot.name[sizeof snapshot.name - 1] = '\0';
  uint32_t expected = _generation + 1;
  closePicker(); if (_generation != expected) return;
  expected = _generation + 1;
  closePrompt(); if (_generation != expected) return;
  expected = _generation + 1;
  closeRoot(RootKind::Console); if (_generation != expected) return;
  clearPending();
  _target = snapshot;
  _targetValid = true;
  _promptKind = kind;
  const uint32_t buildGeneration = _generation;
  const int status = _host.statusHeight ? _host.statusHeight(_host.context) : 0;
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr), sh = lv_disp_get_ver_res(nullptr);
  lv_obj_t *root = lv_obj_create(lv_layer_top());
  if (!_promptRoot.set(root)) { lv_obj_del(root); return; }
  lv_obj_add_event_cb(root, deleted, LV_EVENT_DELETE, &_rootContexts[0]);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, sw, sh - status); lv_obj_set_pos(root, 0, status);
  lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(root, closeClicked, LV_EVENT_CLICKED, this);

  const int shift = afterReloginFailed ? PSC(14) : 0;
#if CAP_LARGE_SCREEN
  const int cardW = PCW(220), cardH = PSC(180) + shift;
#else
  const int cardW = 220, cardH = 180 + shift;
#endif
  lv_obj_t *card = lv_obj_create(root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, cardW, cardH);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_set_style_bg_color(card, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  addCloseXBadge(card, closeClicked, this);

  char name[24];
  if (_host.sanitize) _host.sanitize(_host.context, &font14(), name, sizeof name, snapshot.name);
  else safeCopy(name, sizeof name, snapshot.name);
  if (_destroying || _generation != buildGeneration || !promptOpen()) return;
  char titleText[40];
  std::snprintf(titleText, sizeof titleText, "%s %.20s", kind == PromptKind::Join ? "Join:" : "Login:", name);
  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, titleText);
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &font14(), LV_PART_MAIN);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, cardW - 20 - 32);
  lv_obj_set_pos(title, 0, 0);
  lv_obj_t *subtitle = lv_label_create(card);
  lv_label_set_text(subtitle, afterReloginFailed
    ? TR("Server has forgotten you.\nJoin with the room password.")
    : (snapshot.room ? "Room password (blank = guest)" : "Password (blank = guest)"));
  lv_obj_set_style_text_color(subtitle, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(subtitle, &font12(), LV_PART_MAIN);
  if (afterReloginFailed) {
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(subtitle, cardW - 20, PSC(28));
  }
  lv_obj_set_pos(subtitle, 0, PSC(22));

  lv_obj_t *field = lv_textarea_create(card);
  _promptText.set(field);
  lv_obj_set_size(field, cardW - 20, PSC(32));
  lv_obj_set_pos(field, 0, PSC(42) + shift);
  styleCard(field); lv_textarea_set_one_line(field, true);
  lv_textarea_set_password_mode(field, true);
  lv_textarea_set_max_length(field, 15);
  taSetPlaceholder(field, "");
  lv_obj_set_style_text_color(field, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(field, &font14(), LV_PART_MAIN);
  if (_host.attachTextArea) _host.attachTextArea(_host.context, field);
  if (_destroying || _generation != buildGeneration || _promptText.get() != field) return;
  if (_host.attachSymbolButton) _host.attachSymbolButton(_host.context, field);
  if (_destroying || _generation != buildGeneration || _promptText.get() != field) return;
  char saved[sizeof _password] = {};
  const bool hasSaved = _host.loadPassword &&
    _host.loadPassword(_host.context, snapshot.key, saved, sizeof saved) && saved[0];
  saved[sizeof saved - 1] = '\0';
  if (_destroying || _generation != buildGeneration || _promptText.get() != field) {
    std::memset(saved, 0, sizeof saved); return;
  }
  if (hasSaved) lv_textarea_set_text(field, saved);
  std::memset(saved, 0, sizeof saved);
  if (_host.hasKeyboard && _host.hasKeyboard(_host.context) && !hasSaved && _host.bindKeyboard)
    _host.bindKeyboard(_host.context, field);
  if (_destroying || _generation != buildGeneration || _promptText.get() != field) return;
  lv_obj_add_event_cb(field, submitClicked, LV_EVENT_READY, this);

  lv_obj_t *remember = lv_checkbox_create(card);
  _remember.set(remember);
  lv_checkbox_set_text(remember, TR("Remember password"));
  lv_obj_set_style_text_color(remember, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(remember, &font12(), LV_PART_MAIN);
  lv_obj_set_pos(remember, 0, PSC(82) + shift);
  lv_obj_add_state(remember, LV_STATE_CHECKED);

  lv_obj_t *cancel = lv_btn_create(card);
  lv_obj_set_size(cancel, PSC(88), PSC(32));
  lv_obj_set_pos(cancel, 0, PSC(116) + shift);
  styleButton(cancel);
  lv_obj_add_event_cb(cancel, closeClicked, LV_EVENT_CLICKED, this);
  lv_obj_t *cl = lv_label_create(cancel);
  useChainedFont(cl); lv_label_set_text(cl, TR("Cancel")); lv_obj_center(cl);
  lv_obj_t *login = lv_btn_create(card);
  lv_obj_set_size(login, PSC(100), PSC(32));
  lv_obj_set_pos(login, cardW - 20 - PSC(100), PSC(116) + shift);
  styleButton(login);
  lv_obj_set_style_bg_color(login, lv_color_hex(colors().COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_set_style_text_color(login, lv_color_hex(colors().COLOR_ON_STATUS_OK), LV_PART_MAIN);
  lv_obj_add_event_cb(login, submitClicked, LV_EVENT_CLICKED, this);
  lv_obj_t *ll = lv_label_create(login);
  useChainedFont(ll); lv_label_set_text(ll, snapshot.room ? "Join" : "Login"); lv_obj_center(ll);
}

void AdminSessionScreen::openConsole(const Contact &contact) {
  if (_destroying) return;
  Contact snapshot = contact;
  snapshot.name[sizeof snapshot.name - 1] = '\0';
  uint32_t expected = _generation + 1;
  closePrompt(); if (_generation != expected) return;
  expected = _generation + 1;
  closePicker(); if (_generation != expected) return;
  expected = _generation + 1;
  closeRoot(RootKind::Console); if (_generation != expected) return;
  _target = snapshot; _targetValid = true;
  const uint32_t buildGeneration = _generation;
  const int status = _host.statusHeight ? _host.statusHeight(_host.context) : 0;
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr), sh = lv_disp_get_ver_res(nullptr);
  const lv_coord_t adminH = sh - status;
  lv_obj_t *root = lv_obj_create(lv_layer_top());
  if (!_consoleRoot.set(root)) { lv_obj_del(root); return; }
  lv_obj_add_event_cb(root, deleted, LV_EVENT_DELETE, &_rootContexts[1]);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, sw, adminH); lv_obj_set_pos(root, 0, status);
  styleSurface(root, colors().COLOR_BG, 0);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *header = lv_obj_create(root);
  lv_obj_remove_style_all(header); lv_obj_set_size(header, sw, 36); lv_obj_set_pos(header, 0, 0);
  styleSurface(header, colors().COLOR_PANEL, 0);
  lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_style_border_width(header, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(header, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  char name[24];
  if (_host.sanitize) _host.sanitize(_host.context, &font14(), name, sizeof name, snapshot.name);
  else safeCopy(name, sizeof name, snapshot.name);
  if (_destroying || _generation != buildGeneration || !consoleOpen()) return;
  char titleText[40]; std::snprintf(titleText, sizeof titleText, "Admin: %s", name);
  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title, titleText);
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &font14(), LV_PART_MAIN);
  lv_obj_set_pos(title, 8, 9);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, sw - 44);
  addCloseXBadge(header, closeClicked, this);

  lv_obj_t *box = lv_obj_create(root); _logBox.set(box);
  lv_obj_remove_style_all(box);
  lv_obj_set_size(box, sw - 8, adminH - 36 - 8 - 44);
  lv_obj_set_pos(box, 4, 40);
  styleSurface(box, 0x0A0B0C, 6);
  lv_obj_set_style_border_color(box, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(box, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(box, 6, LV_PART_MAIN);
  lv_obj_set_scroll_dir(box, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_t *label = lv_label_create(box); _logLabel.set(label);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, sw - 20);
  lv_obj_set_style_text_color(label, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(label, &font12(), LV_PART_MAIN);
  if (!_log) _log = static_cast<char *>(platform::allocate(1024, true));
  if (!_log) _log = static_cast<char *>(platform::allocate(1024, false));
  if (_log) { _logLength = 0; _log[0] = '\0'; appendLog("", help); }
  else { lv_label_set_text(label, TR("Out of memory")); alert(TR("Out of memory"), 1800); }
  if (_destroying || _generation != buildGeneration || !consoleOpen()) return;

  lv_obj_t *row = lv_obj_create(root);
  lv_obj_remove_style_all(row); lv_obj_set_size(row, sw, 44);
  lv_obj_set_pos(row, 0, adminH - 44);
  styleSurface(row, colors().COLOR_PANEL, 0);
  lv_obj_set_style_border_side(row, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(row, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *picker = lv_btn_create(row);
  lv_obj_set_size(picker, 32, 32); lv_obj_align(picker, LV_ALIGN_LEFT_MID, 4, 0);
  styleButton(picker);
  lv_obj_set_style_bg_color(picker, lv_color_hex(colors().COLOR_CONTROL), LV_PART_MAIN);
  lv_obj_set_style_pad_all(picker, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(picker, pickerClicked, LV_EVENT_CLICKED, this);
  lv_obj_t *pickerLabel = lv_label_create(picker);
  lv_label_set_text(pickerLabel, LV_SYMBOL_LIST);
  lv_obj_set_style_text_font(pickerLabel, &font14(), LV_PART_MAIN);
  lv_obj_center(pickerLabel);
  lv_obj_t *field = lv_textarea_create(row); _commandText.set(field);
  lv_obj_set_size(field, sw - 104, 32); lv_obj_align(field, LV_ALIGN_LEFT_MID, 40, 0);
  styleCard(field); lv_textarea_set_one_line(field, true);
  lv_textarea_set_max_length(field, 64); taSetPlaceholder(field, TR("command"));
  lv_obj_set_style_text_color(field, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(field, &font14(), LV_PART_MAIN);
  if (_host.attachTextArea) _host.attachTextArea(_host.context, field);
  if (_destroying || _generation != buildGeneration || _commandText.get() != field) return;
  lv_obj_add_event_cb(field, sendClicked, LV_EVENT_READY, this);
  lv_obj_t *send = lv_btn_create(row);
  lv_obj_set_size(send, 56, 32); lv_obj_align(send, LV_ALIGN_RIGHT_MID, -4, 0);
  styleButton(send);
  lv_obj_set_style_bg_color(send, lv_color_hex(colors().COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_set_style_text_color(send, lv_color_hex(colors().COLOR_ON_STATUS_OK), LV_PART_MAIN);
  lv_obj_add_event_cb(send, sendClicked, LV_EVENT_CLICKED, this);
  lv_obj_t *sendLabel = lv_label_create(send);
  useChainedFont(sendLabel); lv_label_set_text(sendLabel, LV_SYMBOL_RIGHT); lv_obj_center(sendLabel);
}

void AdminSessionScreen::openPicker() {
  if (_destroying || !consoleOpen()) return;
  const uint32_t expected = _generation + 1;
  closePicker();
  if (_generation != expected || !consoleOpen()) return;
  const int status = _host.statusHeight ? _host.statusHeight(_host.context) : 0;
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr), sh = lv_disp_get_ver_res(nullptr);
  lv_obj_t *root = lv_obj_create(lv_layer_top());
  if (!_pickerRoot.set(root)) { lv_obj_del(root); return; }
  lv_obj_add_event_cb(root, deleted, LV_EVENT_DELETE, &_rootContexts[2]);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, sw, sh - status); lv_obj_set_pos(root, 0, status);
  lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_70, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(root, closeClicked, LV_EVENT_CLICKED, this);
  const int cardW = sw - 20, cardH = sh - status - 40;
  lv_obj_t *card = lv_obj_create(root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, cardW, cardH); lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 6, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, TR("Commands"));
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &font14(), LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 4);
  addCloseXBadge(card, closeClicked, this);
  lv_obj_t *list = lv_list_create(card);
  lv_obj_set_size(list, cardW - 12, cardH - 40);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 28);
  lv_obj_set_style_bg_color(list, lv_color_hex(colors().COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
  for (unsigned i = 0; i < commandCount; ++i) {
    const Command &entry = commands[i];
    if (!entry.text) {
      lv_obj_t *section = lv_list_add_text(list, entry.label);
      lv_obj_set_style_text_color(section, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
      lv_obj_set_style_text_font(section, &font12(), LV_PART_MAIN);
      lv_obj_set_style_bg_color(section, lv_color_hex(themeRole(0x0F1722, colors().COLOR_ACCENT_SURFACE)), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(section, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_width(section, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_ver(section, 6, LV_PART_MAIN);
      lv_obj_set_style_pad_left(section, 10, LV_PART_MAIN);
      continue;
    }
    lv_obj_t *button = lv_list_add_btn(list, nullptr, entry.label);
    lv_obj_set_style_text_font(button, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(button, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(colors().COLOR_CONTROL_PRESSED), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(colors().COLOR_CONTROL_PRESSED), LV_PART_MAIN);
    lv_obj_set_style_border_side(button, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_min_height(button, 30, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(button, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_left(button, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(button, rowClicked, LV_EVENT_CLICKED, &_rows[i]);
  }
  _rowCount = commandCount;
}

} } // namespace ui::screens

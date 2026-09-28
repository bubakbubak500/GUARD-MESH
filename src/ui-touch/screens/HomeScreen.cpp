// SPDX-License-Identifier: GPL-3.0-or-later
#include "HomeScreen.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/GuardianShield.h"
#include <cstdio>
#include <cstring>

namespace ui {
namespace screens {
namespace {
void setText(lv_obj_t *label, const char *value) {
  if (strcmp(lv_label_get_text(label), value))
    lv_label_set_text(label, value);
}
void plain(lv_obj_t *object) {
  lv_obj_remove_style_all(object);
  lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}
void panel(lv_obj_t *object) {
  plain(object);
  lv_obj_set_style_bg_color(object, lv_color_hex(theme::colors().COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(object, lv_color_hex(theme::colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(object, 6, LV_PART_MAIN);
}
void labelStyle(lv_obj_t *label, const lv_font_t *font, uint32_t color) {
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
}
} // namespace

HomeScreen::~HomeScreen() { detach(); }

void HomeScreen::detach() {
  // Disarm callbacks before deleting the owned child tree. The parent is borrowed.
  if (auto *root = _root.get()) {
    lv_obj_remove_event_cb_with_user_data(_messageCard, actionEvent, &_actionBindings[0]);
    lv_obj_remove_event_cb_with_user_data(_guardian, actionEvent, &_actionBindings[6]);
    for (int i = 0; i < 5; ++i)
      lv_obj_remove_event_cb_with_user_data(_actions[i], actionEvent, &_actionBindings[i + 1]);
    for (int i = 0; i < 3; ++i)
      lv_obj_remove_event_cb_with_user_data(_rows[i].root, rowEvent, &_rowBindings[i]);
    _root.set(nullptr);
    lv_obj_del(root);
  }
  _messageCard = _empty = _unread = nullptr;
  _guardian = _guardianTitle = _guardianStatus = nullptr;
  for (auto &button : _actions) button = nullptr;
  for (auto &row : _rows) row = Row{};
  _width = _height = 0;
}

bool HomeScreen::create(lv_obj_t *parent, int width, int height) {
  if (!parent || width < 240 || height < 180)
    return false;
  if (_root.get() && lv_obj_get_parent(_root.get()) == parent && _width == width && _height == height)
    return true;
  detach();
  auto *root = lv_obj_create(parent);
  if (!root || !_root.set(root)) {
    if (root) lv_obj_del(root);
    return false;
  }
  _width = width;
  _height = height;
  plain(root);
  lv_obj_set_size(root, width, height);
  lv_obj_set_style_bg_color(root, lv_color_hex(theme::colors().COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);

  const int contentH = height - 16;
  const int leftW = width - 8 - 100 - 8 - 8;
  const int messageH = contentH - 8 - 40;
  _messageCard = lv_obj_create(root);
  panel(_messageCard);
  lv_obj_set_pos(_messageCard, 8, 8);
  lv_obj_set_size(_messageCard, leftW, messageH);
  lv_obj_add_flag(_messageCard, LV_OBJ_FLAG_CLICKABLE);
  _actionBindings[0].owner = this;
  _actionBindings[0].action = Action::Inbox;
  lv_obj_add_event_cb(_messageCard, actionEvent, LV_EVENT_CLICKED, &_actionBindings[0]);

  _unread = lv_label_create(_messageCard);
  labelStyle(_unread, &theme::font12(), theme::colors().COLOR_ACCENT);
  lv_obj_set_size(_unread, leftW - 14, 16);
  lv_obj_set_pos(_unread, 7, 5);
  lv_label_set_text(_unread, "0");

  for (int i = 0; i < 3; ++i) {
    auto &row = _rows[i];
    row.root = lv_obj_create(_messageCard);
    plain(row.root);
    lv_obj_set_style_border_color(row.root, lv_color_hex(theme::colors().COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(row.root, i ? 1 : 0, LV_PART_MAIN);
    lv_obj_set_style_border_side(row.root, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_pos(row.root, 5, 23 + i * 31);
    lv_obj_set_size(row.root, leftW - 10, 31);
    lv_obj_add_flag(row.root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    row.name = lv_label_create(row.root);
    labelStyle(row.name, &theme::font12(), theme::colors().COLOR_TEXT);
    lv_obj_set_pos(row.name, 2, 1);
    lv_obj_set_size(row.name, leftW - 14, 14);
    row.text = lv_label_create(row.root);
    labelStyle(row.text, &theme::font12(), theme::colors().COLOR_SUB);
    lv_obj_set_pos(row.text, 2, 15);
    lv_obj_set_size(row.text, leftW - 14, 14);
    _rowBindings[i].owner = this;
    _rowBindings[i].slot = i;
    lv_obj_add_event_cb(row.root, rowEvent, LV_EVENT_CLICKED, &_rowBindings[i]);
  }
  _empty = lv_label_create(_messageCard);
  lv_label_set_text(_empty, TR("All marked read"));
  labelStyle(_empty, &theme::font12(), theme::colors().COLOR_SUB);
  lv_obj_set_pos(_empty, 7, 38);
  lv_obj_set_size(_empty, leftW - 14, 18);

  auto *guardian = lv_obj_create(root);
  _guardian = guardian;
  panel(guardian);
  lv_obj_add_flag(guardian, LV_OBJ_FLAG_CLICKABLE);
  _actionBindings[6].owner = this;
  _actionBindings[6].action = Action::Guardian;
  lv_obj_add_event_cb(guardian, actionEvent, LV_EVENT_CLICKED, &_actionBindings[6]);
  lv_obj_set_pos(guardian, 8, 8 + messageH + 8);
  lv_obj_set_size(guardian, leftW, 40);
  _guardianShield = widgets::guardianShield(guardian, 30);
  lv_obj_set_pos(_guardianShield, 4, 4);
  _guardianHadStatus = false; _guardianNoticeUntil = 0;
  auto *guardianTitle = lv_label_create(guardian);
  _guardianTitle = guardianTitle;
  lv_label_set_text(guardianTitle, TR("Guardian BLE off"));
  labelStyle(guardianTitle, &theme::font12(), theme::colors().COLOR_TEXT);
  lv_obj_set_pos(guardianTitle, 41, 3);
  lv_obj_set_size(guardianTitle, leftW - 47, 15);
  auto *guardianStatus = lv_label_create(guardian);
  _guardianStatus = guardianStatus;
  lv_label_set_text(guardianStatus, "TX: —  RX: —");
  labelStyle(guardianStatus, &theme::font12(), theme::colors().COLOR_SUB);
  lv_obj_set_pos(guardianStatus, 41, 20);
  lv_obj_set_size(guardianStatus, leftW - 47, 15);
  for (int i = 0; i < 2; ++i) {
    auto* bar = _guardianBars[i] = lv_bar_create(guardian);
    lv_obj_remove_style_all(bar); lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x26343E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(i ? 0xFFD06A : 0x5DD3D5), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 2, LV_PART_MAIN); lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
  }

  static constexpr Action kinds[] = {Action::Advert, Action::Terminal, Action::Discover,
                                     Action::Apps, Action::Control};
  static const char *const names[] = {"Advert", "Terminal", "Discover", "Apps", "Control"};
  static const char *const icons[] = {LV_SYMBOL_UPLOAD, ">_", LV_SYMBOL_REFRESH, LV_SYMBOL_LIST, LV_SYMBOL_SETTINGS};
  const int buttonGap = 4;
  const int buttonH = (contentH - 4 * buttonGap) / 5;
  for (int i = 0; i < 5; ++i) {
    auto *button = lv_btn_create(root);
    _actions[i] = button;
    plain(button);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    const bool apps = kinds[i] == Action::Apps;
    lv_obj_set_style_bg_color(button, lv_color_hex(apps ? 0xF29D38 : theme::colors().COLOR_CONTROL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(apps ? 0xDB8522 : theme::colors().COLOR_CONTROL_PRESSED),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(button, 5, LV_PART_MAIN);
    lv_obj_set_pos(button, width - 108, 8 + i * (buttonH + buttonGap));
    lv_obj_set_size(button, 100, buttonH);
    auto *title = lv_label_create(button);
    char caption[64];
    const char* captionText = kinds[i] == Action::Discover && i18nGetLang() == LANG_CS
                                ? TR("Search") : TR(names[i]);
    snprintf(caption,sizeof caption,"%s  %s",icons[i],captionText);
    lv_label_set_text(title, caption);
    labelStyle(title, &theme::font12(), apps ? 0x1B1B1B : theme::colors().COLOR_TEXT);
    lv_obj_set_width(title, 92);
    lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,LV_PART_MAIN);
    lv_obj_center(title);
    _actionBindings[i + 1].owner = this;
    _actionBindings[i + 1].action = kinds[i];
    lv_obj_add_event_cb(button, actionEvent, LV_EVENT_CLICKED, &_actionBindings[i + 1]);
  }
  // Resolve the new parent's geometry before a first refresh writes LONG_DOT
  // labels; otherwise their initial zero-width layout may retain only "...".
  lv_obj_update_layout(root);
  return true;
}

void HomeScreen::refresh(int totalUnread, const Preview *rows, int count) {
  if (!active()) return;
  char countText[48];
  snprintf(countText, sizeof countText, TR("Unread %d"), totalUnread < 0 ? 0 : totalUnread);
  setText(_unread, countText);
  if (!rows || count < 0) count = 0;
  if (count > 3) count = 3;
  int visible = 0;
  for (int i = 0; i < 3; ++i) {
    auto &row = _rows[i];
    if (i >= count || rows[i].index < 0) {
      row.visible = false;
      row.preview = Preview{};
      lv_obj_add_flag(row.root, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    row.preview = rows[i];
    row.preview.name[sizeof row.preview.name - 1] = '\0';
    row.preview.text[sizeof row.preview.text - 1] = '\0';
    char name[96] = {}, snippet[128] = {};
    if (_host.sanitize) {
      _host.sanitize(&theme::font12(), name, sizeof name, row.preview.name);
      _host.sanitize(&theme::font12(), snippet, sizeof snippet, row.preview.text);
    } else {
      snprintf(name, sizeof name, "%s", row.preview.name);
      snprintf(snippet, sizeof snippet, "%s", row.preview.text);
    }
    name[sizeof name - 1] = snippet[sizeof snippet - 1] = '\0';
    for (char *p = snippet; *p; ++p)
      if (*p == '\n' || *p == '\r') *p = ' ';
    setText(row.name, name);
    setText(row.text, snippet);
    row.visible = true;
    ++visible;
    lv_obj_clear_flag(row.root, LV_OBJ_FLAG_HIDDEN);
  }
  if (visible == 0) lv_obj_clear_flag(_empty, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(_empty, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *HomeScreen::actionTarget(Action action) const {
  if (!active()) return nullptr;
  if (action == Action::Guardian) return _guardian;
  if (action == Action::Inbox) return _messageCard;
  const int index = static_cast<int>(action) - 1;
  return index >= 0 && index < 5 ? _actions[index] : nullptr;
}

void HomeScreen::refreshGuardian(const guardian::Snapshot& state, uint32_t now) {
  if (!active()) return;
  char title[64], detail[96];
  const bool fresh = state.session.fresh(now);
  for (auto* bar : _guardianBars) lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_opa(_guardianShield, fresh ? LV_OPA_COVER : LV_OPA_40, LV_PART_MAIN);
  lv_obj_set_style_text_color(_guardianTitle, lv_color_hex(theme::colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_color(_guardianStatus, lv_color_hex(theme::colors().COLOR_SUB), LV_PART_MAIN);
  if (fresh) {
    const auto& s = state.session.status;
    if (_guardianHadStatus && s.inbox > _guardianInbox) _guardianNoticeUntil = now + 5000;
    _guardianHadStatus = true; _guardianInbox = s.inbox;
    const bool rx = s.flags & 4, tx = s.flags & 2;
    auto progress = [&](int slot, uint8_t percent, bool both) {
      auto* bar = _guardianBars[slot];
      lv_obj_set_pos(bar, both ? 111 : 41, both ? (slot ? 26 : 10) : 27);
      lv_obj_set_size(bar, both ? _width - 238 : _width - 171, 6);
      lv_bar_set_value(bar, percent <= 100 ? percent : 0, LV_ANIM_OFF);
      lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);
    };
    auto line = [](char* out, size_t n, const char* caption, uint8_t p) {
      if (p <= 100) snprintf(out, n, "%s %u%%", caption, p);
      else snprintf(out, n, "%s —", caption);
    };
    if (rx && tx) {
      line(title, sizeof title, "RX", state.session.rxPercent);
      line(detail, sizeof detail, "TX", state.session.txPercent);
      progress(0, state.session.rxPercent, true); progress(1, state.session.txPercent, true);
    } else if (rx || tx) {
      line(title, sizeof title, rx ? TR("Receiving") : TR("Sending"), rx ? state.session.rxPercent : state.session.txPercent);
      detail[0] = 0; progress(rx ? 0 : 1, rx ? state.session.rxPercent : state.session.txPercent, false);
    } else if (_guardianNoticeUntil && int32_t(_guardianNoticeUntil - now) > 0) {
      snprintf(title, sizeof title, "%s", TR("New Guardian message"));
      snprintf(detail, sizeof detail, TR("Inbox %lu / new %lu"), (unsigned long)s.inbox, (unsigned long)s.unread);
      lv_obj_set_style_text_color(_guardianTitle, lv_color_hex(0x5DD3D5), LV_PART_MAIN);
    } else if ((now / 3000) % 2) {
      snprintf(title, sizeof title, "Outbox: %lu", (unsigned long)s.outbox);
      snprintf(detail, sizeof detail, "%s", TR("Queued on PC"));
    } else {
      snprintf(title, sizeof title, "Inbox: %lu", (unsigned long)s.inbox);
      snprintf(detail, sizeof detail, TR("Unread: %lu"), (unsigned long)s.unread);
    }
  } else {
    _guardianHadStatus = false; _guardianNoticeUntil = 0;
    const char* caption = !state.enabled ? TR("Guardian BLE off") : !state.radio ? TR("Bluetooth off") :
      state.pairing ? TR("Guardian pairing") : state.session.connected ? TR("Guardian stale") : TR("Guardian offline");
    snprintf(title, sizeof title, "%s", caption);
    snprintf(detail, sizeof detail, "Inbox — / outbox —");
  }
  setText(_guardianTitle, title); setText(_guardianStatus, detail);
}

void HomeScreen::actionEvent(lv_event_t *event) {
  auto *binding = static_cast<ActionBinding *>(lv_event_get_user_data(event));
  auto *owner = binding->owner;
  if (owner && owner->active() && owner->_host.action &&
      owner->actionTarget(binding->action) == lv_event_get_current_target(event))
    owner->_host.action(binding->action);
}

void HomeScreen::rowEvent(lv_event_t *event) {
  lv_event_stop_bubbling(event);
  auto *binding = static_cast<RowBinding *>(lv_event_get_user_data(event));
  auto *owner = binding->owner;
  if (!owner || !owner->active() || binding->slot < 0 || binding->slot >= 3) return;
  const auto &row = owner->_rows[binding->slot];
  if (!row.visible || lv_obj_has_flag(row.root, LV_OBJ_FLAG_HIDDEN) ||
      row.root != lv_event_get_current_target(event) || !owner->_host.current || !owner->_host.select)
    return;
  if (owner->_host.current(row.preview))
    owner->_host.select(row.preview.index, row.preview.channel);
}

} // namespace screens
} // namespace ui

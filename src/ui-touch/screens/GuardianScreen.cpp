// SPDX-License-Identifier: GPL-3.0-or-later
#include "GuardianScreen.h"
#include "../theme/Theme.h"
#include "../theme/Fonts.h"
#include "../widgets/Styles.h"
#include "../i18n.h"
#include <stdio.h>
#include <string.h>
namespace ui { namespace screens {
namespace {
void text(lv_obj_t* label, const char* value) {
  if (strcmp(lv_label_get_text(label), value)) lv_label_set_text(label, value);
}
lv_obj_t* label(lv_obj_t* parent, int y, int width, const char* value) {
  auto* obj = lv_label_create(parent);
  lv_obj_set_pos(obj, 2, y); lv_obj_set_width(obj, width);
  lv_obj_set_style_text_font(obj, &theme::font14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(obj, lv_color_hex(theme::colors().COLOR_TEXT), LV_PART_MAIN);
  lv_label_set_text(obj, value);
  return obj;
}
}
void GuardianScreen::create(lv_obj_t* parent, void (*command)(guardian::Command)) {
  _root.set(parent); _command = command; _last = 0;
  lv_obj_update_layout(parent);
  const int width = lv_obj_get_content_width(parent) - 4;
  _state = label(parent, 2, width - 48, "Guardian BLE");
  _name = label(parent, 21, width, "");
  lv_obj_set_style_text_font(_name, &theme::font12(), LV_PART_MAIN);
  _counts = label(parent, 37, width, "");
  _radios = label(parent, 101, width, "");
  for (int i = 0; i < 2; ++i) {
    auto* button = lv_btn_create(parent);
    lv_obj_set_pos(button, i * (width / 2 + 2), 130);
    lv_obj_set_size(button, width / 2 - 2, 32);
    widgets::styleButton(button);
    auto* title = lv_label_create(button);
    lv_obj_set_style_text_font(title, &theme::font12(), LV_PART_MAIN);
    lv_obj_center(title);
    if (i == 0) _enable = title; else _pair = title;
    lv_obj_add_event_cb(button, clicked, LV_EVENT_CLICKED, this);
  }
  auto* hint = label(parent, 169, width,
    TR("On PC: Settings > Station settings > Guard Mesh. Pairing starts on PC. Phone BLE is paused."));
  lv_obj_set_style_text_font(hint, &theme::font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(hint, lv_color_hex(theme::colors().COLOR_SUB), LV_PART_MAIN);
  refresh(0);
}
void GuardianScreen::refresh(uint32_t now) {
  if (!_root.get() || (_last && uint32_t(now - _last) < 250)) return;
  _last = now;
  const auto state = guardian::snapshot(now);
  const auto& session = state.session;
  char value[256];
  if (!state.enabled) snprintf(value, sizeof value, "%s", TR("Guardian BLE off"));
  else if (!state.radio) snprintf(value, sizeof value, "%s", TR("Bluetooth off"));
  else if (!state.ready) snprintf(value, sizeof value, "%s", TR("Guardian BLE unavailable"));
  else if (state.pairing) snprintf(value, sizeof value, TR("Pairing: %lu s"), (unsigned long)state.pairingSeconds);
  else if (!session.connected) snprintf(value, sizeof value, "%s", TR("PC disconnected"));
  else if (!session.fresh(now)) snprintf(value, sizeof value, "%s", TR("Guardian stale / unavailable"));
  else snprintf(value, sizeof value, TR("PC connected - %lu s ago"), (unsigned long)((now - session.receivedAt) / 1000));
  text(_state, value);
  text(_name, state.deviceName);
  if (session.fresh(now)) {
    snprintf(value, sizeof value, TR("TX: %s   RX: %s\nInbox: %lu   Unread: %lu\nOutbox: %lu"),
      session.status.flags & 2 ? TR("Active") : TR("Idle"),
      session.status.flags & 4 ? TR("Active") : TR("Idle"),
      (unsigned long)session.status.inbox, (unsigned long)session.status.unread, (unsigned long)session.status.outbox);
    text(_counts, value);
    snprintf(value, sizeof value, "CAT: %s   VARA: %s   CTRL: %s",
      session.status.flags & 8 ? "+" : "-", session.status.flags & 16 ? "+" : "-", session.status.flags & 32 ? "+" : "-");
    text(_radios, value);
  } else {
    text(_counts, TR("TX: —   RX: —\nInbox: —   Unread: —\nOutbox: —"));
    text(_radios, "CAT: —   VARA: —   CTRL: —");
  }
  text(_enable, state.enabled && state.radio ? TR("Disable") : TR("Enable"));
  text(_pair, state.pairing ? TR("Cancel pairing") : TR("Pair PC (2 min)"));
}
void GuardianScreen::clicked(lv_event_t* event) {
  auto* self = static_cast<GuardianScreen*>(lv_event_get_user_data(event));
  if (!self->_root.get() || !self->_command ||
      lv_obj_get_parent(lv_event_get_target(event)) != self->_root.get()) return;
  // Button action follows the last rendered state, including the timeout case.
  const bool enable = lv_event_get_target(event) == lv_obj_get_parent(self->_enable);
  guardian::Command command;
  if (enable) command = !strcmp(lv_label_get_text(self->_enable), TR("Disable")) ? guardian::Command::Disable : guardian::Command::Enable;
  else command = !strcmp(lv_label_get_text(self->_pair), TR("Cancel pairing")) ? guardian::Command::CancelPair : guardian::Command::Pair;
  self->_command(command);
}
} }

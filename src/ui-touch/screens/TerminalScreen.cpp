// SPDX-License-Identifier: GPL-3.0-or-later
#include "TerminalScreen.h"
#include "../services/TerminalSession.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"
#include "../i18n.h"
#include <cstring>
#include <cstdio>
namespace ui { namespace screens {
using namespace ui::theme;
using namespace ui::widgets;
struct AdminCmdEntry { const char* label; const char* command; };
static const char* k_term_banner =
  "WADAMESH terminal - CLI + chat on this node.\n"
  "Type a command or tap the list icon for the picker.\n"
  "Chat: 'to <name>' joins a contact/channel, then just\n"
  "      type to send. 'exit' leaves. list / channels.\n"
  "      All incoming msgs print live.\n"
  "Config: ver clock status get advert reboot\n"
  "        set <name|freq|bw|sf|cr|tx> <value>\n"
  "MeshCom: wifi status, tcp status, ble status,\n"
  "         mqtt status, ota status, tcp/ble on/off.\n";

// Quick-pick catalogue (mirrors the repeater admin picker). Commands here are
// the ones MyMesh::handleMeshcomodCommand understands natively when run
// locally. Tapping a row stuffs the template into the input; "set ..." rows
// carry a trailing space so the value can be typed straight after.
static const AdminCmdEntry k_term_cmds[] = {
  { "[ CHAT ]", nullptr },
  { "list - list contacts",           "list" },
  { "channels - list channels",       "channels" },
  { "to <name> - join contact/chan",  "to " },
  { "send <text> - msg recipient",    "send " },
  { "public <text> - public channel", "public " },
  { "exit - leave current chat",      "exit" },
  { "[ INFO ]", nullptr },
  { "help - command list",            "help" },
  { "ver - firmware version",         "ver" },
  { "clock - show RTC time",          "clock" },
  { "status - device status",         "status" },
  { "get - show radio params",        "get" },
  { "[ RADIO / MESH ]", nullptr },
  { "advert - flood advert",          "advert" },
  { "advert.zerohop - 0-hop advert",  "advert.zerohop" },
  { "set name <new>",                 "set name " },
  { "set freq <MHz>",                 "set freq " },
  { "set bw <kHz>",                   "set bw " },
  { "set sf <7-12>",                  "set sf " },
  { "set cr <5-8>",                   "set cr " },
  { "set tx <dBm>",                   "set tx " },
  { "[ CONNECTIVITY ]", nullptr },
  { "wifi status",                    "wifi status" },
  { "wifi on",                        "wifi on" },
  { "wifi off",                       "wifi off" },
  { "wifi scan",                      "wifi scan" },
  { "wifi set ssid <v>",              "wifi set ssid " },
  { "wifi set pwd <v>",               "wifi set pwd " },
  { "wifi apply - connect now",       "wifi apply" },
  { "wifi clear - forget creds",      "wifi clear" },
  { "mqtt status",                    "mqtt status" },
  { "tcp status",                     "tcp status" },
  { "tcp on",                         "tcp on" },
  { "tcp off",                        "tcp off" },
  { "ble status",                     "ble status" },
  { "ble on",                         "ble on" },
  { "ble off",                        "ble off" },
  { "ota status",                     "ota status" },
  { "ota start",                      "ota start" },
  { "ota url <https://...bin>",       "ota url " },
  { "ota netdiag - net check",        "ota netdiag" },
  { "[ SYSTEM ]", nullptr },
  { "reboot",                         "reboot" },
  { "bootloader - download mode",     "bootloader" },
};
constexpr int k_term_cmds_count = sizeof(k_term_cmds) / sizeof(k_term_cmds[0]);

void TerminalScreen::append(uint32_t color, const char* prefix, const char* text) {
  if (!s_term_log_box || !text) return;
  char buf[512];
  snprintf(buf, sizeof buf, "%s%s", prefix ? prefix : "", text);
  lv_obj_t* lbl = lv_label_create(s_term_log_box);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, lv_pct(100));
  if (_host.isEink()) {
    lv_obj_set_style_bg_color(lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_black(), LV_PART_MAIN);
  } else {
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), LV_PART_MAIN);
  }
  lv_obj_set_style_text_font(lbl, &font12(), LV_PART_MAIN);
  lv_label_set_text(lbl, buf);
  uint32_t n = lv_obj_get_child_cnt(s_term_log_box);
  while (n > TERM_MAX_LINES) { lv_obj_del(lv_obj_get_child(s_term_log_box, 0)); --n; }
  lv_obj_scroll_to_y(s_term_log_box, LV_COORD_MAX, LV_ANIM_OFF);
}

void TerminalScreen::closeTermCmdPicker() {
  if (s_term_picker_root) {
    auto* old = s_term_picker_root;
    s_term_picker_root = nullptr;
    detach(old);
    _host.closePopup(&old);
  }
}

void TerminalScreen::openTermCmdPicker() {
  closeTermCmdPicker();
  if (!_body) return;
  lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_term_picker_root = lv_obj_create(lv_layer_top());
  lv_obj_add_event_cb(s_term_picker_root, deleted, LV_EVENT_DELETE, this);
  lv_obj_remove_style_all(s_term_picker_root);
  lv_obj_set_size(s_term_picker_root, sw, sh - _host.statusHeight());
  lv_obj_set_pos(s_term_picker_root, 0, _host.statusHeight());
#if defined(HAS_TDECK_PRO)
  lv_obj_set_style_bg_color(s_term_picker_root, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_term_picker_root, LV_OPA_COVER, LV_PART_MAIN);
#else
  lv_obj_set_style_bg_color(s_term_picker_root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_term_picker_root, LV_OPA_70, LV_PART_MAIN);
#endif
  lv_obj_clear_flag(s_term_picker_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_term_picker_root, [](lv_event_t* e) {
    auto& self = *static_cast<TerminalScreen*>(lv_event_get_user_data(e));
    if (!self.accepts(e)) return;
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_indev_t* a = lv_indev_get_act();
    if (a) lv_indev_wait_release(a);
    self.closeTermCmdPicker();
  }, LV_EVENT_CLICKED, this);

  const int card_w = sw - 20;
  const int card_h = (sh - _host.statusHeight()) - 40;
  lv_obj_t* card = lv_obj_create(s_term_picker_root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
#if defined(HAS_TDECK_PRO)
  lv_obj_set_style_border_color(card, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
#else
  lv_obj_set_style_border_color(card, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
#endif
  lv_obj_set_style_pad_all(card, 6, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, TR("Commands"));
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &font14(), LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 4);

  addCloseXBadge(card, +[](lv_event_t* e) {
    auto& self = *static_cast<TerminalScreen*>(lv_event_get_user_data(e));
    if (!self.accepts(e)) return;
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    self.closeTermCmdPicker();
  }, this);

  lv_obj_t* list = lv_list_create(card);
  lv_obj_set_size(list, card_w - 12, card_h - 12 - 28);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 28);
  lv_obj_set_style_bg_color(list, lv_color_hex(colors().COLOR_BG), LV_PART_MAIN);
  lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);

  for (int i = 0; i < k_term_cmds_count; ++i) {
    const AdminCmdEntry& e = k_term_cmds[i];
    if (!e.command) {
      lv_obj_t* h = lv_list_add_text(list, e.label);
    #if defined(HAS_TDECK_PRO)
      lv_obj_set_style_text_color(h, lv_color_black(), LV_PART_MAIN);
      lv_obj_set_style_bg_color(h, lv_color_white(), LV_PART_MAIN);
    #else
      lv_obj_set_style_text_color(h, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
      lv_obj_set_style_bg_color(h, lv_color_hex(themeRole(0x0F1722, colors().COLOR_ACCENT_SURFACE)), LV_PART_MAIN);
    #endif
      lv_obj_set_style_text_font(h, &font12(), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(h, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_width(h, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_ver(h, 6, LV_PART_MAIN);
      lv_obj_set_style_pad_left(h, 10, LV_PART_MAIN);
      continue;
    }
    lv_obj_t* btn = lv_list_add_btn(list, nullptr, e.label);
    lv_obj_set_style_text_font(btn, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(btn,
  #if defined(HAS_TDECK_PRO)
                  lv_color_black(),
  #else
                  lv_color_hex(colors().COLOR_TEXT),
  #endif
                  LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn,
  #if defined(HAS_TDECK_PRO)
                  lv_color_white(),
  #else
                  lv_color_hex(colors().COLOR_PANEL),
  #endif
                  LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(colors().COLOR_CONTROL_PRESSED), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn,
  #if defined(HAS_TDECK_PRO)
                    lv_color_black(),
  #else
                    lv_color_hex(colors().COLOR_CONTROL_PRESSED),
  #endif
                    LV_PART_MAIN);
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_min_height(btn, 30, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_left(btn, 10, LV_PART_MAIN);
    lv_obj_set_user_data(btn, (void*)(intptr_t)i);
    lv_obj_add_event_cb(btn, [](lv_event_t* ev) {
    auto& self = *static_cast<TerminalScreen*>(lv_event_get_user_data(ev));
    if (!self.accepts(ev)) return;
      if (lv_event_get_code(ev) != LV_EVENT_CLICKED) return;
      intptr_t idx = (intptr_t)lv_obj_get_user_data(lv_event_get_target(ev));
      if (idx < 0 || idx >= k_term_cmds_count) return;
      const char* tpl = k_term_cmds[idx].command;
      if (!tpl || !self.s_term_input_ta) { self.closeTermCmdPicker(); return; }
      lv_textarea_set_text(self.s_term_input_ta, tpl);
      lv_textarea_set_cursor_pos(self.s_term_input_ta, LV_TEXTAREA_CURSOR_LAST);
      self.closeTermCmdPicker();
      // Keep the input focused so the operator can type a value / press Enter.
      self._host.bindKeyboard(self.s_term_input_ta);
    }, LV_EVENT_CLICKED, this);
  }
}

void TerminalScreen::buildTerminal(lv_obj_t* body) {
  close();
  if (!body) return;
  _body = body;
  lv_obj_add_event_cb(body, deleted, LV_EVENT_DELETE, this);
  const bool paper = _host.isEink();
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  if (paper) {
    lv_obj_t* shell = lv_obj_get_parent(body);
    if (shell) {
      lv_obj_set_style_bg_color(shell, lv_color_white(), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(shell, LV_OPA_COVER, LV_PART_MAIN);
    }
    lv_obj_set_style_bg_color(body, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, LV_PART_MAIN);
  }
  const lv_coord_t bw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t bh = (lv_disp_get_ver_res(nullptr) - _host.statusHeight());  // body fills the view
  const lv_coord_t row_h = 40;

  s_term_log_box = lv_obj_create(body);
  lv_obj_remove_style_all(s_term_log_box);
  lv_obj_set_size(s_term_log_box, bw - 8, bh - row_h - 4);
  lv_obj_set_pos(s_term_log_box, 4, 2);
  styleSurface(s_term_log_box, paper ? 0xFFFFFF : 0x0A0B0C, paper ? 0 : 6);
  lv_obj_set_style_bg_color(s_term_log_box, paper ? lv_color_white() : lv_color_hex(0x0A0B0C), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_term_log_box, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(s_term_log_box,
                                paper ? lv_color_black() : lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_term_log_box, paper ? 2 : 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_term_log_box, 6, LV_PART_MAIN);
  lv_obj_set_scroll_dir(s_term_log_box, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_term_log_box, LV_SCROLLBAR_MODE_AUTO);
  // Stack each log line as its own coloured label.
  lv_obj_set_flex_flow(s_term_log_box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_term_log_box, 1, LV_PART_MAIN);

  // Seed the banner (fresh each open) — must come AFTER the box exists.
  _host.log(TERM_C_BANNER, nullptr, k_term_banner);

  // Input row
  lv_obj_t* row = lv_obj_create(body);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, bw, row_h);
  lv_obj_set_pos(row, 0, bh - row_h);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  if (paper) {
    lv_obj_set_style_bg_color(row, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
  }

  lv_obj_t* picker_btn = lv_btn_create(row);
  lv_obj_set_size(picker_btn, 32, 32);
  lv_obj_align(picker_btn, LV_ALIGN_LEFT_MID, 4, 0);
  styleButton(picker_btn);
  lv_obj_set_style_bg_color(picker_btn, lv_color_hex(colors().COLOR_CONTROL), LV_PART_MAIN);
  lv_obj_set_style_pad_all(picker_btn, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(picker_btn, [](lv_event_t* e) {
    auto& self = *static_cast<TerminalScreen*>(lv_event_get_user_data(e));
    if (!self.accepts(e)) return;
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    self.openTermCmdPicker();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* picker_lbl = lv_label_create(picker_btn);
  lv_label_set_text(picker_lbl, LV_SYMBOL_LIST);
  lv_obj_set_style_text_font(picker_lbl, &font14(), LV_PART_MAIN);
  lv_obj_center(picker_lbl);

  s_term_input_ta = lv_textarea_create(row);
  lv_obj_set_size(s_term_input_ta, bw - 104, 32);
  lv_obj_align(s_term_input_ta, LV_ALIGN_LEFT_MID, 40, 0);
  styleCard(s_term_input_ta);
  lv_textarea_set_one_line(s_term_input_ta, true);
  lv_textarea_set_max_length(s_term_input_ta, 96);
  taSetPlaceholder(s_term_input_ta, TR("command"));
  lv_obj_set_style_text_color(s_term_input_ta, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_term_input_ta, &font14(), LV_PART_MAIN);
  if (paper) {
    lv_obj_set_style_bg_color(s_term_input_ta, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_term_input_ta, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_term_input_ta, lv_color_black(), LV_PART_MAIN);
  }
  _host.attachField(s_term_input_ta);

  lv_obj_t* send_btn = lv_btn_create(row);
  lv_obj_set_size(send_btn, 56, 32);
  lv_obj_align(send_btn, LV_ALIGN_RIGHT_MID, -4, 0);
  styleButton(send_btn);
#if defined(HAS_TDECK_PRO)
  lv_obj_set_style_bg_color(send_btn, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_color(send_btn, lv_color_black(), LV_PART_MAIN);
#else
  lv_obj_set_style_bg_color(send_btn, lv_color_hex(colors().COLOR_STATUS_OK), LV_PART_MAIN);
  lv_obj_set_style_text_color(send_btn, lv_color_hex(colors().COLOR_ON_STATUS_OK), LV_PART_MAIN);
#endif
  lv_obj_add_event_cb(send_btn, [](lv_event_t* e) {
    auto& self = *static_cast<TerminalScreen*>(lv_event_get_user_data(e));
    if (!self.accepts(e)) return;
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    self.terminalSubmit();
  }, LV_EVENT_CLICKED, this);
  lv_obj_t* send_lbl = lv_label_create(send_btn);
  useChainedFont(send_lbl);
  lv_label_set_text(send_lbl, LV_SYMBOL_RIGHT);
  lv_obj_center(send_lbl);

  // Capture replies + auto-focus the input so HW keys type immediately.
  _host.setSink(true);
  _host.bindKeyboard(s_term_input_ta);
}

void TerminalScreen::terminalSubmit() {
  if (!s_term_input_ta) return;
  _host.syncKeyboard();   // pull the latest text out of the keyboard mirror
  const char* text = lv_textarea_get_text(s_term_input_ta);
  if (!text || !text[0]) return;
  char cmd[128];
  strncpy(cmd, text, sizeof(cmd) - 1);
  cmd[sizeof(cmd) - 1] = '\0';
  _host.log(TERM_C_INPUT, "> ", cmd);
  _host.execute(cmd);
  if (!s_term_input_ta) return;  // A local command may close the tool view.
  lv_textarea_set_text(s_term_input_ta, "");
  // Re-bind so the cleared mirror tracks the field and the next Enter submits.
  _host.bindKeyboard(s_term_input_ta);
}
bool TerminalScreen::accepts(lv_event_t* event) const {
  for (auto* object = lv_event_get_target(event); object; object = lv_obj_get_parent(object))
    if (object == _body || object == s_term_picker_root) return true;
  return false;
}
void TerminalScreen::detach(lv_obj_t* object) {
  if (!object) return;
  while (lv_obj_remove_event_cb_with_user_data(object, nullptr, this)) {}
  for (uint32_t i=0; i<lv_obj_get_child_cnt(object); ++i) detach(lv_obj_get_child(object, i));
}
void TerminalScreen::deleted(lv_event_t* event) {
  auto& self = *static_cast<TerminalScreen*>(lv_event_get_user_data(event));
  auto* object = lv_event_get_target(event);
  if (object == self.s_term_picker_root) self.s_term_picker_root = nullptr;
  if (object == self._body) {
    self._body = nullptr;
    self.s_term_log_box = self.s_term_input_ta = nullptr;
    self._host.setSink(false);
    self._host.unbindKeyboard();
    self.closeTermCmdPicker();
  }
}
void TerminalScreen::close() {
  closeTermCmdPicker();
  if (!_body) return;
  _host.setSink(false);
  _host.unbindKeyboard();
  auto* old = _body;
  _body = s_term_log_box = s_term_input_ta = nullptr;
  detach(old);
  lv_obj_clean(old);
}
TerminalScreen::~TerminalScreen() { close(); }

} }

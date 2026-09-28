// SPDX-License-Identifier: GPL-3.0-or-later
#include "PingReplyDialog.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"
#include <cstdio>
#include <algorithm>
namespace ui { namespace screens {
using namespace theme;
void PingReplyDialog::detach(lv_obj_t* object) {
  lv_obj_remove_event_cb_with_user_data(object, closeEvent, this);
  lv_obj_remove_event_cb_with_user_data(object, backdropEvent, this);
  lv_obj_remove_event_cb_with_user_data(object, deleteEvent, this);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i) detach(lv_obj_get_child(object, i));
}
void PingReplyDialog::dismiss() {
  auto* old = _root;
  _root = nullptr;
  if (old) { detach(old); _host.closeRoot(&old); }
}
void PingReplyDialog::closeEvent(lv_event_t* e) {
  auto* self = static_cast<PingReplyDialog*>(lv_event_get_user_data(e));
  self->dismiss();
}
void PingReplyDialog::backdropEvent(lv_event_t* e) {
  auto* self = static_cast<PingReplyDialog*>(lv_event_get_user_data(e));
  if (lv_event_get_target(e) == self->_root) self->dismiss();
}
void PingReplyDialog::deleteEvent(lv_event_t* e) {
  auto* self = static_cast<PingReplyDialog*>(lv_event_get_user_data(e));
  if (lv_event_get_target(e) == self->_root) self->_root = nullptr;
}
void PingReplyDialog::show(const char* name, const PingStatus& s) {
  dismiss();
  const auto top = _host.contentTop();
  const lv_coord_t width = lv_disp_get_hor_res(nullptr);
  const lv_coord_t height = lv_disp_get_ver_res(nullptr) - top;
  _root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(_root);
  lv_obj_set_size(_root, width, height);
  lv_obj_set_pos(_root, 0, top);
  lv_obj_set_style_bg_color(_root, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(_root, LV_OPA_60, 0);
  lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(_root, backdropEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(_root, deleteEvent, LV_EVENT_DELETE, this);
  auto* card = lv_obj_create(_root);
  lv_obj_remove_style_all(card);
  const lv_coord_t cw = std::min<lv_coord_t>(width - 16, PSC(340));
  const lv_coord_t ch = std::min<lv_coord_t>(height - 8, PSC(200));
  lv_obj_set_size(card, cw, ch);
  lv_obj_center(card);
  widgets::styleSurface(card, colors().COLOR_PANEL, 12);
  lv_obj_set_style_border_color(card, lv_color_hex(colors().COLOR_BORDER), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  auto label = [](lv_obj_t* parent, const char* text, int x, int y, int w, const lv_font_t& font) {
    auto* result = lv_label_create(parent);
    lv_obj_set_style_text_font(result, &font, 0);
    lv_obj_set_style_text_color(result, lv_color_hex(colors().COLOR_TEXT), 0);
    lv_obj_set_pos(result, x, y);
    lv_obj_set_width(result, w);
    lv_label_set_long_mode(result, LV_LABEL_LONG_DOT);
    lv_label_set_text(result, text);
    return result;
  };
  auto* title = label(card, name && *name ? name : TR("Node"), 10, 9, cw - 50, font16());
  lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);
  const int gap = 6, pad = 9, rowTop = 38;
  const int cellWidth = (cw - pad * 2 - gap) / 2;
  const int cellHeight = (ch - rowTop - 30 - gap) / 2;
  auto cell = [&](int index, const char* icon, const char* caption, const char* value, const char* extra) {
    auto* box = lv_obj_create(card);
    lv_obj_remove_style_all(box);
    lv_obj_set_pos(box, pad + (index % 2) * (cellWidth + gap), rowTop + (index / 2) * (cellHeight + gap));
    lv_obj_set_size(box, cellWidth, cellHeight);
    widgets::styleSurface(box, colors().COLOR_BG, 6);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    label(box, icon, 7, 5, 19, font14());
    label(box, caption, 28, 5, cellWidth - 33, font12());
    label(box, value, 7, 24, cellWidth - 14, font16());
    if (extra) label(box, extra, 7, 44, cellWidth - 14, font14());
  };
  char battery[32] = "--", percent[24] = "", uptime[40] = "--", queue[24] = "--", rssi[24] = "--";
  if (s.hasBattery) snprintf(battery, sizeof battery, "%lu.%02lu V", (unsigned long)(s.batteryMv / 1000), (unsigned long)((s.batteryMv % 1000) / 10));
  const auto estimated = s.batteryPercent();
  // The UI fonts do not contain U+2248; ASCII ~ explicitly marks an estimate.
  if (estimated >= 0) snprintf(percent, sizeof percent, "~%d %%", estimated);
  if (s.hasUptime) {
    if (s.uptimeSecs >= 86400) snprintf(uptime, sizeof uptime, "%lud %luh", (unsigned long)(s.uptimeSecs / 86400), (unsigned long)((s.uptimeSecs % 86400) / 3600));
    else if (s.uptimeSecs >= 3600) snprintf(uptime, sizeof uptime, "%luh %02lum", (unsigned long)(s.uptimeSecs / 3600), (unsigned long)((s.uptimeSecs % 3600) / 60));
    else snprintf(uptime, sizeof uptime, "%lum %02lus", (unsigned long)(s.uptimeSecs / 60), (unsigned long)(s.uptimeSecs % 60));
  }
  if (s.hasQueue) snprintf(queue, sizeof queue, "%lu", (unsigned long)s.queueLength);
  if (s.hasRssi) snprintf(rssi, sizeof rssi, "%d dBm", int(s.rssi));
  cell(0, LV_SYMBOL_BATTERY_FULL, TR("Battery"), battery, estimated >= 0 ? percent : nullptr);
  cell(1, LV_SYMBOL_REFRESH, TR("Uptime"), uptime, nullptr);
  cell(2, LV_SYMBOL_LIST, TR("TX queue"), queue, nullptr);
  cell(3, LV_SYMBOL_WIFI, TR("Last RSSI"), rssi, nullptr);
  label(card, TR("Ping · battery estimate (3.3–4.2 V)"), 10, ch - 23, cw - 20, font12());
  auto* close = widgets::addCloseXBadge(card, closeEvent, this);
  if (_host.focus) _host.focus(close);
}
} }

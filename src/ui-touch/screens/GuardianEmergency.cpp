// SPDX-License-Identifier: GPL-3.0-or-later
#include "GuardianEmergency.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../widgets/Styles.h"
#include "../widgets/GuardianIcon.h"
#include <cstdio>
namespace ui { namespace screens {
namespace {
GuardianEmergency &view() { static GuardianEmergency value(guardian::v3::client()); return value; }
lv_obj_t *label(lv_obj_t *parent, const char *value, lv_coord_t width, bool small = false) {
  auto *result = lv_label_create(parent);
  lv_obj_set_width(result, width);
  lv_label_set_long_mode(result, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(result, small ? &theme::font12() : &theme::font16(), LV_PART_MAIN);
  lv_obj_set_style_text_color(result, lv_color_hex(0xf3f5ff), LV_PART_MAIN);
  lv_label_set_text(result, value);
  return result;
}
std::string signature(const guardian::v3::Alert &notice, const std::string &name) {
  return std::to_string(notice.code) + ":" + std::to_string(name.size()) + ":" + name + ":" +
    std::to_string(notice.source.size()) + ":" + notice.source + ":" +
    (notice.radio.known ? std::to_string(notice.radio.value) : "?") + ":" + notice.note;
}
}
void guardianEmergencyConfigure(GuardianEmergency::Host host) { view().configure(host); }
void guardianEmergencyTick(uint32_t now) { view().tick(now); }
bool guardianEmergencyVisible() { return view().visible(); }
GuardianEmergency::~GuardianEmergency() { close(); }
void GuardianEmergency::close() {
  auto *owned = _root.get();
  _root.set(nullptr); _error.set(nullptr);
  _id.clear(); _signature.clear(); _started = false;
  if (owned) lv_obj_del(owned);
}
void GuardianEmergency::show(const guardian::v3::Alert &notice) {
  // Replacing this overlay must never delete the system layer or another modal.
  const bool started = _started;
  close(); _started = started;
  const lv_coord_t width = lv_disp_get_hor_res(nullptr), height = lv_disp_get_ver_res(nullptr);
  auto *root = lv_obj_create(lv_layer_sys());
  if (!root) return;
  if (!_root.set(root)) { lv_obj_del(root); return; }
  lv_obj_remove_style_all(root);
  lv_obj_set_pos(root, 0, 0); lv_obj_set_size(root, width, height);
  lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  _id = notice.alert_id;
  const std::string name = _client.alertLabel(notice.code);
  _signature = signature(notice, name);
  auto *header = lv_obj_create(root);
  widgets::styleSurface(header, 0x091b27, 8);
  lv_obj_set_pos(header, 8, 8); lv_obj_set_size(header, width - 16, 72);
  lv_obj_set_style_border_width(header, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(header, lv_color_hex(0x00b9ec), LV_PART_MAIN);
  lv_obj_set_style_pad_all(header, 8, LV_PART_MAIN);
  lv_obj_clear_flag(header,LV_OBJ_FLAG_SCROLLABLE);
  auto* severity=widgets::guardianIcon(header,widgets::GuardianIcon::Warning,30,0xff5349,lv_color_hex(0x111a22));
  lv_obj_set_pos(severity,0,0);
  auto *title = label(header, name.c_str(), width - 74);
  lv_obj_set_pos(title,38,0); lv_obj_set_height(title,20); lv_label_set_long_mode(title,LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(title, lv_color_hex(0xf3f5ff), LV_PART_MAIN);
  const std::string source = std::string(TR("Source:")) + " " +
    (notice.source.empty() ? TR("Unknown") : notice.source);
  auto* sourceLabel=label(header, source.c_str(), width - 74, true);
  lv_obj_set_pos(sourceLabel,38,24); lv_obj_set_height(sourceLabel,14); lv_label_set_long_mode(sourceLabel,LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(sourceLabel,lv_color_hex(0xa8b8cb),LV_PART_MAIN);
  char radio[64];
  if (notice.radio.known) snprintf(radio, sizeof radio, TR("Radio %lu"), static_cast<unsigned long>(notice.radio.value));
  else snprintf(radio, sizeof radio, "%s", TR("Radio unknown"));
  auto* radioLabel=label(header, radio, width - 74, true); lv_obj_set_pos(radioLabel,38,42);
  lv_obj_set_style_text_color(radioLabel,lv_color_hex(0xa8b8cb),LV_PART_MAIN);
  lv_obj_update_layout(header);
  const lv_coord_t bodyY = 8 + lv_obj_get_height(header) + 6;
  auto *body = lv_obj_create(root);
  widgets::styleSurface(body, 0x0a1620, 8);
  lv_obj_set_style_border_width(body,1,LV_PART_MAIN);
  lv_obj_set_style_border_color(body,lv_color_hex(0x00b9ec),LV_PART_MAIN);
  lv_obj_set_pos(body, 8, bodyY); lv_obj_set_size(body, width - 16, height - bodyY - 88);
  lv_obj_set_style_pad_all(body, 9, LV_PART_MAIN);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
  // No ellipsis, byte clipping, or implicit read action: the full validated UTF-8
  // note remains scrollable until the explicit durable read succeeds.
  auto *note = label(body, notice.note.empty() ? TR("No message text") : notice.note.c_str(), width - 36);
  lv_obj_set_style_text_font(note, &theme::font14(), LV_PART_MAIN);
  auto *error = label(root, TR("Alarm repeats until marked as read."), width - 16, true); _error.set(error);
  lv_obj_set_pos(error, 8, height - 80); lv_obj_set_height(error, 32);
  lv_obj_set_style_text_color(error, lv_color_hex(0xa8b8cb), LV_PART_MAIN);
  auto *read = lv_btn_create(root);
  widgets::styleButton(read);
  lv_obj_set_pos(read, 8, height - 42); lv_obj_set_size(read, width - 16, 34);
  lv_obj_set_style_bg_color(read, lv_color_hex(0xf29d38), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(read, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(read, lv_color_hex(0xdb8522), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(read, lv_color_hex(0x1b1b1b), LV_PART_MAIN);
  lv_obj_add_event_cb(read, [](lv_event_t *event) {
    static_cast<GuardianEmergency *>(lv_event_get_user_data(event))->read();
  }, LV_EVENT_CLICKED, this);
  auto *caption = label(read, TR("Mark as read"), width - 32);
  lv_obj_set_style_text_font(caption, &theme::font14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(caption, lv_color_hex(0x1b1b1b), LV_PART_MAIN);
  lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_center(caption);
  auto* readIcon=lv_label_create(read); lv_label_set_text(readIcon,LV_SYMBOL_OK);
  lv_obj_set_style_text_font(readIcon,&theme::font16(),LV_PART_MAIN);
  lv_obj_set_style_text_color(readIcon,lv_color_hex(0x1b1b1b),LV_PART_MAIN);
  lv_obj_align(readIcon,LV_ALIGN_LEFT_MID,12,0);
}
void GuardianEmergency::read() {
  if (!_root.get() || _id.empty()) return;
  const std::string id = _id;
  if (!_client.seen(id)) {
    if (_error.get()) {
      lv_label_set_text(_error.get(), TR("Cannot save read state. Alarm continues."));
      lv_obj_set_style_text_color(_error.get(), lv_color_hex(0xffba64), LV_PART_MAIN);
    }
    return;
  }
  close();
  tick(_now); // The next oldest unseen emergency is shown immediately.
}
void GuardianEmergency::tick(uint32_t now) {
  _now = now;
  const auto *pending = _client.emergency();
  if (!pending) { if (_root.get() || !_id.empty()) close(); return; }
  const guardian::v3::Alert& notice = *pending;
  const bool first = !_root.get() || _id != notice.alert_id;
  if (first || (_observedRevision != _client.revision && _signature != signature(notice, _client.alertLabel(notice.code)))) show(notice);
  _observedRevision = _client.revision;
  const bool wake = first || !_started || uint32_t(now - _wakeAt) >= 1000;
  const bool beep = first || !_started || uint32_t(now - _beepAt) >= 3000;
  _started = true;
  if (wake) _wakeAt = now;
  if (beep) _beepAt = now;
  if (_host.keepAwake) _host.keepAwake();
  if (wake && _host.wake) _host.wake();
  if (beep && _host.beep) _host.beep();
  if (_root.get()) lv_obj_move_foreground(_root.get());
}
} }

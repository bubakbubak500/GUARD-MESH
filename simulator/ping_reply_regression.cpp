// SPDX-License-Identifier: GPL-3.0-or-later
#include "models/PingStatus.h"
#include "screens/PingReplyDialog.h"
#include "i18n.h"
#include <cstring>
#include <stdexcept>
namespace {
void check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
lv_obj_t* find(lv_obj_t* root, const char* text) {
  if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text)) return root;
  for (unsigned i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto* p = find(lv_obj_get_child(root, i), text)) return p;
  return nullptr;
}
void closeAsync(lv_obj_t** root) { lv_obj_del_async(*root); *root = nullptr; }
}
void runPingReplyRegression(void (*pump)(unsigned), void (*capture)(const char*)) {
  uint8_t raw[56]{};
  raw[0] = 0x7c; raw[1] = 0x10; // 4220 mV
  raw[2] = 3;
  raw[6] = 0xa5; raw[7] = 0xff; // -91 dBm
  raw[16] = 17; // air time must not become uptime
  raw[20] = 0x10; raw[21] = 0x0e; // uptime 3600
  auto s = ui::PingStatus::parse(raw, sizeof raw);
  check(s.hasBattery && s.batteryMv == 4220 && s.batteryPercent() == 100, "Ping full battery estimate");
  check(s.hasUptime && s.uptimeSecs == 3600 && s.queueLength == 3 && s.rssi == -91, "RepeaterStats offsets");
  s.batteryMv = 4020; check(s.batteryPercent() == 80, "Ping 4.02 V estimate");
  s.batteryMv = 3200; check(s.batteryPercent() == 0, "Ping low estimate clamp");
  s.batteryMv = 8400; check(s.batteryPercent() == -1, "Do not estimate another voltage range");
  s = ui::PingStatus::parse(raw, 20); check(!s.hasUptime && !s.hasBattery, "Truncated binary Ping accepted");
  s = ui::PingStatus::parse(nullptr, 20); check(!s.hasQueue, "Null Ping payload");
  auto json = [](const char* body) { return ui::PingStatus::parse(reinterpret_cast<const uint8_t*>(body), strlen(body)); };
  auto partial = json("{\n \"battery_mv\":4020, \"uptime_secs\":0, \"queue_len\":0}");
  check(partial.batteryPercent() == 80 && partial.hasUptime && partial.uptimeSecs == 0 && partial.hasQueue && !partial.hasRssi, "JSON Ping zero/missing fields");
  s = json("{\"battery_mv\":-1,\"uptime_secs\":\"bad\",\"queue_len\":null}");
  check(!s.hasBattery && !s.hasUptime && !s.hasQueue, "Invalid JSON Ping types");
  s = json("{\"battery_mv\":4220"); check(!s.hasBattery, "Truncated JSON Ping");
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  ui::screens::PingReplyDialog dialog({[]{ return lv_coord_t(22); }, closeAsync, nullptr});
  dialog.show("Heltec V4 · OK7PS", ui::PingStatus::parse(raw, sizeof raw));
  pump(30);
  auto* title = find(lv_layer_top(), "Heltec V4 · OK7PS");
  check(title && find(lv_layer_top(), "4.22 V") && find(lv_layer_top(), "~100 %") &&
        find(lv_layer_top(), "1h 00m") && find(lv_layer_top(), "-91 dBm"), "Ping modal values missing");
  auto* card = lv_obj_get_parent(title);
  lv_obj_update_layout(card);
  lv_area_t bounds; lv_obj_get_coords(card, &bounds);
  check(bounds.x1 >= 0 && bounds.x2 < lv_disp_get_hor_res(nullptr) && bounds.y1 >= 22 &&
        bounds.y2 < lv_disp_get_ver_res(nullptr), "Ping modal exceeds display");
  if (capture) capture("ping-reply.png");
  auto* oldClose = lv_obj_get_parent(find(card, LV_SYMBOL_CLOSE));
  dialog.show("Room server", partial);
  lv_event_send(oldClose, LV_EVENT_CLICKED, nullptr);
  check(dialog.isOpen(), "Old Ping close event dismissed replacement");
  pump(30);
  check(find(lv_layer_top(), "~80 %") && find(lv_layer_top(), "--") && !find(lv_layer_top(), "0 dBm"), "Missing RSSI must stay unknown");
  if (capture) capture("ping-reply-partial.png");
  title = find(lv_layer_top(), "Room server");
  card = lv_obj_get_parent(title);
  lv_event_send(card, LV_EVENT_CLICKED, nullptr);
  check(dialog.isOpen(), "Click inside Ping modal dismissed it");
  lv_event_send(lv_obj_get_parent(card), LV_EVENT_CLICKED, nullptr);
  check(!dialog.isOpen(), "Ping backdrop did not dismiss");
  pump(30);
  lv_obj_t* stale;
  {
    ui::screens::PingReplyDialog transient({[]{ return lv_coord_t(22); }, closeAsync, nullptr});
    transient.show("Temporary", partial);
    stale = lv_obj_get_parent(find(lv_layer_top(), LV_SYMBOL_CLOSE));
  }
  lv_event_send(stale, LV_EVENT_CLICKED, nullptr);
  pump(30);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Ping modal leaked its tree");
}

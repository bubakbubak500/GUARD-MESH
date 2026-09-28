// SPDX-License-Identifier: GPL-3.0-or-later
#include "PingStatus.h"
#include "BatteryModel.h"
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 0
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_PROGMEM 0
#include <ArduinoJson.h>
namespace ui {
namespace {
uint16_t read16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t read32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
bool space(uint8_t c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }
}
PingStatus PingStatus::parse(const uint8_t* data, size_t len) {
  PingStatus result;
  if (!data || !len) return result;
  // Identify JSON before binary: a JSON object beginning with "{\n" has a
  // plausible battery voltage if its first two bytes are mistaken for u16.
  size_t start = 0;
  while (start < len && space(data[start])) ++start;
  size_t next = start + 1;
  while (next < len && space(data[next])) ++next;
  const bool json = start < len && data[start] == '{' && next < len &&
                    (data[next] == '"' || data[next] == '}');
  if (json) {
    if (len > 1024) return result;
    JsonDocument doc;
    if (deserializeJson(doc, data, len) || !doc.is<JsonObject>()) return result;
    result.hasBattery = doc["battery_mv"].is<uint32_t>() && doc["battery_mv"].as<uint32_t>() > 0;
    result.hasUptime = doc["uptime_secs"].is<uint32_t>();
    result.hasQueue = doc["queue_len"].is<uint32_t>();
    if (result.hasBattery) result.batteryMv = doc["battery_mv"].as<uint32_t>();
    if (result.hasUptime) result.uptimeSecs = doc["uptime_secs"].as<uint32_t>();
    if (result.hasQueue) result.queueLength = doc["queue_len"].as<uint32_t>();
    // StatsFormatHelper JSON does not carry RSSI.
  } else if (len >= 24) {
    const auto mv = read16(data);
    if (mv != 0 && (mv < 2000 || mv > 5500)) return result;
    result.batteryMv = mv;
    result.hasBattery = mv != 0;
    result.queueLength = read16(data + 2);
    result.rssi = static_cast<int16_t>(read16(data + 6));
    // RepeaterStats: offset 16 is TX airtime; uptime starts at offset 20.
    result.uptimeSecs = read32(data + 20);
    result.hasQueue = result.hasRssi = result.hasUptime = true;
  }
  return result;
}
int PingStatus::batteryPercent() const {
  if (!hasBattery || batteryMv < 2000 || batteryMv > 5500) return -1;
  return BatteryModel{}.percent(static_cast<uint16_t>(batteryMv));
}
}

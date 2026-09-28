// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The wire model uses std::string on the device and in the desktop simulator.
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 0
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_PROGMEM 0
#include <ArduinoJson.h>
#include <string>
namespace guardian {
inline bool utf8(const std::string& s, size_t* count = nullptr) {
  size_t chars = 0;
  for (size_t i = 0; i < s.size(); ++chars) {
    const uint8_t c = uint8_t(s[i++]);
    if (!c) return false;
    if (c < 128) continue;
    int n; uint32_t cp, min;
    if (c >= 0xc2 && c <= 0xdf) { n = 1; cp = c & 31; min = 128; }
    else if (c >= 0xe0 && c <= 0xef) { n = 2; cp = c & 15; min = 2048; }
    else if (c >= 0xf0 && c <= 0xf4) { n = 3; cp = c & 7; min = 65536; }
    else return false;
    while (n--) {
      if (i >= s.size() || (uint8_t(s[i]) & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (uint8_t(s[i++]) & 63);
    }
    if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
  }
  if (count) *count = chars;
  return true;
}
inline bool parseObject(JsonDocument& doc, const std::string& raw) {
  if (raw.empty() || raw.size() > 32768 || raw.front() != '{' || raw.back() != '}' || !utf8(raw)) return false;
  // ArduinoJson stops at the first object; reject extra roots/trailing bytes.
  int depth = 0; bool quoted = false, escape = false;
  for (size_t i = 0; i < raw.size(); ++i) {
    const char c = raw[i];
    if (quoted) { if (escape) escape = false; else if (c == '\\') escape = true; else if (c == '"') quoted = false; }
    else if (c == '"') quoted = true;
    else if (c == '{' || c == '[') ++depth;
    else if (c == '}' || c == ']') { if (--depth == 0 && i + 1 != raw.size()) return false; }
  }
  return !quoted && depth == 0 && !deserializeJson(doc, raw) && doc.is<JsonObject>();
}
inline std::string jsonText(const JsonDocument& doc) { std::string s; serializeJson(doc, s); return s; }
}

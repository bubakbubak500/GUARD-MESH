// SPDX-License-Identifier: GPL-3.0-or-later
#include "GuardianDraft.h"
#include "GuardianJson.h"
#include <cstdio>
#if defined(ESP32) && !defined(GUARD_SIMULATOR)
#include <Preferences.h>
#include <esp_system.h>
#else
#include <random>
namespace { std::string stored; }
#endif
namespace guardian {
bool validateDraft(const Draft& d) {
  if (d.to.empty() || d.to.size() > 16 || d.priority > 3) return false;
  for (char c : d.to) if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
      (c >= '0' && c <= '9') || c == '/' || c == '-')) return false;
  size_t n;
  if (!utf8(d.subject, &n) || n > 256 || !utf8(d.body, &n) || n == 0 || n > 4096) return false;
  return d.body.find_first_not_of(" \r\n\t\v\f") != std::string::npos;
}
bool saveDraft(const Draft& d) {
  JsonDocument doc;
  doc["to"] = d.to; doc["subject"] = d.subject; doc["body"] = d.body;
  doc["token"] = d.token; doc["priority"] = d.priority;
  const std::string raw = jsonText(doc);
  if (raw.size() > 32768) return false;
#if defined(ESP32) && !defined(GUARD_SIMULATOR)
  Preferences prefs;
  if (!prefs.begin("guardian-draft", false)) return false;
  // A single NVS blob is committed atomically. Never notify before it succeeds.
  const bool ok = prefs.putBytes("draft", raw.data(), raw.size()) == raw.size();
  prefs.end(); return ok;
#else
  stored = raw; return true;
#endif
}
bool loadDraft(Draft& d) {
  std::string raw;
#if defined(ESP32) && !defined(GUARD_SIMULATOR)
  Preferences prefs;
  if (!prefs.begin("guardian-draft", true)) return false;
  const size_t n = prefs.getBytesLength("draft");
  if (!n || n > 32768) { prefs.end(); return false; }
  raw.resize(n);
  const bool ok = prefs.getBytes("draft", &raw[0], n) == n;
  prefs.end(); if (!ok) return false;
#else
  raw = stored;
#endif
  JsonDocument doc;
  if (!parseObject(doc, raw)) return false;
  d.to = doc["to"] | ""; d.subject = doc["subject"] | ""; d.body = doc["body"] | "";
  d.token = doc["token"] | ""; d.priority = doc["priority"] | 0u;
  return true;
}
std::string newToken() {
  uint8_t bytes[16];
#if defined(ESP32) && !defined(GUARD_SIMULATOR)
  esp_fill_random(bytes, sizeof bytes);
#else
  std::random_device source; for (auto& b : bytes) b = uint8_t(source());
#endif
  bytes[6] = (bytes[6] & 15) | 0x40; bytes[8] = (bytes[8] & 63) | 0x80;
  char s[37]; snprintf(s, sizeof s,
    "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
    bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5],bytes[6],bytes[7],
    bytes[8],bytes[9],bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]);
  return s;
}
}

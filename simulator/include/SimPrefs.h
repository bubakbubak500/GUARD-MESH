// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Preferences-compatible host store for the unchanged TouchPrefsStore.cpp.
// Namespaces share one versioned, atomically replaced desktop file.
#include "Arduino.h"
#include <map>
#include <vector>
#include <fstream>
#include <iomanip>
#include <filesystem>
bool simReplaceFile(const char *source, const char *destination);
class SdNvsPrefs {
  std::string ns;
  inline static std::map<std::string, std::vector<uint8_t>> data;
  inline static std::string filename;
  inline static bool dirty = false;
  std::string key(const char *k) const { return ns + ":" + k; }
  template <class T> T get(const char *k, T def) {
    auto i = data.find(key(k));
    if (i == data.end() || i->second.size() != sizeof(T))
      return def;
    T v;
    memcpy(&v, i->second.data(), sizeof v);
    return v;
  }
  template <class T> size_t put(const char *k, T v) { return putBytes(k, &v, sizeof v); }

public:
  static void setFile(const char *path) {
    filename = path;
    dirty = true;
  }
  static void load(const char *path) {
    filename = path;
    data.clear();
    dirty = false;
    if (filename.empty())
      return;
    std::ifstream in(filename);
    std::string k, hex, magic;
    std::getline(in, magic);
    if (magic != "GUARD-SIM-PREFS-1")
      return;
    while (in >> std::quoted(k) >> hex) {
      if (k.size() > 96 || hex.size() > 1024 * 1024 || hex.size() % 2)
        continue;
      std::vector<uint8_t> v;
      bool valid = true;
      for (size_t i = 0; i < hex.size(); i += 2) {
        auto digit = [](char c) -> int {
          return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        };
        int a = digit(hex[i]), b = digit(hex[i + 1]);
        if (a < 0 || b < 0) {
          valid = false;
          break;
        }
        v.push_back((uint8_t)(a * 16 + b));
      }
      if (valid)
        data[k] = std::move(v);
    }
  }
  bool begin(const char *n, bool = false) {
    ns = n;
    return true;
  }
  void end() {}
  bool isKey(const char *k) { return data.count(key(k)); }
  bool remove(const char *k) {
    bool erased = data.erase(key(k));
    dirty |= erased;
    return erased;
  }
  bool clear() {
    for (auto i = data.begin(); i != data.end();) {
      if (i->first.rfind(ns + ":", 0) == 0)
        i = data.erase(i);
      else
        ++i;
    }
    dirty = true;
    return true;
  }
  static bool fileMode() { return false; }
  static bool ioBusy() { return false; }
  static bool busy() { return false; }
  template <class... A> static bool readFileBool(A...) { return false; }
  template <class... A> static bool writeFileBool(A...) { return false; }
  static bool flush(uint32_t = 0) {
    if (!dirty || filename.empty())
      return true;
    std::filesystem::create_directories(std::filesystem::path(filename).parent_path());
    std::string tmp = filename + ".tmp";
    std::ofstream out(tmp, std::ios::trunc);
    out << "GUARD-SIM-PREFS-1\n";
    const char *digits = "0123456789abcdef";
    for (auto &[k, v] : data) {
      if (v.empty())
        continue;
      out << std::quoted(k) << ' ';
      for (uint8_t b : v)
        out << digits[b >> 4] << digits[b & 15];
      out << '\n';
    }
    out.close();
    if (!out || !simReplaceFile(tmp.c_str(), filename.c_str()))
      return false;
    dirty = false;
    return true;
  }
  static void tick(uint32_t now) {
    static uint32_t last = 0;
    if (now - last >= 1000) {
      last = now;
      flush();
    }
  }
  uint8_t getUChar(const char *k, uint8_t d = 0) { return get(k, d); }
  int8_t getChar(const char *k, int8_t d = 0) { return get(k, d); }
  uint16_t getUShort(const char *k, uint16_t d = 0) { return get(k, d); }
  uint32_t getUInt(const char *k, uint32_t d = 0) { return get(k, d); }
  bool getBool(const char *k, bool d = false) { return get(k, d); }
  size_t putUChar(const char *k, uint8_t v) { return put(k, v); }
  size_t putChar(const char *k, int8_t v) { return put(k, v); }
  size_t putUShort(const char *k, uint16_t v) { return put(k, v); }
  size_t putUInt(const char *k, uint32_t v) { return put(k, v); }
  size_t putBool(const char *k, bool v) { return put(k, v); }
  String getString(const char *k, const String &d = String()) {
    auto i = data.find(key(k));
    if (i == data.end())
      return d;
    auto end = std::find(i->second.begin(), i->second.end(), 0);
    return String(std::string(i->second.begin(), end));
  }
  size_t getString(const char *k, char *b, size_t n) { return strlcpy(b, getString(k).c_str(), n); }
  size_t putString(const char *k, const char *s) { return putBytes(k, s, strlen(s) + 1); }
  size_t putString(const char *k, const String &s) { return putString(k, s.c_str()); }
  size_t getBytes(const char *k, void *b, size_t n) {
    auto i = data.find(key(k));
    if (i == data.end())
      return 0;
    auto sz = std::min(n, i->second.size());
    memcpy(b, i->second.data(), sz);
    return sz;
  }
  size_t getBytesLength(const char *k) {
    auto i = data.find(key(k));
    return i == data.end() ? 0 : i->second.size();
  }
  size_t putBytes(const char *k, const void *b, size_t n) {
    auto p = (const uint8_t *)b;
    data[key(k)] = {p, p + n};
    dirty = true;
    return n;
  }
};
using Preferences = SdNvsPrefs;

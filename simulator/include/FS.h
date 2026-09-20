// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// No emulated SD/SPIFFS in this target. Opening a firmware path always fails;
// simulator preferences and screenshots use separate desktop storage.
#include "Arduino.h"
#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"
namespace fs {
class File {
public:
  operator bool() const { return false; }
  size_t read(uint8_t *, size_t) { return 0; }
  int read() { return -1; }
  size_t readBytes(char *, size_t) { return 0; }
  String readStringUntil(char) { return {}; }
  template <class T> size_t print(const T &) { return 0; }
  void rewindDirectory() {}
  time_t getLastWrite() { return 0; }
  String getNextFileName() { return {}; }
  size_t write(const uint8_t *, size_t) { return 0; }
  size_t write(uint8_t) { return 0; }
  bool seek(size_t, int = 0) { return false; }
  size_t size() const { return 0; }
  size_t position() const { return 0; }
  int available() const { return 0; }
  bool isDirectory() const { return false; }
  const char *name() const { return ""; }
  const char *path() const { return ""; }
  void close() {}
  void flush() {}
  File openNextFile(const char * = "r") { return {}; }
  template <class... A> int printf(const char *, A...) { return 0; }
};
class FS {
public:
  File open(const char *, const char * = "r", bool = false) { return {}; }
  bool exists(const char *) { return false; }
  bool remove(const char *) { return false; }
  bool mkdir(const char *) { return false; }
  bool rmdir(const char *) { return false; }
  bool rename(const char *, const char *) { return false; }
  uint64_t totalBytes() { return 0; }
  uint64_t usedBytes() { return 0; }
  uint64_t cardSize() { return 0; }
  uint8_t cardType() { return 0; }
  template <class... A> bool begin(A...) { return false; }
  void end() {}
};
} // namespace fs
using fs::File;
inline fs::FS SPIFFS, SD;
#define CARD_NONE 0

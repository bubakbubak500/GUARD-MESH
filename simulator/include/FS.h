// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Opt-in in-memory Arduino FS for service regression tests. The device globals
// remain unmounted by default, so normal simulation never pretends to have SD.
#include "Arduino.h"
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include <limits>
#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"
namespace fs {
struct MemoryDisk {
  std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> files;
  std::set<std::string> directories{"/"};
  size_t writeBudget = std::numeric_limits<size_t>::max();
  bool failRename = false;
  size_t renameCalls = 0, failRenameAt = 0;
  bool persistRenameFailure = false;
  size_t readBudget = std::numeric_limits<size_t>::max();
};
class File {
  struct Handle {
    std::shared_ptr<MemoryDisk> disk;
    std::shared_ptr<std::vector<uint8_t>> bytes;
    std::vector<std::string> entries;
    std::string path;
    size_t position = 0, next = 0;
    bool writable = false, directory = false;
  };
  std::shared_ptr<Handle> _handle;
  friend class FS;
public:
  operator bool() const { return bool(_handle); }
  size_t read(uint8_t* out, size_t count) {
    if (!_handle || !_handle->bytes || !out) return 0;
    auto& h = *_handle;
    count = std::min(count, h.bytes->size() - std::min(h.position, h.bytes->size()));
    count = std::min(count, h.disk->readBudget);
    if (h.disk->readBudget != std::numeric_limits<size_t>::max()) h.disk->readBudget -= count;
    if (count) memcpy(out, h.bytes->data() + h.position, count);
    h.position += count;
    return count;
  }
  int read() { uint8_t b; return read(&b, 1) == 1 ? b : -1; }
  size_t readBytes(char* out, size_t count) { return read(reinterpret_cast<uint8_t*>(out), count); }
  String readStringUntil(char delimiter) {
    std::string text;
    for (int c; (c = read()) >= 0 && c != delimiter;) text.push_back(char(c));
    return String(text);
  }
  size_t print(char value) { return write(static_cast<uint8_t>(value)); }
  template<class T> size_t print(const T& value) { String text(value); return write(reinterpret_cast<const uint8_t*>(text.c_str()), text.length()); }
  void rewindDirectory() { if (_handle) _handle->next = 0; }
  time_t getLastWrite() { return 0; }
  String getNextFileName() {
    if (!_handle || !_handle->directory || _handle->next == _handle->entries.size()) return {};
    return String(_handle->entries[_handle->next++]);
  }
  size_t write(const uint8_t* bytes, size_t count) {
    if (!_handle || !_handle->bytes || !_handle->writable || !bytes) return 0;
    auto& h = *_handle;
    count = std::min(count, h.disk->writeBudget);
    if (h.disk->writeBudget != std::numeric_limits<size_t>::max()) h.disk->writeBudget -= count;
    if (h.position + count > h.bytes->size()) h.bytes->resize(h.position + count);
    if (count) memcpy(h.bytes->data() + h.position, bytes, count);
    h.position += count;
    return count;
  }
  size_t write(uint8_t byte) { return write(&byte, 1); }
  bool seek(size_t position, int = 0) {
    if (!_handle || !_handle->bytes) return false;
    _handle->position = position; return true;
  }
  size_t size() const { return _handle && _handle->bytes ? _handle->bytes->size() : 0; }
  size_t position() const { return _handle ? _handle->position : 0; }
  int available() const { return int(size() - std::min(size(), position())); }
  bool isDirectory() const { return _handle && _handle->directory; }
  const char* name() const { const char* p = path(); const char* slash = strrchr(p, '/'); return slash ? slash + 1 : p; }
  const char* path() const { return _handle ? _handle->path.c_str() : ""; }
  void close() { _handle.reset(); }
  void flush() {}
  File openNextFile(const char* = "r");
  template<class... A> int printf(const char* format, A... args) {
    const int length = snprintf(nullptr, 0, format, args...);
    if (length < 0) return 0;
    std::vector<char> buffer(size_t(length) + 1);
    snprintf(buffer.data(), buffer.size(), format, args...);
    return int(write(reinterpret_cast<const uint8_t*>(buffer.data()), size_t(length)));
  }
};
class FS {
  std::shared_ptr<MemoryDisk> _disk;
  friend class File;
  static bool valid(const char* path) { return path && path[0] == '/' && !strstr(path, ".."); }
public:
  void enableMemory() { _disk = std::make_shared<MemoryDisk>(); }
  void limitWrites(size_t bytes) { if (_disk) _disk->writeBudget = bytes; }
  void allowWrites() { limitWrites(std::numeric_limits<size_t>::max()); }
  void limitReads(size_t bytes) { if (_disk) _disk->readBudget = bytes; }
  void allowReads() { limitReads(std::numeric_limits<size_t>::max()); }
  void failRenames(bool fail) { if (_disk) _disk->failRename = fail; }
  void failRenameOnCall(size_t ordinal, bool persist = false) {
    if (_disk) { _disk->renameCalls=0; _disk->failRenameAt=ordinal; _disk->persistRenameFailure=persist; }
  }
  File open(const char* path, const char* mode = "r", bool = false) {
    File result;
    if (!_disk || !valid(path) || !mode) return result;
    const std::string key(path);
    const bool directory = _disk->directories.count(key) != 0;
    const bool writable = mode[0] == 'w' || mode[0] == 'a';
    if (!directory && !writable && !_disk->files.count(key)) return result;
    auto handle = std::make_shared<File::Handle>();
    handle->disk = _disk; handle->path = key; handle->directory = directory; handle->writable = writable;
    if (directory) {
      const std::string prefix = key == "/" ? key : key + "/";
      for (const auto& item : _disk->files)
        if (item.first.compare(0, prefix.size(), prefix) == 0 && item.first.find('/', prefix.size()) == std::string::npos) handle->entries.push_back(item.first);
      for (const auto& item : _disk->directories)
        if (item != key && item.compare(0, prefix.size(), prefix) == 0 && item.find('/', prefix.size()) == std::string::npos) handle->entries.push_back(item);
    } else {
      auto& bytes = _disk->files[key];
      if (!bytes) bytes = std::make_shared<std::vector<uint8_t>>();
      if (mode[0] == 'w') bytes->clear();
      handle->bytes = bytes;
      if (mode[0] == 'a') handle->position = bytes->size();
    }
    result._handle = std::move(handle);
    return result;
  }
  bool exists(const char* path) { return _disk && valid(path) && (_disk->files.count(path) || _disk->directories.count(path)); }
  bool remove(const char* path) { return _disk && valid(path) && _disk->files.erase(path); }
  bool mkdir(const char* path) { return _disk && valid(path) && (_disk->directories.insert(path), true); }
  bool rmdir(const char* path) { return _disk && valid(path) && _disk->directories.erase(path); }
  bool rename(const char* from, const char* to) {
    if (_disk) ++_disk->renameCalls;
    if (_disk && _disk->failRenameAt &&
        (_disk->renameCalls==_disk->failRenameAt ||
         (_disk->persistRenameFailure && _disk->renameCalls>_disk->failRenameAt))) return false;
    if (!_disk || !valid(from) || !valid(to) || _disk->failRename || !_disk->files.count(from) || exists(to)) return false;
    _disk->files[to] = _disk->files[from]; _disk->files.erase(from); return true;
  }
  uint64_t totalBytes() { return _disk ? 64 * 1024 * 1024 : 0; }
  uint64_t usedBytes() { uint64_t bytes = 0; if (_disk) for (const auto& item : _disk->files) bytes += item.second->size(); return bytes; }
  uint64_t cardSize() { return totalBytes(); }
  uint8_t cardType() { return _disk ? 1 : 0; }
  template<class... A> bool begin(A...) { return bool(_disk); }
  void end() {} // Disk survives an unmount/reopen, like physical storage.
};
inline File File::openNextFile(const char* mode) {
  auto path = getNextFileName(); if (!path.length()) return {};
  FS filesystem; filesystem._disk = _handle->disk; return filesystem.open(path.c_str(), mode);
}
}
using fs::File;
inline fs::FS SPIFFS, SD;
#define CARD_NONE 0

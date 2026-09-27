// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace fs { class FS; }
namespace ui {
// Synchronous storage operations. The caller chooses the UI/worker scheduling
// and provides DMA-capable scratch allocation plus its watchdog/activity hook.
class FileOperations {
public:
  struct Host { void* (*allocate)(size_t); void (*release)(void*); void (*progress)(); };
  explicit FileOperations(Host host) : _host(host) {}
  ~FileOperations();
  FileOperations(const FileOperations&) = delete;
  FileOperations& operator=(const FileOperations&) = delete;
  bool copy(fs::FS& source, const char* from, fs::FS& destination, const char* to, bool directory);
  bool move(fs::FS& source, const char* from, fs::FS& destination, const char* to, bool directory);
  bool remove(fs::FS&, const char* path);
private:
  Host _host;
  uint8_t* _buffer = nullptr;
  bool copyFile(fs::FS&, const char*, fs::FS&, const char*);
  bool copyTree(fs::FS&, const char*, fs::FS&, const char*, bool, unsigned depth);
  bool removeTree(fs::FS&, const char*, unsigned depth);
};
}

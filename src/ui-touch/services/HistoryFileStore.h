// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdio>
#include <cstring>
#include <utility>
namespace ui { namespace history {
// Borrowed, already mounted filesystem/root. Mount recovery and scheduling
// stay with the coordinator. Arduino, desktop and test files use the same code.
template<class Filesystem> class FileStore {
public:
  using File = decltype(std::declval<Filesystem&>().open("", "r"));
  FileStore(Filesystem& filesystem, const char* root) : _fs(filesystem), _root(root ? root : "") {}
  File open(const char* name, const char* mode) {
    char path[80];
    return mode && makePath(name, path) ? _fs.open(path, mode) : File();
  }
  void remove(const char* name) {
    char path[80];
    if (makePath(name, path)) _fs.remove(path);
  }
  bool replace(const char* final_name, const char* temporary_name) {
    char final_path[80], temporary_path[80];
    if (!makePath(final_name, final_path) || !makePath(temporary_name, temporary_path)) return false;
    // Preserve the Arduino FS contract (FAT cannot overwrite a destination).
    // This is NOT atomic across power loss between remove and rename.
    if (!_fs.exists(temporary_path)) return false;
    _fs.remove(final_path);
    return _fs.rename(temporary_path, final_path);
  }
  void ensureDirectory(const char* name) {
    char path[80];
    if (!makePath(name, path)) return;
    if (*_root) _fs.mkdir(_root);
    _fs.mkdir(path); // SPIFFS has flat names; mkdir failure there is expected.
  }
private:
  bool makePath(const char* name, char (&path)[80]) const {
    if (!name) return false;
    const int size = snprintf(path, sizeof path, "%s%s", _root, name);
    return size >= 0 && static_cast<size_t>(size) < sizeof path;
  }
  Filesystem& _fs;
  const char* _root;
};

// Prefix-read older records, zero new fields and skip future extensions.
// Short reads and failed seeks must reach the caller, never look like success.
template<class File> bool readRecord(File& file, void* destination, size_t current_size, size_t disk_size) {
  if (!destination || !current_size || !disk_size) return false;
  memset(destination, 0, current_size);
  const size_t take = disk_size < current_size ? disk_size : current_size;
  if (file.readBytes(static_cast<char*>(destination), take) != take) return false;
  return disk_size <= current_size || file.seek(file.position() + (disk_size - current_size));
}
} }

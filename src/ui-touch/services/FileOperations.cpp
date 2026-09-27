// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileOperations.h"
#include <FS.h>
#include <cstdio>
#include <cstring>
namespace ui {
namespace {
constexpr size_t chunkSize = 2048;
constexpr unsigned maxDepth = 32;
bool validPath(const char* path) {
  if (!path || *path != '/') return false;
  if (!path[1]) return true;
  for (const char* part = path + 1; *part;) {
    const char* end = std::strchr(part, '/');
    const size_t size = end ? size_t(end - part) : std::strlen(part);
    if (!size || (size == 1 && *part == '.') || (size == 2 && part[0] == '.' && part[1] == '.')) return false;
    if (!end) break;
    part = end + 1;
    if (!*part) return false;
  }
  return true;
}
bool childPath(char* out, size_t size, const char* parent, const char* name) {
  const char* base = std::strrchr(name, '/'); base = base ? base + 1 : name;
  if (!*base || !std::strcmp(base, ".") || !std::strcmp(base, "..")) return false;
  const int written = std::snprintf(out, size, "%s%s%s", parent, std::strcmp(parent, "/") ? "/" : "", base);
  return written > 0 && size_t(written) < size;
}
}
FileOperations::~FileOperations() { if (_buffer && _host.release) _host.release(_buffer); }
bool FileOperations::move(fs::FS& source, const char* from, fs::FS& destination, const char* to, bool directory) {
  if (!validPath(from) || !validPath(to) || !std::strcmp(from, "/") || destination.exists(to)) return false;
  if (&source == &destination) {
    const size_t length = std::strlen(from);
    if (!std::strcmp(from, to) || (directory && !std::strncmp(from, to, length) && to[length] == '/')) return false;
    if (source.rename(from, to)) return true;
  }
  return copy(source, from, destination, to, directory) && remove(source, from);
}
bool FileOperations::copy(fs::FS& source, const char* from, fs::FS& destination, const char* to, bool directory) {
  if (!validPath(from) || !validPath(to)) return false;
  if (&source == &destination) {
    const size_t length = std::strlen(from);
    if (!std::strcmp(from, to) || (directory && (!std::strcmp(from, "/") ||
        (!std::strncmp(from, to, length) && to[length] == '/')))) return false;
  }
  return copyTree(source, from, destination, to, directory, 0);
}
bool FileOperations::copyFile(fs::FS& source, const char* from, fs::FS& destination, const char* to) {
  if (destination.exists(to)) return false; // the file manager supplies a fresh destination
  if (!_buffer && _host.allocate) _buffer = static_cast<uint8_t*>(_host.allocate(chunkSize));
  if (!_buffer) return false;
  File input = source.open(from, "r");
  if (!input || input.isDirectory()) return false;
  size_t remaining = input.size();
  File output = destination.open(to, "w");
  if (!output) { input.close(); return false; }
  bool ok = true;
  while (remaining) {
    const size_t count = remaining < chunkSize ? remaining : chunkSize;
    const size_t read = input.read(_buffer, count);
    if (read != count || output.write(_buffer, count) != count) { ok = false; break; }
    remaining -= count;
    if (_host.progress) _host.progress();
  }
  output.close(); input.close();
  if (!ok) destination.remove(to);
  return ok;
}
bool FileOperations::copyTree(fs::FS& source, const char* from, fs::FS& destination, const char* to,
                              bool directory, unsigned depth) {
  if (depth > maxDepth) return false;
  if (!directory) return copyFile(source, from, destination, to);
  File input = source.open(from, "r");
  if (!input || !input.isDirectory()) return false;
  if (!destination.mkdir(to)) { input.close(); return false; }
  for (;;) {
    File entry = input.openNextFile();
    if (!entry) break;
    char childSource[200], childDestination[200];
    const bool isDirectory = entry.isDirectory();
    // name() is borrowed from the handle: copy it BEFORE close().
    const bool havePaths = childPath(childSource, sizeof childSource, from, entry.name()) &&
                           childPath(childDestination, sizeof childDestination, to, entry.name());
    entry.close();
    if (!havePaths || !copyTree(source, childSource, destination, childDestination, isDirectory, depth + 1)) {
      input.close(); return false;
    }
    if (_host.progress) _host.progress();
  }
  input.close(); return true;
}
bool FileOperations::remove(fs::FS& filesystem, const char* path) {
  if (!validPath(path) || !std::strcmp(path, "/")) return false;
  return removeTree(filesystem, path, 0);
}
bool FileOperations::removeTree(fs::FS& filesystem, const char* path, unsigned depth) {
  if (depth > maxDepth) return false;
  for (;;) {
    File directory = filesystem.open(path, "r");
    if (!directory) return false;
    if (!directory.isDirectory()) { directory.close(); return filesystem.remove(path); }
    File entry = directory.openNextFile();
    if (!entry) { directory.close(); return filesystem.rmdir(path); }
    char child[200];
    const bool havePath = childPath(child, sizeof child, path, entry.name());
    entry.close(); directory.close();
    if (!havePath || !removeTree(filesystem, child, depth + 1)) return false;
    // Reopen after deletion. FAT directory cursors are not stable across unlink.
    if (_host.progress) _host.progress();
  }
}
}

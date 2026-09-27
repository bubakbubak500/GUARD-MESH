// SPDX-License-Identifier: GPL-3.0-or-later
#include "BackupCatalog.h"

#include "../platform/UiPlatform.h"
#include <cstring>

namespace ui {
namespace services {

bool BackupPath::assign(const char *path) {
  value[0] = '\0';
  if (!path)
    return false;
  const std::size_t length = std::strlen(path);
  if (length >= Capacity)
    return false;
  std::memcpy(value, path, length + 1);
  return true;
}

BackupCatalog::~BackupCatalog() { releaseBuffers(); }

void BackupCatalog::releaseBuffers() {
  if (_paths)
    platform::release(_paths);
  if (_display)
    platform::release(_display);
  _paths = nullptr;
  _display = nullptr;
}

bool BackupCatalog::ensureBuffers() {
  char (*paths)[BackupPath::Capacity] = _paths;
  char (*display)[DisplayCapacity] = _display;
  bool newPaths = false;
  bool newDisplay = false;
  if (!paths) {
    paths = static_cast<char (*)[BackupPath::Capacity]>(
        platform::allocate(MaxEntries * BackupPath::Capacity, true));
    if (!paths)
      paths = static_cast<char (*)[BackupPath::Capacity]>(
          platform::allocate(MaxEntries * BackupPath::Capacity, false));
    newPaths = paths != nullptr;
  }
  if (!display) {
    display = static_cast<char (*)[DisplayCapacity]>(
        platform::allocate(MaxEntries * DisplayCapacity, true));
    if (!display)
      display = static_cast<char (*)[DisplayCapacity]>(
          platform::allocate(MaxEntries * DisplayCapacity, false));
    newDisplay = display != nullptr;
  }
  if (!paths || !display) {
    if (newPaths)
      platform::release(paths);
    if (newDisplay)
      platform::release(display);
    return false;
  }
  _paths = paths;
  _display = display;
  return true;
}

void BackupCatalog::clear() { _count = 0; }

bool BackupCatalog::scan() {
  clear();
  if (!_host.scan)
    return false;
  _host.scan(_host.context, *this);
  return true;
}

bool BackupCatalog::add(const char *stored, const char *display) {
  if (!stored || !display || _count >= MaxEntries)
    return false;
  BackupPath path(stored);
  if (stored[0] == '\0' || path.empty())
    return false;
  const std::size_t displayLength = std::strlen(display);
  if (!ensureBuffers())
    return false;
  for (std::size_t i = 0; i < _count; ++i)
    if (!std::strcmp(_paths[i], path.c_str()))
      return false;
  std::memcpy(_paths[_count], path.value, BackupPath::Capacity);
  const std::size_t copyLength = displayLength < DisplayCapacity ? displayLength : DisplayCapacity - 1;
  std::memcpy(_display[_count], display, copyLength);
  _display[_count][copyLength] = '\0';
  ++_count;
  return true;
}

bool BackupCatalog::copyPath(std::size_t index, BackupPath &out) const {
  if (index >= _count || !_paths)
    return false;
  std::memcpy(out.value, _paths[index], BackupPath::Capacity);
  return true;
}

const char *BackupCatalog::path(std::size_t index) const {
  return index < _count && _paths ? _paths[index] : nullptr;
}

const char *BackupCatalog::display(std::size_t index) const {
  return index < _count && _display ? _display[index] : nullptr;
}

bool BackupCatalog::isJsonName(const char *name) {
  if (!name)
    return false;
  const char *base = std::strrchr(name, '/');
  base = base ? base + 1 : name;
  if (base[0] == '.')
    return false;
  const char *extension = std::strrchr(base, '.');
  if (!extension)
    return false;
  static constexpr char Json[] = ".json";
  for (std::size_t i = 0; Json[i]; ++i) {
    char actual = extension[i];
    if (actual >= 'A' && actual <= 'Z')
      actual = static_cast<char>(actual - 'A' + 'a');
    if (actual != Json[i])
      return false;
  }
  return extension[sizeof(Json) - 1] == '\0';
}

} // namespace services
} // namespace ui

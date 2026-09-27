// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

namespace ui {
namespace services {

struct BackupPath {
  static constexpr std::size_t Capacity = 160;
  char value[Capacity] = {};

  BackupPath() = default;
  explicit BackupPath(const char *path) { assign(path); }
  bool assign(const char *path);
  bool empty() const { return value[0] == '\0'; }
  const char *c_str() const { return value; }
};

class BackupCatalog {
public:
  static constexpr std::size_t MaxEntries = 24;
  static constexpr std::size_t DisplayCapacity = 48;

  struct Host {
    void *context = nullptr;
    void (*scan)(void *, BackupCatalog &) = nullptr;

    Host() = default;
    Host(void *context, void (*scan)(void *, BackupCatalog &))
        : context(context), scan(scan) {}
  };

  BackupCatalog() = default;
  explicit BackupCatalog(Host host) : _host(host) {}
  ~BackupCatalog();
  BackupCatalog(const BackupCatalog &) = delete;
  BackupCatalog &operator=(const BackupCatalog &) = delete;

  bool scan();
  void clear();
  bool add(const char *stored, const char *display);
  bool copyPath(std::size_t index, BackupPath &out) const;
  const char *path(std::size_t index) const;
  const char *display(std::size_t index) const;
  std::size_t count() const { return _count; }

  static bool isJsonName(const char *name);

private:
  bool ensureBuffers();
  void releaseBuffers();

  Host _host;
  char (*_paths)[BackupPath::Capacity] = nullptr;
  char (*_display)[DisplayCapacity] = nullptr;
  std::size_t _count = 0;
};

} // namespace services
} // namespace ui

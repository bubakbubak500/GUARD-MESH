// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <FS.h>
namespace ui { namespace platform {
// Owns the boot-selected data backend/root. Remounting and SPI arbitration are
// borrowed from the card lifecycle; resolving never adopts another profile.
class DataFilesystem {
public:
  DataFilesystem() = default;
  DataFilesystem(const DataFilesystem&) = delete;
  DataFilesystem& operator=(const DataFilesystem&) = delete;
  struct Host { bool (*adopt)(); bool (*mount)(); void (*failure)(); };
  void configure(Host host) { _host = host; }
  bool ready();
  bool isSd();
  fs::FS* current() const { return _filesystem; }
  fs::FS* filesystem() { return ready() ? _filesystem : nullptr; }
  const char* root() const { return _root; }
  void finalizeBoot() { _bootFinalized = true; }
  File open(const char*, const char*);
private:
  Host _host{};
  fs::FS* _filesystem = nullptr;
  char _root[16] = {};
  bool _bootFinalized = false;
};
} }

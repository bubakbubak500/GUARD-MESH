// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace fs { class FS; }
namespace ui {
// One worker owns this short-lived installation. Stage and read back every
// file before commit; retain originals as backups until all replacements land.
// Rollback handles ordinary I/O errors. This is not a power-loss transaction:
// leftover backups require recovery and are never silently overwritten.
class StagedFileInstall {
public:
  struct Host { void* (*allocate)(size_t); void (*release)(void*); void (*yield)(bool stalled); };
  enum Error { None, InvalidPath, NoMemory, RecoveryRequired, WriteFailed, VerifyFailed, CommitFailed, CleanupFailed };
  StagedFileInstall(fs::FS& filesystem, Host host) : _filesystem(filesystem), _host(host) {}
  ~StagedFileInstall();
  StagedFileInstall(const StagedFileInstall&) = delete;
  StagedFileInstall& operator=(const StagedFileInstall&) = delete;
  bool stage(const char* path, const uint8_t* data, size_t size);
  bool commit();
  Error error() const { return _error; }
private:
  struct Entry { char path[80]{}, temporary[84]{}, backup[84]{}; bool staged=false, backed=false, installed=false; };
  fs::FS& _filesystem;
  Host _host;
  Entry _entries[2];
  unsigned _count=0;
  uint8_t* _buffer=nullptr;
  Error _error=None;
  bool _finished=false;
  bool fail(Error error) { _error=error; return false; }
  void rollback();
};
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "StagedFileInstall.h"
#include <FS.h>
#include <cstring>
#include <cstdio>
namespace ui {
namespace { constexpr size_t chunkSize=4096; }
StagedFileInstall::~StagedFileInstall() {
  for (unsigned i=0; i<_count; ++i)
    if (_entries[i].staged) _filesystem.remove(_entries[i].temporary);
  if (_buffer) _host.release(_buffer);
}
bool StagedFileInstall::stage(const char* path,const uint8_t* data,size_t size) {
  if (_finished || _error!=None || _count==2 || !path || path[0]!='/' || !path[1] ||
      std::strstr(path,"..") || std::strlen(path)>=sizeof _entries[0].path || !data || !size)
    return fail(InvalidPath);
  for (unsigned i=0; i<_count; ++i)
    if (!std::strcmp(path,_entries[i].path) || !std::strcmp(path,_entries[i].temporary) ||
        !std::strcmp(path,_entries[i].backup)) return fail(InvalidPath);
  auto& entry=_entries[_count];
  std::snprintf(entry.path,sizeof entry.path,"%s",path);
  std::snprintf(entry.temporary,sizeof entry.temporary,"%s.tmp",path);
  std::snprintf(entry.backup,sizeof entry.backup,"%s.bak",path);
  for (unsigned i=0; i<_count; ++i)
    if (!std::strcmp(entry.temporary,_entries[i].path) ||
        !std::strcmp(entry.backup,_entries[i].path)) return fail(InvalidPath);
  if (_filesystem.exists(entry.temporary) || _filesystem.exists(entry.backup)) return fail(RecoveryRequired);
  File old=_filesystem.open(path,"r");
  const bool directory=old && old.isDirectory(); old.close();
  if (directory) return fail(InvalidPath);
  if (!_buffer) _buffer=static_cast<uint8_t*>(_host.allocate(chunkSize));
  if (!_buffer) return fail(NoMemory);
  File output=_filesystem.open(entry.temporary,"w");
  if (!output) return fail(WriteFailed);
  entry.staged=true; ++_count;
  size_t written=0; unsigned stalls=0;
  while (written<size) {
    const size_t step=size-written<chunkSize ? size-written : chunkSize;
    std::memcpy(_buffer,data+written,step);
    const size_t n=output.write(_buffer,step);
    if (n>step) break;
    if (!n) { if (++stalls==4) break; }
    else { written+=n; stalls=0; }
    if (_host.yield) _host.yield(!n);
  }
  output.close();
  if (written!=size) return fail(WriteFailed);
  File input=_filesystem.open(entry.temporary,"r");
  bool verified=input && !input.isDirectory() && input.size()==size;
  for (size_t offset=0; verified && offset<size;) {
    const size_t step=size-offset<chunkSize ? size-offset : chunkSize;
    verified=input.read(_buffer,step)==step && !std::memcmp(_buffer,data+offset,step);
    offset+=step;
    if (_host.yield) _host.yield(false);
  }
  input.close();
  return verified || fail(VerifyFailed);
}
void StagedFileInstall::rollback() {
  for (unsigned i=_count; i>0; --i) {
    auto& entry=_entries[i-1];
    if (entry.installed) {
      if (!_filesystem.remove(entry.path)) { _error=RecoveryRequired; continue; }
      entry.installed=false;
    }
    if (entry.backed) {
      if (!_filesystem.rename(entry.backup,entry.path)) _error=RecoveryRequired;
      else entry.backed=false;
    }
  }
}
bool StagedFileInstall::commit() {
  if (_finished || _error!=None || !_count) return false;
  _finished=true;
  // Back up all originals before publishing either part of an app install.
  for (unsigned i=0; i<_count; ++i) {
    auto& entry=_entries[i];
    if (!_filesystem.exists(entry.path)) continue;
    if (!_filesystem.rename(entry.path,entry.backup)) {
      _error=CommitFailed; rollback(); return false;
    }
    entry.backed=true;
  }
  for (unsigned i=0; i<_count; ++i) {
    auto& entry=_entries[i];
    if (!_filesystem.rename(entry.temporary,entry.path)) {
      _error=CommitFailed; rollback(); return false;
    }
    entry.staged=false; entry.installed=true;
  }
  bool cleaned=true;
  for (unsigned i=0; i<_count; ++i) {
    auto& entry=_entries[i];
    if (entry.backed && !_filesystem.remove(entry.backup)) cleaned=false;
    else entry.backed=false;
  }
  return cleaned || fail(CleanupFailed);
}
}

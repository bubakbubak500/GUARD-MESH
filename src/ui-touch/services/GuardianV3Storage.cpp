// SPDX-License-Identifier: GPL-3.0-or-later
#include "GuardianV3Storage.h"
#if defined(ESP32) && !defined(GUARD_SIMULATOR)
#include <SPIFFS.h>
#include "../platform/StorageAccess.h"
namespace guardian { namespace v3 { namespace {
const char* path(unsigned slot) { return slot ? "/guardian-v3-1" : "/guardian-v3-0"; }
bool read(unsigned slot,std::string& bytes) {
  ui::platform::StorageLease lease; if(!lease.acquired()) return false;
  if(!SPIFFS.totalBytes()) return false;
  bytes.clear();
  if(!SPIFFS.exists(path(slot))) return true;
  auto file=SPIFFS.open(path(slot),"r");
  if(!file) return false;
  if(!file.size() || file.size()>DurableStore::MaxBytes+20) { bytes="invalid"; return true; }
  bytes.resize(file.size()); return file.readBytes(&bytes[0],bytes.size())==bytes.size();
}
bool write(unsigned slot,const std::string& bytes) {
  ui::platform::StorageLease lease; if(!lease.acquired()) return false;
  auto file=SPIFFS.open(path(slot),"w"); if(!file) return false;
  const bool ok=file.write(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size())==bytes.size();
  file.flush(); file.close(); return ok;
}
} StorageHooks deviceStorage() { return {read,write,true}; } } }
#else
namespace guardian { namespace v3 { namespace {
std::string slots[2];
bool read(unsigned slot,std::string& bytes) { bytes=slots[slot]; return !bytes.empty(); }
bool write(unsigned slot,const std::string& bytes) { slots[slot]=bytes; return true; }
} StorageHooks deviceStorage() { return {read,write}; } } }
#endif

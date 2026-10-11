// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <string>
#include <cstring>
namespace guardian { namespace v3 {
struct StorageHooks {
  bool (*read)(unsigned slot, std::string& bytes);
  bool (*write)(unsigned slot, const std::string& bytes);
  bool strictReads;
  // Strict readers return true with empty bytes for a missing slot, and false
  // for unavailable storage. A denied lease must never resemble a clean boot.
  StorageHooks(bool (*reader)(unsigned,std::string&)=nullptr,bool (*writer)(unsigned,const std::string&)=nullptr,bool strict=false) : read(reader),write(writer),strictReads(strict) {}
};
// Two complete slots: a torn write never removes the previous committed state.
// The wire JSON limit does not constrain our local bounded history snapshot.
// GV4S: magic[4], generation LE64, body length LE32, CRC32 LE32, body.
// CRC covers bytes 0..15 and the body, excluding the CRC field (16..19).
// Legacy GV3S omitted the header from its CRC and is deliberately rejected.
class DurableStore {
public:
  static constexpr size_t MaxBytes = 131072;
  void configure(StorageHooks value) { hooks=value; loaded=false; unavailableStorage=false; damaged=false; generation=0; active=1; }
  bool unavailable() const { return unavailableStorage; }
  bool corrupt() const { return damaged; }
  bool load(std::string& body) {
    loaded=false; unavailableStorage=false; damaged=false; generation=0; active=1; body.clear();
    bool existing=false;
    for(unsigned slot=0;slot<2;++slot) {
      std::string raw, candidate; uint64_t serial=0;
      const bool readOk=hooks.read && hooks.read(slot,raw);
      if(!readOk && hooks.strictReads) unavailableStorage=true;
      existing=existing || !raw.empty();
      if(readOk && decode(raw,serial,candidate) && serial>generation) {
        generation=serial; active=slot; body.swap(candidate);
      }
    }
    damaged=existing && generation==0;
    loaded=!unavailableStorage && !damaged;
    return loaded && generation!=0;
  }
  bool save(const std::string& body) {
    if(!loaded) { std::string old; load(old); }
    if(!loaded) return false;
    if(!hooks.write || !hooks.read || body.empty() || body.size()>MaxBytes || generation==UINT64_MAX) return false;
    const unsigned target=1-active;
    std::string raw(20,'\0'); raw.replace(0,4,"GV4S",4);
    put(raw,4,generation+1,8); put(raw,12,body.size(),4); raw+=body; put(raw,16,checksum(raw),4);
    if(!hooks.write(target,raw)) return false;
    std::string checked;
    if(!hooks.read(target,checked) || checked!=raw) return false;
    ++generation; active=target; return true;
  }
private:
  StorageHooks hooks; bool loaded=false, unavailableStorage=false, damaged=false; uint64_t generation=0; unsigned active=1;
  static void put(std::string& out,size_t at,uint64_t value,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i) out[at+i]=char(value>>(8*i));
  }
  static uint64_t get(const std::string& in,size_t at,unsigned bytes) {
    uint64_t value=0; for(unsigned i=0;i<bytes;++i) value|=uint64_t(uint8_t(in[at+i]))<<(8*i); return value;
  }
  static uint32_t checksum(const std::string& raw) {
    uint32_t crc=0xffffffff;
    for(size_t at=0;at<raw.size();++at) {
      if(at>=16 && at<20) continue;
      crc^=uint8_t(raw[at]); for(unsigned bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1)));
    }
    return ~crc;
  }
  static bool decode(const std::string& raw,uint64_t& serial,std::string& body) {
    if(raw.size()<20 || raw.size()>MaxBytes+20 || raw.compare(0,4,"GV4S")) return false;
    const auto length=get(raw,12,4); serial=get(raw,4,8);
    if(!serial || !length || length!=raw.size()-20) return false;
    if(checksum(raw)!=get(raw,16,4)) return false;
    body=raw.substr(20); return true;
  }
};
StorageHooks deviceStorage();
} }

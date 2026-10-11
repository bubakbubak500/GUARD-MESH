#include "ui-touch/services/GuardianV3Storage.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::abort(); } } while(0)
namespace {
std::string slots[2]; bool torn=false, denied=false;
bool read(unsigned slot,std::string& raw) { raw=slots[slot]; return !raw.empty(); }
bool write(unsigned slot,const std::string& raw) {
  if(denied) return false;
  slots[slot]=torn?raw.substr(0,raw.size()/2):raw; return true;
}
}
int main() {
  guardian::v3::DurableStore store; store.configure({read,write}); std::string raw;
  CHECK(!store.load(raw)); CHECK(store.save("first"));
  CHECK(store.save("second"));
  guardian::v3::DurableStore reboot; reboot.configure({read,write});
  CHECK(reboot.load(raw) && raw=="second");
  torn=true; CHECK(!reboot.save("broken third write")); torn=false;
  guardian::v3::DurableStore afterCut; afterCut.configure({read,write});
  CHECK(afterCut.load(raw) && raw=="second");
  CHECK(afterCut.save("third"));
  slots[0][20]^=1; // corrupt newest slot while retaining the previous generation
  guardian::v3::DurableStore corrupted; corrupted.configure({read,write});
  CHECK(corrupted.load(raw) && raw=="second");
  denied=true; CHECK(!corrupted.save("not committed")); denied=false;
  CHECK(!corrupted.save(std::string(guardian::v3::DurableStore::MaxBytes+1,'x')));
  slots[0].clear(); slots[1].clear();
  guardian::v3::DurableStore protectedStore; protectedStore.configure({read,write});
  CHECK(protectedStore.save("first") && protectedStore.save("second"));
  const std::string firstSlot=slots[0], secondSlot=slots[1];
  for(size_t byte=0;byte<20;++byte) {
    slots[0]=firstSlot; slots[1]=secondSlot;
    slots[0][byte]^=char(0x80); // A corrupt older serial must never outrank the current slot.
    guardian::v3::DurableStore damagedOlder; damagedOlder.configure({read,write});
    CHECK(damagedOlder.load(raw) && raw=="second");
    slots[0]=firstSlot; slots[1]=secondSlot;
    slots[1][byte]^=char(0x80);
    guardian::v3::DurableStore damagedNewer; damagedNewer.configure({read,write});
    CHECK(damagedNewer.load(raw) && raw=="first");
  }
  CHECK(firstSlot.compare(0,4,"GV4S")==0);
  // Valid legacy GV3S fixture: generation 1, body 123456789, CRC32 CBF43926.
  const unsigned char legacyHeader[]={'G','V','3','S',1,0,0,0,0,0,0,0,9,0,0,0,0x26,0x39,0xf4,0xcb};
  slots[0].assign(reinterpret_cast<const char*>(legacyHeader),sizeof legacyHeader);
  slots[0]+="123456789"; slots[1].clear();
  guardian::v3::DurableStore legacy; legacy.configure({read,write});
  CHECK(!legacy.load(raw) && legacy.corrupt() && !legacy.save("replace legacy"));
  puts("Guardian v3 durable slots: restart, torn write, body/header corruption, legacy fail-closed, failed write, bound PASS");
}

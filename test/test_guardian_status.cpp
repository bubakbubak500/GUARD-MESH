// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/models/GuardianStatus.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace guardian;
int main() {
  const uint8_t reference[] = {0x47,0x4d,1,7,5,0,0,0,2,0,0,0,1,0,0,0,7,0,0,0};
  uint8_t p[21]; memcpy(p, reference, 20);
  Session s;
  assert(s.accept(p,20,1) == Result::Disconnected);
  s.connect(); assert(!s.fresh(0));
  assert(s.accept(p,20,100) == Result::Ok);
  assert(s.status.inbox == 5 && s.status.unread == 2 && s.status.outbox == 1 && s.status.flags == 7);
  assert(s.fresh(15099) && !s.fresh(15100));
  assert(s.accept(p,20,15000) == Result::Sequence && s.receivedAt == 100);
  p[16] = 6; assert(s.accept(p,20,15000) == Result::Sequence);
  for (size_t n = 0; n <= 21; ++n) if (n != 20) assert(s.accept(p,n,200) == Result::Length);
  p[16] = 8;
  for (int field = 0; field < 4; ++field) {
    const uint8_t old = p[field]; p[field] = field == 3 ? 0x81 : 0;
    assert(s.accept(p,20,200) == Result::Invalid); p[field] = old;
  }
  p[3] = 0; assert(s.accept(p,20,200) == Result::Invalid);
  p[3] = 0x3f; assert(s.accept(p,20,15101) == Result::Ok && s.fresh(15101));
  s.disconnect(); assert(!s.fresh(15102));
  s.connect(); p[16] = 0; assert(s.accept(p,20,0) == Result::Ok && s.fresh(0));
  s.connect(); memset(p+16,0xff,4);
  assert(s.accept(p,20,0xfffffff0u) == Result::Ok);
  memset(p+16,0,4); assert(s.accept(p,20,20) == Result::Ok && s.fresh(21));
  p[19] = 0x80; assert(s.accept(p,20,22) == Result::Sequence);
  // Unaligned reference and 32-bit counts, not truncated to UI-sized counters.
  s.connect(); memcpy(p+1,reference,20); memset(p+5,0xff,4);
  assert(s.accept(p+1,20,100) == Result::Ok && s.status.inbox == 0xffffffffu);
  puts("Guardian protocol: PASS");
}

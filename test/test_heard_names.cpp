// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/services/HeardNameService.h"
#include "ui-touch/platform/UiPlatform.h"
#include <stdexcept>
#include <cstdlib>
#include <cstring>
#include <cstdio>
namespace { uint32_t clockMs = 0; fs::FS disk; }
namespace ui { namespace platform {
void* allocate(size_t bytes, bool) { return malloc(bytes); }
void release(void* memory) { free(memory); }
uint32_t milliseconds() { return clockMs++; }
void yieldUiWork() { ++clockMs; }
} }
namespace {
using Cache = ui::HeardNameCache;
using Service = ui::services::HeardNameService;
Service::Host host() { return {[]{ return &disk; }, []{ return "/names"; }}; }
void key(unsigned n, uint8_t* out) { memset(out, 0, 32); memcpy(out, &n, sizeof n); out[31] = 17; }
bool has(const Service& service, unsigned n, const char* expected) {
  uint8_t pub[32]; key(n, pub); char name[32];
  return service.lookup(pub, 32, name, sizeof name) && !strcmp(name, expected);
}
void drain(Service& service, uint32_t now) { service.tick(now); service.runPending(); service.tick(now + 1); }
}
void require(bool ok) { if (!ok) throw std::runtime_error("Heard-name regression failed"); }
int main() {
  Cache cache;
  require(cache.allocate(8, ui::platform::allocate, ui::platform::release));
  uint8_t pub[32]; char name[32];
  for (unsigned i = 0; i < 8; ++i) { key(i, pub); require(cache.remember(pub, "peer")); }
  key(0, pub); require(!cache.remember(pub, "peer")); // refresh LRU without a name write
  key(8, pub); cache.remember(pub, "new");
  key(1, pub); require(!cache.lookup(pub, 32, name, sizeof name));
  key(0, pub); require(cache.lookup(pub, 32, name, sizeof name));
  uint8_t other[32]; memcpy(other, pub, 32); other[31] = 22;
  cache.remember(other, "other");
  require(!cache.lookup(pub, 8, name, sizeof name));
  uint8_t resolved[32];
  require(cache.resolve(pub, 8, resolved) == -1);
  require(cache.resolve(pub, 32, resolved) == 1 && !memcmp(pub, resolved, 32));
  require(cache.lookup(pub, 32, name, sizeof name) && !strcmp(name, "peer"));
  cache.remember(pub, "renamed");
  require(cache.lookup(pub, 32, name, sizeof name) && !strcmp(name, "renamed"));
  cache.clear(); require(cache.count() == 0 && !cache.lookup(pub, 32, name, sizeof name));
  require(cache.resolve(pub, 8, resolved) == 0);
  Cache large;
  require(large.allocate(1024, ui::platform::allocate, ui::platform::release));
  for (unsigned i = 0; i < 10000; ++i) { key(i, pub); large.remember(pub, "bounded"); }
  require(large.count() == 1024);
  key(0, pub); require(!large.lookup(pub, 32, name, sizeof name));
  key(9999, pub); require(large.lookup(pub, 32, name, sizeof name));
  disk.enableMemory(); disk.mkdir("/names");
  {
    Service service; service.configure(host(), 128); drain(service, 0);
    key(42, pub); service.remember(pub, "old", 100);
    drain(service, 20200); require(has(service, 42, "old"));
    service.remember(pub, "new", 20300); drain(service, 40400);
    require(service.flush());
  }
  {
    Service service; service.configure(host(), 128);
    key(42, pub); service.remember(pub, "live wins", 0); drain(service, 0);
    require(has(service, 42, "live wins"));
    require(service.flush());
    // A queued old snapshot must never restore pre-clear names.
    key(99, pub); service.remember(pub, "stale", 100);
    service.tick(20200); service.clear(20201); service.runPending();
    drain(service, 20202); drain(service, 20203);
    require(service.flush() && !service.clearing() && service.count() == 0);
  }
  {
    Service service; service.configure(host(), 128); drain(service, 0);
    require(service.count() == 0);
    key(7, pub); service.remember(pub, "durable", 100); drain(service, 20200);
    service.remember(pub, "interrupted", 20300);
    disk.limitWrites(25); drain(service, 40400); disk.allowWrites();
  }
  {
    Service service; service.configure(host(), 128); drain(service, 0);
    require(has(service, 7, "durable")); // CRC rejects the torn newer slot
    service.clear(1); require(service.flush());
  }
  // Continuous arrivals cannot postpone a checkpoint indefinitely.
  {
    Service service; service.configure(host(), 128); drain(service, 0);
    for (unsigned i = 0; i <= 70; ++i) {
      key(i, pub); service.remember(pub, "stream", i * 1000 + 10);
      drain(service, i * 1000 + 10);
    }
  }
  {
    Service service; service.configure(host(), 128); drain(service, 0);
    require(service.count() >= 60 && service.count() <= 71);
  }
  puts("Heard names: indexed identities, prefix ambiguity, 10000-node bound, LRU, live/load merge, restart, clear cancellation, torn writes and continuous checkpoints passed.");
}

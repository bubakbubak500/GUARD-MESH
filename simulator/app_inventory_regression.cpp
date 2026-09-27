// SPDX-License-Identifier: GPL-3.0-or-later
#include "services/AppInventory.h"
#include <FS.h>
#include <atomic>
#include <thread>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
namespace {
fs::FS disk;
std::atomic<bool> hold{false}, entered{false};
int allocations=0, released=0, failAt=0;
std::atomic<int> filesystemCalls{0};
void check(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
void put(const char* path,const char* body) { auto f=disk.open(path,"w"); f.print(body); f.close(); }
ui::AppInventory::Host host() {
  return {
    []()->fs::FS*{++filesystemCalls;entered=true;while(hold.load())std::this_thread::yield();return &disk;},
    [](char* out,size_t size,const char* path){snprintf(out,size,"%s",path);},
    []{return 1;}, [](int){ui::InstalledApp a{};strcpy(a.id,"builtin");strcpy(a.name,"Built in");strcpy(a.ver,"1");return a;},
    [](size_t size)->void*{if(++allocations==failAt)return nullptr;return malloc(size);},
    [](void* memory){++released;free(memory);}
  };
}
}
void runAppInventoryRegression() {
  disk=fs::FS{}; disk.enableMemory(); disk.mkdir("/apps");
  put("/apps/alpha.json","{\"id\":\"alpha\",\"name\":\"Alpha\",\"ver\":\"2\"}");
  put("/apps/alpha.lua","return 1"); put("/apps/bare.lua","return 2");
  put("/apps/._junk.lua","junk"); put("/apps/overlong-name-that-must-not-be-truncated.lua","return 3");
  put("/apps/invalid.json","{\"id\":\"../wrong\",\"name\":\"Invalid\"}");
  failAt=2;
  { ui::AppInventory inventory(host()); check(!inventory.ready(),"Inventory accepted partial allocation");
    check(released==1,"Inventory leaked partial allocation"); failAt=0;
    check(inventory.request() && inventory.refresh(),"Offline caller could not claim queued scan");
    check(inventory.count()==3 && inventory.find("alpha") && inventory.find("bare") && inventory.find("builtin"),"Inventory scan/seed/dedup failed");
    check(!strcmp(inventory.find("alpha")->ver,"2"),"Manifest version lost");
    const auto* oldRows=inventory.rows();
    put("/apps/alpha.json","{\"id\":\"alpha\",\"name\":\"Updated\",\"ver\":\"3\"}");
    check(inventory.request(),"Inventory request failed"); hold=true; entered=false;
    std::thread worker([&]{inventory.runPending();});
    while(!entered.load())std::this_thread::yield();
    const bool unchanged=inventory.rows()==oldRows && !strcmp(inventory.find("alpha")->name,"Alpha");
    const bool blocked=!inventory.refresh(true);
    inventory.installed("fresh","Fresh","4");
    put("/apps/fresh.json","{\"id\":\"fresh\",\"name\":\"Fresh\",\"ver\":\"4\"}");
    hold=false; worker.join();
    check(unchanged && blocked,"Worker changed current inventory or overlapped synchronous scan");
    check(inventory.poll() && inventory.find("fresh"),"Old scan replaced newer install result");
    check(inventory.runPending() && inventory.poll(),"Coalesced inventory rescan lost");
    check(!strcmp(inventory.find("alpha")->name,"Updated") && inventory.count()==4,"Inventory publish lost completed snapshot");
    disk.remove("/apps/fresh.json"); inventory.invalidate(); check(inventory.refresh(),"Inventory invalidation ignored");
    check(!inventory.find("fresh"),"Inventory retained removed app");
    check(inventory.request() && inventory.runPending(),"Inventory did not stage older directory");
    inventory.installed("lagged","Known installed","5");
    check(inventory.poll() && inventory.find("lagged") && !inventory.active(),"Directory lag erased a known completed install");
    char small[4]; check(!ui::appManifestField("{\"id\":\"length\"}","id",small,sizeof small),"Manifest silently truncated identity");
    check(!ui::appManifestField("{\"id\":\"no end}","id",small,sizeof small),"Truncated manifest accepted");
    char escaped[20]; check(ui::appManifestField("{\"name\":\"A \\\"quote\\\"\"}","name",escaped,sizeof escaped) && !strcmp(escaped,"A \"quote\""),"Escaped manifest field lost");
  }
  check(released==3,"Inventory did not release both snapshots");
  {
    ui::AppInventory inventory(host());
    const int reads=filesystemCalls.load();
    check(inventory.prepare() && inventory.active() && inventory.find("builtin"),
          "Cold drawer did not expose built-ins while queueing its scan");
    check(filesystemCalls==reads,"Cold prepare accessed storage on the UI thread");
    check(inventory.cancelQueued() && !inventory.active() && inventory.find("builtin"),
          "Worker startup failure lost cache or left a queued job");
    check(inventory.prepare() && inventory.prepare(),"Failed worker could not be retried");
    check(filesystemCalls==reads,"Repeated prepare accessed storage");
    check(inventory.runPending() && inventory.poll() && inventory.find("alpha"),
          "Worker snapshot was not published outside the Store");
    check(!inventory.runPending(),"Repeated open queued a redundant second scan");
    const auto revision=inventory.revision();
    const int warmReads=filesystemCalls.load();
    for(int i=0;i<10;++i) check(inventory.prepare(),"Warm prepare failed");
    check(!inventory.active() && filesystemCalls==warmReads,"Warm drawer reread storage");
    check(inventory.prepare(true) && filesystemCalls==warmReads,"Invalidation scanned synchronously");
    check(inventory.runPending() && inventory.poll() && inventory.revision()==revision,
          "Unchanged scan forced a visual rebuild");
    check(inventory.request() && inventory.runPending(),"Removal race fixture was not staged");
    inventory.removed("alpha");
    const auto removedRevision=inventory.revision();
    check(removedRevision!=revision && !inventory.find("alpha"),"Confirmed removal was not immediate");
    check(inventory.poll() && !inventory.find("alpha") && inventory.revision()==removedRevision,
          "Older scan resurrected a removed app");
    std::puts("App inventory: cold/warm UI prepare = 0 storage calls; retry, revision and stale removal passed.");
  }
  check(released==5,"Async inventory leaked snapshots");
}

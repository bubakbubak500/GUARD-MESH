// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/AppStoreScreen.h"
#include "services/AppStoreJobs.h"
#include "services/AppStoreData.h"
#include "services/AppInventory.h"
#include <FS.h>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
namespace {
namespace store=ui::screens::store;
fs::FS disk;
store::Navigation navigation{};
int alerts=0,removals=0,reboots=0,ended=0,downloads=0;
bool workerAvailable=true;
char removedId[20]{};
void check(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
void path(char* out,size_t cap,const char* relative) { snprintf(out,cap,"%s",relative); }
void put(const char* name,const char* body) { auto f=disk.open(name,"w"); f.print(body); f.close(); }
lv_obj_t* label(lv_obj_t* object,const char* text) {
  if (lv_obj_check_type(object,&lv_label_class) && !strcmp(lv_label_get_text(object),text)) return object;
  for (uint32_t i=0;i<lv_obj_get_child_cnt(object);++i)
    if (auto* found=label(lv_obj_get_child(object,i),text)) return found;
  return nullptr;
}
lv_obj_t* button(const char* text) {
  auto* found=label(store::root(),text); check(found,"Store button missing"); return lv_obj_get_parent(found);
}
}
void runStoreScreenRegression(void (*pump)(unsigned)) {
  const auto roots=lv_obj_get_child_cnt(lv_layer_top());
  disk=fs::FS{}; disk.enableMemory(); disk.mkdir("/apps"); disk.mkdir("/lang");
  ui::AppInventory inventory({[]()->fs::FS*{return &disk;},path,[]{return 0;},
    [](int){return ui::InstalledApp{};},malloc,free});
  ui::AppStoreData data({[]()->fs::FS*{return &disk;},path,malloc,free});
  ui::AppStoreJobs jobs({malloc,free});
  data.parseApps(R"({"apps":[{"id":"clock","name":"Clock","ver":"2","desc":"A clock"}]})");
  data.parseLanguages(R"({"langs":[{"code":"xx","name":"Test language","ver":"1"}]})");
  store::Host host{};
  host.data=&data;host.jobs=&jobs;host.inventory=&inventory;
  host.statusHeight=[]{return 24;};host.begin=[](const char*,void(*)()){};host.end=[](void(*)()){++ended;};
  host.popupClose=[](lv_obj_t** object){if(*object)lv_obj_del_async(*object);*object=nullptr;};
  host.drawerChanged=[]{};host.alert=[](const char*,uint32_t){++alerts;};
  host.rebootWithNotice=[](const char*){++reboots;};host.languageKeyboard=[](uint8_t){};
  host.hiddenChanged=[]{};host.startWorker=[]{return workerAvailable;};
  host.workerMemory=[]{return store::WorkerMemory{workerAvailable,32,16};};
  host.beforeRebuild=[]{};host.navigation=[](const store::Navigation& nav){navigation=nav;};
  host.removeApp=[](const char* id){++removals;snprintf(removedId,sizeof removedId,"%s",id);};
  store::configure(host);
  store::open(); check(navigation.list && navigation.activeTab,"Store navigation not published");
  check(inventory.runPending(),"Store did not request inventory"); store::poll();
  auto* oldGet=button(TR("Get"));
  lv_event_send(oldGet,LV_EVENT_CLICKED,nullptr);
  check(jobs.downloadsActive(),"Store did not queue install");
  store::close(); check(!navigation.list && !store::root(),"Store retained detached navigation");
  store::open();
  const int alertsBefore=alerts;
  lv_event_send(oldGet,LV_EVENT_CLICKED,nullptr);
  check(alerts==alertsBefore,"Detached Store button submitted another operation");
  pump(2);
  ui::AppStoreJobs::Backend backend{nullptr,
    [](void*,ui::AppStoreJobs::Catalog,char*,size_t){return -1;},
    [](void*,bool language,const ui::AppStoreJobs::Request&,const char**){
      ++downloads;
      if (!language) {put("/apps/clock.lua","return 1");put("/apps/clock.json",R"({"id":"clock","name":"Clock","ver":"2"})");}
      return true;
    }};
  check(jobs.runOne(backend) && downloads==1,"Queued install was lost on reopen");
  store::close(); store::poll();
  check(jobs.downloadsActive(),"Closed Store consumed install result");
  store::open(); store::poll();
  check(!jobs.downloadsActive() && inventory.find("clock"),"Reopened Store did not publish completed installation");
  auto* oldRemove=button(TR("Remove"));
  // A coordinator may publish a new cache before the Store's next poll. The
  // old visible button must still address the app it was built for.
  inventory.removed("clock");
  inventory.installed("replacement","Replacement","1");
  lv_event_send(oldRemove,LV_EVENT_CLICKED,nullptr);
  check(removals==1 && !strcmp(removedId,"clock"),"Published inventory retargeted a visible remove action");
  removals=0;
  oldRemove=button(TR("Remove"));
  store::close(); store::openLanguages();
  lv_event_send(oldRemove,LV_EVENT_CLICKED,nullptr);
  check(removals==0,"Detached Store button removed an app");
  pump(2);
  check(navigation.activeTab==navigation.tabs[2],"Language entry did not select its tab");
  auto* oldLanguageGet=button(TR("Get"));
  store::close(); store::open();
  lv_event_send(oldLanguageGet,LV_EVENT_CLICKED,nullptr);
  check(!jobs.downloadsActive() && reboots==0,"Detached language callback performed an action");
  pump(2);
  const int endBefore=ended;
  lv_obj_del(store::root());
  check(!store::root() && !navigation.list && ended==endBefore+1,"External Store deletion did not retire state");
  pump(300);
  // Worker creation failure must not leave either catalog queued forever.
  data.setCatalogState(ui::AppStoreJobs::Apps,0);data.setCatalogState(ui::AppStoreJobs::Languages,0);
  workerAvailable=false; store::openLanguages();
  check(!inventory.active(),"Worker creation failure stranded inventory loading");
  check(data.appCount()==-1 && data.languageCount()==-1 &&
        !jobs.catalogActive(ui::AppStoreJobs::Apps) && !jobs.catalogActive(ui::AppStoreJobs::Languages),"Worker creation failure stranded catalog loading");
  store::close();pump(2);
  workerAvailable=true;
  store::configure({});
  check(lv_obj_get_child_cnt(lv_layer_top())==roots,"Store leaked an LVGL tree");
}

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
namespace ui {
class AppStoreData; class AppStoreJobs; class AppInventory;
namespace screens { namespace store {
struct Navigation { lv_obj_t* list; lv_obj_t* tabs[3]; lv_obj_t* activeTab; };
struct WorkerMemory { bool running; unsigned freeKb,largestKb; };
struct Host {
  AppStoreData* data;
  AppStoreJobs* jobs;
  AppInventory* inventory;
  int (*statusHeight)();
  void (*begin)(const char*,void(*)());
  void (*end)(void(*)());
  void (*popupClose)(lv_obj_t**);
  void (*drawerChanged)();
  void (*alert)(const char*,uint32_t);
  void (*rebootWithNotice)(const char*);
  void (*languageKeyboard)(uint8_t);
  void (*hiddenChanged)();
  bool (*startWorker)();
  WorkerMemory (*workerMemory)();
  void (*beforeRebuild)();
  void (*navigation)(const Navigation&);
  void (*removeApp)(const char* id);
};
// One Store instance on the device. It owns its LVGL tree and poll timer;
// services outlive it, so closing cannot discard an outstanding installation.
void configure(const Host&);
void open();
void openLanguages();
void close();
void poll();
lv_obj_t* root(); // borrowed, invalid after close or external LVGL deletion
} } }

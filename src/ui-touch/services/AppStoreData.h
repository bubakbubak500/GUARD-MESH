// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AppStoreJobs.h"
#include "../i18n.h"
namespace fs { class FS; }
namespace ui {
// UI-owned catalog and language snapshots. Network workers publish bytes via
// AppStoreJobs; only the UI parses/replaces these rows. No LVGL dependencies.
class AppStoreData {
public:
  struct App { char id[20],name[28],ver[12],desc[72],requires_cap[16]; };
  struct Language { char code[12],name[28],ver[8]; };
  struct Host {
    fs::FS* (*filesystem)();
    void (*path)(char*,size_t,const char*);
    void* (*allocate)(size_t);
    void (*release)(void*);
  };
  explicit AppStoreData(Host host) : _host(host) {}
  ~AppStoreData();
  AppStoreData(const AppStoreData&) = delete;
  AppStoreData& operator=(const AppStoreData&) = delete;
  bool ready();
  void parseApps(const char* json);
  void parseLanguages(const char* json);
  void scanLanguages();
  bool removeLanguage(const char* code);
  bool languageMetadata(const char* code,char* name,size_t name_cap,
                        char* base,size_t base_cap,char* ver=nullptr,size_t ver_cap=0);
  const App* apps() const { return _apps; }
  const Language* languages() const { return _languages; }
  const Language* installedLanguages() const { return _installed; }
  int appCount() const { return _appCount; }
  int languageCount() const { return _languageCount; }
  int installedCount() const { return _installedCount; }
  // Legacy presentation convention: 0 loading, -1 unavailable, positive rows.
  void setCatalogState(AppStoreJobs::Catalog kind,int state) {
    (kind==AppStoreJobs::Apps ? _appCount : _languageCount)=state<0 ? -1 : 0;
  }
private:
  enum { AppCapacity=16, LanguageCapacity=LANG_COUNT+2, InstalledCapacity=12 };
  Host _host;
  App* _apps=nullptr;
  Language* _languages=nullptr;
  Language* _installed=nullptr;
  int _appCount=0,_languageCount=0,_installedCount=0;
};
}

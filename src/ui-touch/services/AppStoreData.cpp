// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppStoreData.h"
#include "AppInventory.h"
#include <FS.h>
#include <cstring>
#include <cstdio>
namespace ui {
namespace {
// Catalogs contain arrays of flat records inside a wrapper object. Find leaf
// objects without interpreting braces or escaped quotes in display strings.
bool nextObject(const char*& cursor,char* object,size_t cap) {
  const char* start=nullptr;
  bool quoted=false,escape=false;
  while (*cursor) {
    const char* current=cursor++;
    const char c=*current;
    if (quoted) {
      if (escape) escape=false;
      else if (c=='\\') escape=true;
      else if (c=='"') quoted=false;
      continue;
    }
    if (c=='"') quoted=true;
    else if (c=='{') start=current;
    else if (c=='}' && start) {
      const size_t n=size_t(cursor-start);
      if (n<cap) { std::memcpy(object,start,n); object[n]=0; return true; }
      start=nullptr;
    }
  }
  return false;
}
bool displayField(const char* object,const char* key,char* out,size_t cap) {
  char value[512];
  out[0]=0;
  if (!appManifestField(object,key,value,sizeof value)) return false;
  size_t length=std::strlen(value);
  if (length>=cap) {
    length=cap-1;
    while (length && (static_cast<unsigned char>(value[length])&0xC0)==0x80) --length;
  }
  std::memcpy(out,value,length); out[length]=0;
  return length>0;
}
bool identifier(const char* value) {
  if (!value || !*value || std::strstr(value,"..")) return false;
  for (const unsigned char* p=reinterpret_cast<const unsigned char*>(value); *p; ++p)
    if (!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9') ||
          *p=='_' || *p=='-' || *p=='.')) return false;
  return true;
}
}
AppStoreData::~AppStoreData() {
  if (_apps) _host.release(_apps);
  if (_languages) _host.release(_languages);
  if (_installed) _host.release(_installed);
}
bool AppStoreData::ready() {
  if (!_apps) {
    _apps=static_cast<App*>(_host.allocate(sizeof(App)*AppCapacity));
    if (_apps) std::memset(_apps,0,sizeof(App)*AppCapacity);
  }
  if (!_languages) {
    _languages=static_cast<Language*>(_host.allocate(sizeof(Language)*LanguageCapacity));
    if (_languages) std::memset(_languages,0,sizeof(Language)*LanguageCapacity);
  }
  if (!_installed) {
    _installed=static_cast<Language*>(_host.allocate(sizeof(Language)*InstalledCapacity));
    if (_installed) std::memset(_installed,0,sizeof(Language)*InstalledCapacity);
  }
  return _apps && _languages && _installed;
}
void AppStoreData::parseApps(const char* json) {
  _appCount=-1;
  if (!ready() || !json) return;
  _appCount=0;
  char object[512]; const char* cursor=json;
  while (_appCount<AppCapacity && nextObject(cursor,object,sizeof object)) {
    App app{};
    if (!appManifestField(object,"id",app.id,sizeof app.id) || !identifier(app.id) ||
        !appManifestField(object,"ver",app.ver,sizeof app.ver) || !identifier(app.ver) ||
        !displayField(object,"name",app.name,sizeof app.name)) continue;
    bool duplicate=false;
    for (int i=0;i<_appCount;++i) if (!std::strcmp(_apps[i].id,app.id)) duplicate=true;
    if (duplicate) continue;
    displayField(object,"desc",app.desc,sizeof app.desc);
    appManifestField(object,"requires",app.requires_cap,sizeof app.requires_cap);
    _apps[_appCount++]=app;
  }
  if (!_appCount) _appCount=-1;
}
void AppStoreData::parseLanguages(const char* json) {
  _languageCount=-1;
  if (!ready() || !json) return;
  _languageCount=0;
  char object[512]; const char* cursor=json;
  while (_languageCount<LanguageCapacity && nextObject(cursor,object,sizeof object)) {
    Language language{};
    if (!appManifestField(object,"code",language.code,sizeof language.code) || !identifier(language.code) ||
        !displayField(object,"name",language.name,sizeof language.name)) continue;
    if (!appManifestField(object,"ver",language.ver,sizeof language.ver)) {
      // Only genuinely absent versions use the pre-versioning default.
      if (std::strstr(object,"\"ver\"")) continue;
      std::strcpy(language.ver,"1");
    }
    if (!identifier(language.ver)) continue;
    bool duplicate=false;
    for (int i=0;i<_languageCount;++i) if (!std::strcmp(_languages[i].code,language.code)) duplicate=true;
    if (!duplicate) _languages[_languageCount++]=language;
  }
  if (!_languageCount) _languageCount=-1;
}
bool AppStoreData::languageMetadata(const char* code, char* name, size_t name_cap,
                                 char* base, size_t base_cap,
                                 char* ver, size_t ver_cap) {
  if (!name || !name_cap || (base_cap && !base)) return false;
  name[0] = 0;
  if (base_cap) base[0] = 0;
  if (ver && ver_cap) ver[0] = 0;
  fs::FS* fs = _host.filesystem();
  if (!fs || !identifier(code) || strlen(code)>=sizeof(Language::code)) return false;
  char rel[28], path[64];
  snprintf(rel, sizeof rel, "/lang/%s.lang", code);
  _host.path(path, sizeof path, rel);
  File f = fs->open(path, "r");
  if (!f) return false;
  char head[192];
  size_t rd = f.read((uint8_t*)head, sizeof head - 1);
  f.close();
  head[rd] = 0;
  auto grab = [&](const char* tag, char* out, size_t cap) {
    const char* q = strstr(head, tag);
    if (!q) return;
    q += strlen(tag);
    while (*q == ' ') q++;
    size_t o = 0;
    while (q[o] && q[o] != '\n' && q[o] != '\r' && o + 1 < cap) { out[o] = q[o]; o++; }
    out[o] = 0;
  };
  grab("# name:", name, name_cap);
  if (base_cap) grab("# base:", base, base_cap);
  if (ver && ver_cap) {
    grab("# ver:", ver, ver_cap);
    if (!ver[0]) snprintf(ver, ver_cap, "1");   // pre-versioning files are v1
  }
  return name[0] != 0;
}

void AppStoreData::scanLanguages() {
  if (!ready()) return;
  _installedCount = 0;
  fs::FS* fs = _host.filesystem();
  if (!fs) return;
  char dir[48];
  _host.path(dir, sizeof dir, "/lang");
  File d = fs->open(dir);
  if (!d || !d.isDirectory()) return;
  File e;
  while (_installedCount < InstalledCapacity && (e = d.openNextFile())) {
    const char* nm = e.name();
    const char* leaf = strrchr(nm, '/');
    leaf = leaf ? leaf + 1 : nm;
    size_t ln = strlen(leaf);
    if (!e.isDirectory() && leaf[0] != '.' && ln > 5 && ln - 5 < sizeof(_installed[0].code) &&
        strcmp(leaf + ln - 5, ".lang") == 0) {
      Language& L = _installed[_installedCount];
      L = {};
      memcpy(L.code, leaf, ln - 5);
      L.code[ln - 5] = 0;
      e.close();
      char base[12];
      if (!languageMetadata(L.code, L.name, sizeof L.name, base, sizeof base,
                                L.ver, sizeof L.ver))
        snprintf(L.name, sizeof L.name, "%s", L.code);
      if (!L.ver[0]) snprintf(L.ver, sizeof L.ver, "1");
      _installedCount++;
      continue;
    }
    e.close();
  }
  d.close();
}
bool AppStoreData::removeLanguage(const char* code) {
  if (!identifier(code) || strlen(code)>=sizeof(Language::code)) return false;
  fs::FS* filesystem=_host.filesystem();
  if (!filesystem) return false;
  char relative[28],path[64];
  snprintf(relative,sizeof relative,"/lang/%s.lang",code);
  _host.path(path,sizeof path,relative);
  return filesystem->remove(path);
}
}

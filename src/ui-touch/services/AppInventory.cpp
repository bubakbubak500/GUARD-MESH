// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppInventory.h"
#include <FS.h>
#include <cstdio>
#include <cstring>
#include <new>
namespace ui {
namespace {
void whitespace(const char*& p) { while (*p==' ' || *p=='\t' || *p=='\r' || *p=='\n') ++p; }
bool jsonString(const char*& p,char* out,size_t capacity,bool& fits) {
  if (*p!='"') return false;
  ++p; size_t used=0; fits=true;
  while (*p && *p!='"') {
    char c=*p++;
    if (static_cast<unsigned char>(c)<0x20) return false;
    if (c=='\\') {
      c=*p++;
      if (!c) return false;
      if (c=='n') c='\n'; else if (c=='r') c='\r'; else if (c=='t') c='\t';
      else if (c!='"' && c!='\\' && c!='/') return false;
    }
    if (used+1<capacity) out[used++]=c;
    else fits=false;
  }
  out[used]=0;
  if (*p!='"') return false;
  ++p; return true;
}
}
bool appManifestField(const char* object,const char* key,char* out,size_t capacity) {
  if (!out || !capacity) return false;
  out[0]=0;
  if (!object || !key) return false;
  const char* cursor=object;
  while (*cursor) {
    if (*cursor!='"') { ++cursor; continue; }
    char token[28]; bool fits=false;
    if (!jsonString(cursor,token,sizeof token,fits)) return false;
    whitespace(cursor);
    if (*cursor!=':') continue; // A value string is not a field name.
    ++cursor; whitespace(cursor);
    if (fits && !strcmp(token,key)) {
      bool valueFits=false;
      if (jsonString(cursor,out,capacity,valueFits) && valueFits && out[0]) return true;
      out[0]=0; return false;
    }
    // Consume the complete value so its text can never become a field name.
    if (*cursor=='"' && !jsonString(cursor,token,sizeof token,fits)) return false;
  }
  return false;
}
AppInventory::~AppInventory() {
  if (_current) _host.release(_current);
  if (_pending) _host.release(_pending);
}
bool AppInventory::ready() {
  if (_current && _pending) return true;
  void* current = _host.allocate(sizeof(Snapshot));
  void* pending = _host.allocate(sizeof(Snapshot));
  if (!current || !pending) {
    if (current) _host.release(current);
    if (pending) _host.release(pending);
    return false;
  }
  _current = new(current) Snapshot{};
  _pending = new(pending) Snapshot{};
  addBuiltins(*_current); // Boot UI remains useful before the first storage result.
  if (_current->count) ++_revision;
  return true;
}
const InstalledApp* AppInventory::find(const Snapshot& snapshot, const char* id) {
  if (!id) return nullptr;
  for (int i=0; i<snapshot.count; ++i) if (!strcmp(snapshot.apps[i].id,id)) return &snapshot.apps[i];
  return nullptr;
}
const InstalledApp* AppInventory::rows() const { return _current ? _current->apps : nullptr; }
int AppInventory::count() const { return _current ? _current->count : 0; }
const InstalledApp* AppInventory::find(const char* id) const { return _current ? find(*_current,id) : nullptr; }
static bool validId(const char* id) {
  return id && *id && strcmp(id,".") && !strstr(id,"..") && !strchr(id,'/') && !strchr(id,'\\');
}
void AppInventory::scan(Snapshot& target) {
  target = {};
  fs::FS* filesystem = _host.filesystem();
  if (filesystem) {
    char directory[48] = {}; _host.path(directory,sizeof directory,"/apps");
    target.valid = true;
    for (int pass=0; pass<2 && target.count<Capacity; ++pass) {
      File dir = filesystem->open(directory);
      if (!dir || !dir.isDirectory()) { target.valid=false; break; }
      File entry;
      while (target.count<Capacity && (entry=dir.openNextFile())) {
        const char* name = entry.name();
        const char* leaf = strrchr(name,'/'); leaf=leaf ? leaf+1 : name;
        const size_t length = strlen(leaf);
        InstalledApp app{};
        if (!entry.isDirectory() && leaf[0]!='.') {
          if (!pass && length>5 && !strcmp(leaf+length-5,".json") && entry.size()>0 && entry.size()<900) {
            char buf[900]; const size_t expected=entry.size();
            const size_t read=entry.read(reinterpret_cast<uint8_t*>(buf),expected); buf[read]=0;
            if (read==expected && appManifestField(buf,"id",app.id,sizeof app.id) &&
                validId(app.id) && appManifestField(buf,"name",app.name,sizeof app.name) && !find(target,app.id)) {
              appManifestField(buf,"ver",app.ver,sizeof app.ver);
              appManifestField(buf,"icon",app.icon,sizeof app.icon);
              target.apps[target.count++]=app;
            }
          } else if (pass && length>4 && length-4<sizeof app.id && !strcmp(leaf+length-4,".lua")) {
            memcpy(app.id,leaf,length-4);
            if (validId(app.id) && !find(target,app.id)) {
              snprintf(app.name,sizeof app.name,"%s",app.id); strcpy(app.ver,"dev");
              target.apps[target.count++]=app;
            }
          }
        }
        entry.close();
      }
      dir.close();
    }
  }
  addBuiltins(target);
}
void AppInventory::addBuiltins(Snapshot& target) {
  for (int i=0; i<_host.builtinCount() && target.count<Capacity; ++i) {
    const InstalledApp app=_host.builtin(i);
    if (validId(app.id) && !find(target,app.id)) target.apps[target.count++]=app;
  }
}
bool AppInventory::sameRows(const Snapshot& a, const Snapshot& b) {
  if (a.count != b.count) return false;
  for (int i=0; i<a.count; ++i) {
    const auto& x=a.apps[i]; const auto& y=b.apps[i];
    if (strcmp(x.id,y.id) || strcmp(x.name,y.name) || strcmp(x.ver,y.ver) || strcmp(x.icon,y.icon)) return false;
  }
  return true;
}
bool AppInventory::prepare(bool force) {
  poll();
  if (!ready()) return false;
  if (force) invalidate();
  if (!_valid && !active()) return request();
  return true;
}
bool AppInventory::cancelQueued() {
  State expected=Queued;
  if (!_state.compare_exchange_strong(expected,Idle,std::memory_order_acq_rel)) return false;
  _rescan=false;
  _discard=false;
  return true;
}
bool AppInventory::request() {
  if (!ready()) return false;
  State expected=Idle;
  if (_state.compare_exchange_strong(expected,Queued,std::memory_order_release)) return true;
  _rescan=true;
  return true;
}
bool AppInventory::runPending() {
  State expected=Queued;
  if (!_state.compare_exchange_strong(expected,Running,std::memory_order_acquire)) return false;
  scan(*_pending);
  _state.store(Ready,std::memory_order_release);
  return true;
}
bool AppInventory::poll() {
  if (_state.load(std::memory_order_acquire)!=Ready) return false;
  if (!_discard) {
    if (!sameRows(*_current,*_pending)) ++_revision;
    auto* old=_current; _current=_pending; _pending=old; _valid=_current->valid;
  }
  _discard=false;
  _state.store(Idle,std::memory_order_release);
  if (_rescan) { _rescan=false; request(); }
  return true;
}
bool AppInventory::refresh(bool force) {
  poll();
  if (!ready()) return false;
  if (!force && _valid) return true;
  State expected=Idle;
  if (!_state.compare_exchange_strong(expected,Running,std::memory_order_acquire)) {
    // Offline startup can have a queued scan with no network worker running.
    // Compete for that same queued job; never touch a worker's running snapshot.
    expected=Queued;
    if (!_state.compare_exchange_strong(expected,Running,std::memory_order_acquire)) {
      if (force) { _discard=true; _rescan=true; }
      return false;
    }
  }
  _discard=false; _rescan=false;
  scan(*_pending);
  _state.store(Ready,std::memory_order_release);
  poll(); return true;
}
void AppInventory::invalidate() {
  _valid=false;
  if (active()) { _discard=true; _rescan=true; }
}
void AppInventory::installed(const char* id, const char* name, const char* version) {
  if (!ready() || !validId(id) || strlen(id)>=sizeof(InstalledApp::id)) return;
  // An older in-flight directory snapshot must not overwrite this newer result.
  // Do not automatically rescan a just-written FAT directory: it may still
  // return an older listing. Keep the known successful install until an
  // explicit refresh/invalidation requests another authoritative scan.
  if (active()) _discard=true;
  auto* app=const_cast<InstalledApp*>(find(id));
  if (!app && _current->count<Capacity) app=&_current->apps[_current->count++];
  if (!app) return;
  const InstalledApp before=*app;
  snprintf(app->id,sizeof app->id,"%s",id);
  snprintf(app->name,sizeof app->name,"%s",name ? name : id);
  snprintf(app->ver,sizeof app->ver,"%s",version ? version : "");
  if (strcmp(before.id,app->id) || strcmp(before.name,app->name) || strcmp(before.ver,app->ver)) ++_revision;
}
void AppInventory::removed(const char* id) {
  if (!_current || !id) return;
  if (active()) _discard=true;
  for (int i=0; i<_current->count; ++i) if (!strcmp(_current->apps[i].id,id)) {
    for (int j=i+1; j<_current->count; ++j) _current->apps[j-1]=_current->apps[j];
    _current->apps[--_current->count]={};
    ++_revision;
    return;
  }
}
}

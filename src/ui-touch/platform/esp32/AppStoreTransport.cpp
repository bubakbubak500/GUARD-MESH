// SPDX-License-Identifier: GPL-3.0-or-later
#include "../UiDevice.h"
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && CAP_LUA_APPS
#include "AppStoreTransport.h"
#include "../../services/AppStoreJobs.h"
#include "../../services/AppInventory.h"
#include "../../services/StagedFileInstall.h"
fs::FS* luaHostAppFs();
void luaHostAppPath(char*,size_t,const char*);
int luaStoreHttpReq(WiFiClient& client, HTTPClient& http, const char* url,
                    char* buf, size_t cap,
                    const uint8_t* body, size_t body_len, const char* ctype) {
  if (!url || !buf || cap < 2) return -1;
  http.setReuse(false);
  http.setConnectTimeout(8000);
  http.setTimeout(12000);
  http.setUserAgent("wadamesh-touch");
  if (!http.begin(client, url)) return -1;
  int code;
  if (body) {
    http.addHeader("Content-Type", (ctype && *ctype) ? ctype : "application/octet-stream");
    code = http.POST((uint8_t*)body, body_len);
  } else {
    code = http.GET();
  }
  // Any 2xx counts: a POST that a server answers 201 or 204 succeeded, and
  // demanding exactly 200 would fail every correctly-built upload endpoint.
  if (code < 200 || code > 299) { http.end(); return -1; }
  // Read by Content-Length. The old loop trusted connected() to mean "stream
  // done" and could quit after the first TCP window (~16 KB) with the rest
  // still in flight — a 49 KB language file came back one-third read. Now the
  // only clean exit for a known length is receiving ALL of it; stalls get a
  // progress-based 4 s grace (30 s absolute cap), and a short read FAILS
  // instead of handing back a truncated buffer.
  const int want = http.getSize();          // -1 when the server sent no length
  auto* st = http.getStreamPtr();
  size_t total = 0;
  bool complete = want == 0;
  const unsigned long t0 = millis();
  unsigned long last_progress = millis();
  while (!complete && total < cap - 1 && (millis() - t0) < 30000) {
    int avail = st ? st->available() : 0;
    if (avail <= 0) {
      if (want < 0 && st && !st->connected() && !st->available()) { complete = true; break; }
      if (millis() - last_progress > 4000) break;
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    int rd = st->read((uint8_t*)buf + total, min((size_t)avail, cap - 1 - total));
    if (rd > 0) {
      total += rd; last_progress = millis();
      if (want >= 0 && total == (size_t)want) complete = true;
    }
    else vTaskDelay(pdMS_TO_TICKS(5));
  }
  http.end();
  buf[total] = 0;
  if (!complete) return -1;   // timeout/capacity exhaustion is never a successful body
  return (int)total;
}

int luaStoreHttpGet(WiFiClient& client, HTTPClient& http, const char* url,
                    char* buf, size_t cap) {
  return luaStoreHttpReq(client, http, url, buf, cap, nullptr, 0, nullptr);
}

int luaStoreHttpGetOpaque(void* client, void* http, const char* url, char* buf, size_t cap) {
  return luaStoreHttpGet(*static_cast<WiFiClient*>(client), *static_cast<HTTPClient*>(http),
                         url, buf, cap);
}

int luaStoreHttpPostOpaque(void* client, void* http, const char* url, char* buf, size_t cap,
                           const void* body, size_t body_len, const char* ctype) {
  return luaStoreHttpReq(*static_cast<WiFiClient*>(client), *static_cast<HTTPClient*>(http),
                         url, buf, cap, (const uint8_t*)body, body_len, ctype);
}

namespace {
ui::StagedFileInstall::Host installHost() {
  return {
    [](size_t bytes)->void* { return heap_caps_malloc(bytes,MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); },
    [](void* ptr) { heap_caps_free(ptr); },
    [](bool stalled) { vTaskDelay(pdMS_TO_TICKS(stalled ? 50 : 1)); }
  };
}
void installError(const ui::StagedFileInstall& install,const char** error) {
  switch (install.error()) {
    case ui::StagedFileInstall::NoMemory: *error="Not enough memory"; break;
    case ui::StagedFileInstall::RecoveryRequired:
    case ui::StagedFileInstall::CleanupFailed: *error="Storage recovery required"; break;
    default: *error="Could not write to storage"; break;
  }
}
}
static bool luaStoreDownloadWorker(WiFiClient& client,HTTPClient& http,
                                   const char* id,const char* ver,const char** error) {
  fs::FS* fs=luaHostAppFs();
  if (!fs) return false;
  char dir[48]; luaHostAppPath(dir,sizeof dir,"/apps"); fs->mkdir(dir);
  ui::StagedFileInstall install(*fs,installHost());
  static const char* const extensions[]={"lua","json"};
  const size_t capacities[]={192*1024,1024};
  for (int i=0;i<2;++i) {
    char* buffer=static_cast<char*>(heap_caps_malloc(capacities[i],MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buffer) { *error="Not enough memory"; return false; }
    char url[128],rel[40],path[64];
    snprintf(url,sizeof url,"http://firmware.wadamesh.com/apps/%s/%s/%s.%s",id,ver,id,extensions[i]);
    const int count=luaStoreHttpGet(client,http,url,buffer,capacities[i]);
    snprintf(rel,sizeof rel,"/apps/%s.%s",id,extensions[i]);
    luaHostAppPath(path,sizeof path,rel);
    const bool staged=count>0 && install.stage(path,reinterpret_cast<const uint8_t*>(buffer),size_t(count));
    heap_caps_free(buffer);
    if (!staged) {
      if (count<=0) *error="Server or connection failed";
      else installError(install,error);
      return false;
    }
  }
  if (install.commit()) return true;
  installError(install,error); return false;
}

static bool luaStoreLangDownloadWorker(WiFiClient& client,HTTPClient& http,
                                       const char* code,const char* ver,const char** error) {
  fs::FS* fs=luaHostAppFs();
  if (!fs) return false;
  char dir[48]; luaHostAppPath(dir,sizeof dir,"/lang"); fs->mkdir(dir);
  size_t capacity=128*1024;
  char* buffer=static_cast<char*>(heap_caps_malloc(capacity,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buffer) { capacity=96*1024; buffer=static_cast<char*>(heap_caps_malloc(capacity,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)); }
  if (!buffer) { capacity=72*1024; buffer=static_cast<char*>(heap_caps_malloc(capacity,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)); }
  if (!buffer) { capacity=72*1024; buffer=static_cast<char*>(heap_caps_malloc(capacity,MALLOC_CAP_8BIT)); }
  if (!buffer) { *error="Not enough memory"; return false; }
  char url[112];
  if (ver && *ver) snprintf(url,sizeof url,"http://firmware.wadamesh.com/apps/lang/%s/%s.lang",ver,code);
  else snprintf(url,sizeof url,"http://firmware.wadamesh.com/apps/lang/%s.lang",code);
  const int count=luaStoreHttpGet(client,http,url,buffer,capacity);
  bool ok=false;
  if (count<=16) *error=count<0 ? "Server or connection failed" : "Empty reply";
  else {
    char rel[32],path[64];
    snprintf(rel,sizeof rel,"/lang/%s.lang",code); luaHostAppPath(path,sizeof path,rel);
    ui::StagedFileInstall install(*fs,installHost());
    ok=install.stage(path,reinterpret_cast<const uint8_t*>(buffer),size_t(count)) && install.commit();
    if (!ok) installError(install,error);
  }
  heap_caps_free(buffer);
  return ok;
}
namespace ui { namespace platform {
bool runAppStoreJob(AppStoreJobs& jobs,void* client,void* http) {
  struct Context { WiFiClient& client; HTTPClient& http; } context{*static_cast<WiFiClient*>(client),*static_cast<HTTPClient*>(http)};
  AppStoreJobs::Backend backend{
    &context,
    [](void* ptr,AppStoreJobs::Catalog kind,char* buffer,size_t cap) {
      auto& ctx=*static_cast<Context*>(ptr);
      return luaStoreHttpGet(ctx.client,ctx.http,kind==AppStoreJobs::Apps
        ? "http://firmware.wadamesh.com/apps/apps.json" : "http://firmware.wadamesh.com/apps/langs.json",buffer,cap);
    },
    [](void* ptr,bool language,const AppStoreJobs::Request& request,const char** error) {
      auto& ctx=*static_cast<Context*>(ptr);
      if (!language) return luaStoreDownloadWorker(ctx.client,ctx.http,request.id,request.version,error);
      char version[12]; snprintf(version,sizeof version,"%s",request.version);
      if (!version[0]) {
        // Boot repair resolves the version in private storage. It never parses
        // into, allocates or mutates the UI's language-catalog table.
        char* json=static_cast<char*>(heap_caps_malloc(2048,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (json) {
          const int read=luaStoreHttpGet(ctx.client,ctx.http,"http://firmware.wadamesh.com/apps/langs.json",json,2048);
          const char* next=read>0 ? json : "";
          while (const char* begin=strchr(next,'{')) {
            const char* end=strchr(begin,'}'); if (!end) break;
            const size_t size=size_t(end-begin+1); char object[128];
            if (size<sizeof object) {
              memcpy(object,begin,size); object[size]=0;
              char code[12];
              if (appManifestField(object,"code",code,sizeof code) && !strcmp(code,request.id)) {
                if (!appManifestField(object,"ver",version,sizeof version)) strcpy(version,"1");
                break;
              }
            }
            next=end+1;
          }
          heap_caps_free(json);
        }
      }
      return luaStoreLangDownloadWorker(ctx.client,ctx.http,request.id,version,error);
    }
  };
  return jobs.runOne(backend);
}
} }
#endif

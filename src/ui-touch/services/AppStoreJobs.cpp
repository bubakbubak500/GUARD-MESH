// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppStoreJobs.h"
#include <cstring>
#include <cstdio>
namespace ui {
namespace {
size_t capacity(AppStoreJobs::Catalog kind) { return kind==AppStoreJobs::Apps ? 8192 : 2048; }
bool segment(const char* text, size_t cap, bool empty) {
  if (!text || strlen(text)>=cap || (!empty && !*text)) return false;
  if (strstr(text,"..")) return false;
  for (const unsigned char* p=reinterpret_cast<const unsigned char*>(text); *p; ++p)
    if (!( (*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9') || *p=='_' || *p=='-' || *p=='.')) return false;
  return true;
}
}
AppStoreJobs::~AppStoreJobs() { for (auto& job:_catalogs) if (job.buffer) _memory.release(job.buffer); }
bool AppStoreJobs::requestCatalog(Catalog kind) {
  auto& job=_catalogs[kind];
  if (job.state.load(std::memory_order_acquire)!=Idle) return false;
  if (!job.buffer) job.buffer=static_cast<char*>(_memory.allocate(capacity(kind)));
  if (!job.buffer) return false;
  job.buffer[0]=0; job.size=-1;
  job.state.store(Queued,std::memory_order_release);
  return true;
}
bool AppStoreJobs::cancelCatalog(Catalog kind) {
  State expected=Queued;
  return _catalogs[kind].state.compare_exchange_strong(expected,Idle,std::memory_order_acq_rel);
}
bool AppStoreJobs::takeCatalog(Catalog kind, CatalogResult& result) {
  auto& job=_catalogs[kind];
  if (job.state.load(std::memory_order_acquire)!=Ready) return false;
  result={job.buffer,job.size>0 ? size_t(job.size) : 0,job.size>0};
  job.state.store(Idle,std::memory_order_release);
  return true;
}
bool AppStoreJobs::requestDownload(bool language,const char* id,const char* version,bool reboot,bool silent) {
  if (downloadsActive() || !segment(id,language?12:20,false) || !segment(version,language?8:12,language)) return false;
  auto& job=_downloads[language?1:0];
  job.request={};
  snprintf(job.request.id,sizeof job.request.id,"%s",id);
  snprintf(job.request.version,sizeof job.request.version,"%s",version);
  job.request.reboot=reboot; job.request.silent=silent;
  job.error[0]=0; job.ok=false;
  job.state.store(Queued,std::memory_order_release);
  return true;
}
bool AppStoreJobs::takeDownload(bool language,DownloadResult& result) {
  auto& job=_downloads[language?1:0];
  if (job.state.load(std::memory_order_acquire)!=Ready) return false;
  result.request=job.request; result.ok=job.ok;
  memcpy(result.error,job.error,sizeof result.error);
  job.state.store(Idle,std::memory_order_release);
  return true;
}
bool AppStoreJobs::downloadsActive() const {
  for (const auto& job:_downloads) if (job.state.load(std::memory_order_acquire)!=Idle) return true;
  return false;
}
void AppStoreJobs::failQueuedDownloads(const char* error) {
  for (auto& job:_downloads) {
    State expected=Queued;
    if (!job.state.compare_exchange_strong(expected,Running,std::memory_order_acquire)) continue;
    job.ok=false;
    snprintf(job.error,sizeof job.error,"%s",error ? error : "Not enough memory");
    job.state.store(Ready,std::memory_order_release);
  }
}
bool AppStoreJobs::downloading(bool language,const char* id) const {
  const auto& job=_downloads[language?1:0];
  return id && job.state.load(std::memory_order_acquire)!=Idle && !strcmp(job.request.id,id);
}
bool AppStoreJobs::runCatalog(Catalog kind,const Backend& backend) {
  auto& job=_catalogs[kind]; State expected=Queued;
  if (!job.state.compare_exchange_strong(expected,Running,std::memory_order_acquire)) return false;
  job.size=backend.fetch(backend.context,kind,job.buffer,capacity(kind));
  if (job.size<=0 || size_t(job.size)>=capacity(kind)) { job.size=-1; job.buffer[0]=0; }
  else job.buffer[job.size]=0;
  job.state.store(Ready,std::memory_order_release);
  return true;
}
bool AppStoreJobs::runDownload(bool language,const Backend& backend) {
  auto& job=_downloads[language?1:0]; State expected=Queued;
  if (!job.state.compare_exchange_strong(expected,Running,std::memory_order_acquire)) return false;
  const char* error=nullptr;
  job.ok=backend.download(backend.context,language,job.request,&error);
  snprintf(job.error,sizeof job.error,"%s",job.ok ? "" : error ? error : "Could not write to storage");
  job.state.store(Ready,std::memory_order_release);
  return true;
}
bool AppStoreJobs::runOne(const Backend& backend) {
  return runCatalog(Apps,backend) || runDownload(false,backend) || runCatalog(Languages,backend) || runDownload(true,backend);
}
}

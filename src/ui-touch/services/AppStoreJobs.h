// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <stddef.h>
namespace ui {
// UI submits immutable requests and consumes results; one worker executes them.
// Ready jobs remain owned until consumption, including when the Store is closed.
// The executor must stop before destruction. No method touches LVGL.
class AppStoreJobs {
public:
  enum Catalog { Apps, Languages };
  struct Memory { void* (*allocate)(size_t); void (*release)(void*); };
  struct Request { char id[20]; char version[12]; bool reboot, silent; };
  struct DownloadResult { Request request; bool ok; char error[72]; };
  struct CatalogResult { const char* json; size_t size; bool ok; };
  struct Backend {
    void* context;
    int (*fetch)(void*, Catalog, char*, size_t);
    bool (*download)(void*, bool language, const Request&, const char** error);
  };
  explicit AppStoreJobs(Memory memory) : _memory(memory) {}
  ~AppStoreJobs();
  AppStoreJobs(const AppStoreJobs&) = delete;
  AppStoreJobs& operator=(const AppStoreJobs&) = delete;
  bool requestCatalog(Catalog);
  bool cancelCatalog(Catalog); // queued only; a running/ready buffer is never reset
  bool catalogActive(Catalog kind) const { return _catalogs[kind].state.load(std::memory_order_acquire)!=Idle; }
  bool takeCatalog(Catalog, CatalogResult&); // json borrowed until next request
  bool requestDownload(bool language, const char* id, const char* version,
                       bool reboot = false, bool silent = false);
  bool takeDownload(bool language, DownloadResult&);
  void failQueuedDownloads(const char* error); // UI, when executor creation failed
  bool downloadsActive() const;
  bool downloading(bool language, const char* id) const; // UI-thread query
  bool runOne(const Backend&);
private:
  enum State { Idle, Queued, Running, Ready };
  struct CatalogJob { std::atomic<State> state{Idle}; char* buffer=nullptr; int size=-1; };
  struct DownloadJob { std::atomic<State> state{Idle}; Request request{}; bool ok=false; char error[72]{}; };
  Memory _memory;
  CatalogJob _catalogs[2];
  DownloadJob _downloads[2];
  bool runCatalog(Catalog, const Backend&);
  bool runDownload(bool language, const Backend&);
};
}

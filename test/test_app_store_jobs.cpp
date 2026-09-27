// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/services/AppStoreJobs.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {
using Jobs = ui::AppStoreJobs;
struct Server {
  std::atomic<bool> entered{false}, resume{false};
  bool hold = false, ok = true;
  int size = 2, downloads = 0, catalogs = 0;
  char error[72] = "Storage failed";
  Jobs::Request received{};
  bool language = false;
  void wait() {
    entered.store(true, std::memory_order_release);
    if (hold) while (!resume.load(std::memory_order_acquire)) std::this_thread::yield();
  }
  Jobs::Backend backend() {
    return {this,
      [](void* ptr, Jobs::Catalog, char* buf, size_t) {
        auto& s = *static_cast<Server*>(ptr);
        ++s.catalogs; s.wait();
        std::memcpy(buf, "[]", 2);
        return s.size;
      },
      [](void* ptr, bool language, const Jobs::Request& request, const char** error) {
        auto& s = *static_cast<Server*>(ptr);
        s.wait(); ++s.downloads; s.received = request; s.language = language;
        *error = s.error;
        return s.ok;
      }};
  }
  void awaitEntry() { while (!entered.load(std::memory_order_acquire)) std::this_thread::yield(); }
};
Jobs::Memory memory{std::malloc, std::free};
}

void appStoreJobsRegression() {
  Jobs jobs(memory);
  Server server;
  Jobs::CatalogResult catalog{};
  Jobs::DownloadResult result{};
  assert(!jobs.runOne(server.backend()));
  assert(!jobs.takeCatalog(Jobs::Apps, catalog));
  assert(!jobs.requestDownload(false, "../app", "1"));
  assert(!jobs.requestDownload(false, "app", ""));
  assert(!jobs.requestDownload(true, "cs", "12345678"));
  assert(!jobs.requestDownload(false, "app", "1/2"));
  assert(!jobs.requestDownload(false, "12345678901234567890", "1"));

  // The request owns its strings and cannot be overwritten by another tap,
  // including while the Store is closed and its completed job is unconsumed.
  char id[] = "radio", version[] = "2";
  assert(jobs.requestDownload(false, id, version, true, true));
  id[0] = 'x'; version[0] = '9';
  server.hold = true;
  std::thread download([&]{ assert(jobs.runOne(server.backend())); });
  server.awaitEntry();
  assert(jobs.downloading(false, "radio"));
  assert(!jobs.takeDownload(false, result));
  assert(!jobs.requestDownload(true, "cs", "1"));
  jobs.failQueuedDownloads("Must not overwrite a running job");
  server.resume.store(true, std::memory_order_release);
  download.join();
  assert(!std::strcmp(server.received.id, "radio"));
  assert(!std::strcmp(server.received.version, "2"));
  assert(!jobs.requestDownload(false, "other", "1"));
  assert(jobs.takeDownload(false, result));
  assert(result.ok && result.request.reboot && result.request.silent);
  assert(!std::strcmp(result.request.id, "radio") && !result.error[0]);
  assert(!jobs.downloadsActive() && !jobs.takeDownload(false, result));
  server.hold = false; server.ok = false;
  assert(jobs.requestDownload(true, "cs", ""));
  assert(jobs.runOne(server.backend()));
  server.error[0] = 'X';
  assert(jobs.takeDownload(true, result));
  assert(!result.ok && !std::strcmp(result.error, "Storage failed"));
  assert(server.language && !server.received.version[0]);
  assert(jobs.requestDownload(false, "clock", "1"));
  jobs.failQueuedDownloads("No executor");
  assert(!jobs.runOne(server.backend()));
  assert(jobs.takeDownload(false, result) && !result.ok);
  assert(!std::strcmp(result.error, "No executor"));
  assert(server.downloads == 2);

  // OOM doesn't strand a slot; failed/truncated replies never expose stale JSON.
  Jobs oom({[](size_t) -> void* { return nullptr; }, std::free});
  assert(!oom.requestCatalog(Jobs::Apps) && !oom.catalogActive(Jobs::Apps));
  for (int bytes : {2, -1, 8192, 0}) {
    server.size = bytes;
    assert(jobs.requestCatalog(Jobs::Apps));
    assert(!jobs.requestCatalog(Jobs::Apps));
    assert(jobs.runOne(server.backend()));
    assert(!jobs.cancelCatalog(Jobs::Apps));
    assert(jobs.takeCatalog(Jobs::Apps, catalog));
    assert(catalog.ok == (bytes == 2));
    assert(catalog.size == (bytes == 2 ? 2u : 0u));
    assert(!std::strcmp(catalog.json, bytes == 2 ? "[]" : ""));
  }
  server.hold = true; server.size = 2;
  server.entered.store(false); server.resume.store(false);
  assert(jobs.requestCatalog(Jobs::Languages));
  std::thread fetch([&]{ assert(jobs.runOne(server.backend())); });
  server.awaitEntry();
  assert(!jobs.cancelCatalog(Jobs::Languages));
  assert(!jobs.requestCatalog(Jobs::Languages));
  assert(!jobs.takeCatalog(Jobs::Languages, catalog));
  server.resume.store(true, std::memory_order_release);
  fetch.join();
  assert(jobs.takeCatalog(Jobs::Languages, catalog) && catalog.ok);
  server.hold = false;
  // Cancellation racing the worker has exactly one winner; no half-result.
  for (int i = 0; i < 150; ++i) {
    assert(jobs.requestCatalog(Jobs::Apps));
    bool ran = false;
    std::thread worker([&]{ ran = jobs.runOne(server.backend()); });
    bool cancelled = jobs.cancelCatalog(Jobs::Apps);
    worker.join();
    assert(ran != cancelled);
    assert(jobs.takeCatalog(Jobs::Apps, catalog) == ran);
    assert(!jobs.catalogActive(Jobs::Apps));
  }
}

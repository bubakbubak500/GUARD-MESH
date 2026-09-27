// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "BackupCatalog.h"
#include <FS.h>
#include <cstddef>
#include <cstdint>

namespace ui {
namespace services {

class BackupOperations {
public:
  enum class Status { Success, InvalidPath, StorageUnavailable, OpenFailed, Failed,
                      HistoryBusy, SdBusy, SdIncomplete };
  enum class Backend { Internal, Sd };
  struct ImportResult {
    Status status = Status::Failed;
    int channels = 0;
    int contacts = 0;
  };
  struct ExportResult {
    Status status = Status::Failed;
    Backend backend = Backend::Internal;
    char path[96] = {};
  };
  struct Host {
    void *context = nullptr;
    fs::FS *(*internal)(void *) = nullptr;
    fs::FS *(*sd)(void *) = nullptr;
    bool (*importBackup)(void *, File &, int *, int *) = nullptr;
    void (*exportBackup)(void *, Print &, double, double) = nullptr;
    void (*wdtBegin)(void *) = nullptr;
    void (*wdtEnd)(void *) = nullptr;
    void (*persistHistory)(void *) = nullptr;
    void (*setClockFloor)(void *, uint32_t) = nullptr;
    void (*flushPrefs)(void *) = nullptr;
    void (*waitHistoryWorker)(void *) = nullptr;
    bool (*historyBusy)(void *) = nullptr;
    void (*flushHistorySoon)(void *) = nullptr;
    bool (*sdLifecycleBusy)(void *) = nullptr;
    void (*resetSdMountBackoff)(void *) = nullptr;
    bool (*wipeSdData)(void *) = nullptr;
    bool (*sdMayHoldData)(void *) = nullptr;
    void (*factoryReset)(void *) = nullptr;
    void (*restart)(void *) = nullptr;
    bool resetUsesSd = false;
    bool resetChecksSdLifecycle = false;
    bool resetRequiresVerifiedSd = false;
  };

  explicit BackupOperations(Host host) : _host(host) {}
  ~BackupOperations();
  BackupOperations(const BackupOperations &) = delete;
  BackupOperations &operator=(const BackupOperations &) = delete;

  void scan(BackupCatalog &catalog) const;
  static void makeFilename(uint32_t epoch, bool wallClockCurrent,
                           uint32_t uptimeSeconds, char *out, std::size_t capacity);
  ImportResult beginImport(const BackupPath &selected);
  void finishImport(uint32_t clockFloor);
  ExportResult exportFile(const char *filename, double latitude, double longitude);
  bool deleteFile(const BackupPath &selected) const;
  Status factoryReset();

private:
  fs::FS *resolve(const char *stored, const char *&path) const;
  void endImportGuard();
  Host _host;
  bool _importGuardActive = false;
};

} // namespace services
} // namespace ui

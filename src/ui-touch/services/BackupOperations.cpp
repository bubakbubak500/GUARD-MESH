// SPDX-License-Identifier: GPL-3.0-or-later
#include "BackupOperations.h"
#include "../platform/UiPlatform.h"
#include "../platform/StorageAccess.h"

#include <cstdio>
#include <cstring>
#include <ctime>

namespace ui {
namespace services {
namespace {

class CheckedFileWriter : public Print {
public:
  explicit CheckedFileWriter(File &file) : _file(file) {}
  std::size_t write(uint8_t byte) override {
    if (!_ok) return 0;
    _buffer[_count++] = byte;
    if (_count == sizeof(_buffer)) flushBuffer();
    return _ok ? 1 : 0;
  }
  std::size_t write(const uint8_t *data, std::size_t length) override {
    if (!data || !_ok) return 0;
    std::size_t accepted = 0;
    while (accepted < length && _ok) {
      const std::size_t room = sizeof(_buffer) - _count;
      const std::size_t chunk = length - accepted < room ? length - accepted : room;
      std::memcpy(_buffer + _count, data + accepted, chunk);
      _count += chunk;
      accepted += chunk;
      if (_count == sizeof(_buffer)) flushBuffer();
    }
    return accepted;
  }
  bool finish() { flushBuffer(); return _ok; }
private:
  void flushBuffer() {
    if (!_ok || !_count) return;
    const std::size_t written = _file.write(_buffer, _count);
    if (written != _count) _ok = false;
    _count = 0;
  }
  File &_file;
  uint8_t _buffer[1024];
  std::size_t _count = 0;
  bool _ok = true;
};

class WdtScope {
public:
  explicit WdtScope(const BackupOperations::Host &host) : _host(host) {
    _host.wdtBegin(_host.context);
  }
  ~WdtScope() { _host.wdtEnd(_host.context); }
private:
  const BackupOperations::Host &_host;
};

void scanDirectory(fs::FS &filesystem, const char *directory, bool sd,
                   BackupCatalog &catalog) {
  File folder = filesystem.open(directory);
  if (!folder || (sd && !folder.isDirectory())) {
    if (folder) folder.close();
    return;
  }
  File entry = folder.openNextFile();
  while (entry) {
    if (!sd || !entry.isDirectory()) {
      const char *full = sd ? entry.name() : entry.path();
      if (full && BackupCatalog::isJsonName(full)) {
        const char *base = std::strrchr(full, '/');
        base = base ? base + 1 : full;
        char stored[BackupPath::Capacity];
        char display[BackupCatalog::DisplayCapacity];
        int pathLength = -1;
        int displayLength = -1;
        if (!sd) {
          pathLength = std::snprintf(stored, sizeof(stored), "int:%s", full);
          displayLength = std::snprintf(display, sizeof(display), "Internal: %s", base);
        } else if (directory[1] == '\0') {
          pathLength = std::snprintf(stored, sizeof(stored), "sd:/%s", base);
          displayLength = std::snprintf(display, sizeof(display), "SD: %s", base);
        } else {
          pathLength = std::snprintf(stored, sizeof(stored), "sd:%s/%s", directory, base);
          displayLength = std::snprintf(display, sizeof(display), "SD %s/: %s", directory + 1, base);
        }
        if (pathLength >= 0 && pathLength < static_cast<int>(sizeof(stored)) && displayLength >= 0)
          catalog.add(stored, display);
      }
    }
    entry.close();
    entry = folder.openNextFile();
  }
  folder.close();
}

} // namespace

BackupOperations::~BackupOperations() { endImportGuard(); }

void BackupOperations::scan(BackupCatalog &catalog) const {
  if (_host.internal) {
    if (fs::FS *internal = _host.internal(_host.context))
      scanDirectory(*internal, "/", false, catalog);
  }
  if (_host.sd) {
    if (fs::FS *sd = _host.sd(_host.context)) {
      scanDirectory(*sd, "/", true, catalog);
      scanDirectory(*sd, "/meshcomod", true, catalog);
    }
  }
}

void BackupOperations::makeFilename(uint32_t epoch, bool wallClockCurrent,
                                    uint32_t uptimeSeconds, char *out, std::size_t capacity) {
  if (!out || !capacity) return;
  if (wallClockCurrent && epoch > 0) {
    const time_t when = static_cast<time_t>(epoch);
    struct tm value;
    if (platform::localTime(when, value)) {
      std::snprintf(out, capacity, "meshcore-%04d%02d%02d-%02d%02d%02d.json",
                    value.tm_year + 1900, value.tm_mon + 1, value.tm_mday,
                    value.tm_hour, value.tm_min, value.tm_sec);
      return;
    }
  }
  std::snprintf(out, capacity, "meshcore-backup-%lu.json", static_cast<unsigned long>(uptimeSeconds));
}

fs::FS *BackupOperations::resolve(const char *stored, const char *&path) const {
  path = nullptr;
  if (!stored) return nullptr;
  if (!std::strncmp(stored, "int:", 4)) {
    path = stored + 4;
    return _host.internal ? _host.internal(_host.context) : nullptr;
  }
  if (!std::strncmp(stored, "sd:", 3)) {
    path = stored + 3;
    return _host.sd ? _host.sd(_host.context) : nullptr;
  }
  return nullptr;
}

void BackupOperations::endImportGuard() {
  if (_importGuardActive) {
    _importGuardActive = false;
    _host.wdtEnd(_host.context);
  }
}

BackupOperations::ImportResult BackupOperations::beginImport(const BackupPath &selected) {
  ImportResult result;
  if (selected.empty() || _importGuardActive) {
    result.status = Status::InvalidPath;
    return result;
  }
  const char *path = nullptr;
  fs::FS *filesystem = resolve(selected.c_str(), path);
  if (!path || path[0] != '/' || std::strstr(path, "..")) {
    result.status = Status::InvalidPath;
    return result;
  }
  if (!filesystem) {
    result.status = Status::StorageUnavailable;
    return result;
  }
  if (!_host.importBackup || !_host.wdtBegin || !_host.wdtEnd || !_host.persistHistory ||
      !_host.setClockFloor || !_host.flushPrefs || !_host.restart) return result;
  File file = filesystem->open(path, FILE_READ);
  _host.wdtBegin(_host.context);
  _importGuardActive = true;
  const bool ok = file && _host.importBackup(_host.context, file, &result.channels, &result.contacts);
  if (file) file.close();
  if (!ok) {
    endImportGuard();
    result.status = Status::Failed;
    return result;
  }
  _host.persistHistory(_host.context);
  result.status = Status::Success;
  return result;
}

void BackupOperations::finishImport(uint32_t clockFloor) {
  if (!_importGuardActive) return;
  _host.setClockFloor(_host.context, clockFloor);
  _host.flushPrefs(_host.context);
  _host.restart(_host.context);
  endImportGuard(); // A fake restart can return in a simulator regression.
}

BackupOperations::ExportResult BackupOperations::exportFile(const char *filename,
                                                            double latitude, double longitude) {
  ExportResult result;
  if (!filename || !filename[0] || std::strchr(filename, '/') ||
      std::strchr(filename, '\\') || std::strstr(filename, "..")) {
    result.status = Status::InvalidPath;
    return result;
  }
  const int length = std::snprintf(result.path, sizeof(result.path), "/%s", filename);
  if (length < 0 || length >= static_cast<int>(sizeof(result.path))) {
    result.status = Status::InvalidPath;
    result.path[0] = '\0';
    return result;
  }
  if (!_host.exportBackup || !_host.wdtBegin || !_host.wdtEnd || !_host.internal)
    return result;
  fs::FS *filesystem = _host.sd ? _host.sd(_host.context) : nullptr;
  File file;
  if (filesystem) {
    file = filesystem->open(result.path, FILE_WRITE);
    if (file) result.backend = Backend::Sd;
  }
  if (!file) {
    filesystem = _host.internal(_host.context);
    if (filesystem) file = filesystem->open(result.path, FILE_WRITE);
    result.backend = Backend::Internal;
  }
  if (!file) {
    result.status = Status::OpenFailed;
    return result;
  }
  bool written = false;
  {
    WdtScope guard(_host);
    CheckedFileWriter writer(file);
    _host.exportBackup(_host.context, writer, latitude, longitude);
    written = writer.finish();
    file.close();
    // This mirrors the existing replace-in-place export policy. A failed write
    // cannot restore a preexisting backup; remove the incomplete replacement.
    if (!written) filesystem->remove(result.path);
  }
  if (!written) {
    result.status = Status::Failed;
    return result;
  }
  result.status = Status::Success;
  return result;
}

bool BackupOperations::deleteFile(const BackupPath &selected) const {
  const char *path = nullptr;
  fs::FS *filesystem = resolve(selected.c_str(), path);
  return path && path[0] == '/' && !std::strstr(path, "..") && filesystem &&
         filesystem->remove(path);
}

BackupOperations::Status BackupOperations::factoryReset() {
  if (!_host.wdtBegin || !_host.wdtEnd || !_host.flushPrefs || !_host.waitHistoryWorker ||
      !_host.historyBusy || !_host.factoryReset || !_host.restart || !_host.flushHistorySoon)
    return Status::Failed;
  if (_host.resetUsesSd && (!_host.resetSdMountBackoff || !_host.wipeSdData))
    return Status::Failed;
  if (_host.resetRequiresVerifiedSd && !_host.resetUsesSd) return Status::Failed;
  if (_host.resetChecksSdLifecycle && !_host.sdLifecycleBusy) return Status::Failed;
  if (_host.resetRequiresVerifiedSd && !_host.sdMayHoldData) return Status::Failed;
  if (platform::storageAccess().admissionClosed()) {
    _host.flushHistorySoon(_host.context);
    return Status::SdBusy;
  }
  WdtScope guard(_host);
  _host.flushPrefs(_host.context);
  _host.waitHistoryWorker(_host.context);
  if (_host.historyBusy(_host.context)) {
    _host.flushHistorySoon(_host.context);
    return Status::HistoryBusy;
  }
  if (_host.resetChecksSdLifecycle && _host.sdLifecycleBusy(_host.context)) {
    _host.flushHistorySoon(_host.context);
    return Status::SdBusy;
  }
  platform::StorageTransition storageTransition;
  if (!storageTransition || !storageTransition.enter()) {
    _host.flushHistorySoon(_host.context);
    return Status::SdBusy;
  }
  if (_host.resetChecksSdLifecycle && _host.sdLifecycleBusy(_host.context)) {
    _host.flushHistorySoon(_host.context);
    return Status::SdBusy;
  }
  if (_host.resetUsesSd) {
    _host.resetSdMountBackoff(_host.context);
    const bool wiped = _host.wipeSdData(_host.context);
    if (_host.resetRequiresVerifiedSd && !wiped && _host.sdMayHoldData(_host.context)) {
      _host.flushHistorySoon(_host.context);
      return Status::SdIncomplete;
    }
  }
  _host.factoryReset(_host.context);
  _host.restart(_host.context);
  return Status::Success;
}

} // namespace services
} // namespace ui

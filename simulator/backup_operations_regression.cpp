// SPDX-License-Identifier: GPL-3.0-or-later
#include "services/BackupOperations.h"
#include "platform/UiPlatform.h"
#include "platform/StorageAccess.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <string>

namespace {
using Operations = ui::services::BackupOperations;
using Catalog = ui::services::BackupCatalog;
using Path = ui::services::BackupPath;

void require(bool condition, const char *why) {
  if (!condition) throw std::runtime_error(std::string("Backup operations: ") + why);
}

struct Fixture {
  fs::FS internal;
  fs::FS sd;
  fs::FS unmountedSd;
  bool internalAvailable = true;
  bool sdAvailable = true;
  bool sdUsesUnmounted = false;
  bool importOk = true;
  bool historyBusy = false;
  bool sdBusy = false;
  bool sdWipeOk = true;
  bool sdMayHoldData = true;
  bool prefsSawOpenGate = false;
  bool wipeBorrowedLease = false;
  bool resetDeniedOtherReader = false;
  int wdtDepth = 0;
  int wdtBegins = 0;
  int wdtEnds = 0;
  int imports = 0;
  int exports = 0;
  int persist = 0;
  int flushSoon = 0;
  int reset = 0;
  int restarts = 0;
  uint32_t clockFloor = 0;
  size_t exportBytes = 2048;
  std::string events;
  Fixture() { internal.enableMemory(); sd.enableMemory(); sd.mkdir("/meshcomod"); }
  void record(const char *name) { if (!events.empty()) events += ','; events += name; }

  static fs::FS *internalFs(void *context) {
    Fixture &f = *static_cast<Fixture *>(context);
    return f.internalAvailable ? &f.internal : nullptr;
  }
  static fs::FS *sdFs(void *context) {
    Fixture &f = *static_cast<Fixture *>(context);
    return f.sdAvailable ? (f.sdUsesUnmounted ? &f.unmountedSd : &f.sd) : nullptr;
  }
  static bool importBackup(void *context, File &file, int *channels, int *contacts) {
    Fixture &f = *static_cast<Fixture *>(context);
    ++f.imports;
    f.record("import");
    require(f.wdtDepth == 1, "import ran outside watchdog guard");
    char bytes[8] = {};
    const size_t read = file.read(reinterpret_cast<uint8_t *>(bytes), sizeof(bytes));
    if (!f.importOk || read != 4 || std::memcmp(bytes, "good", 4)) return false;
    *channels = 3;
    *contacts = 5;
    return true;
  }
  static void exportBackup(void *context, Print &out, double lat, double lon) {
    Fixture &f = *static_cast<Fixture *>(context);
    ++f.exports;
    f.record("export");
    require(f.wdtDepth == 1 && lat == 12.5 && lon == -4.25,
            "export lost watchdog or coordinate snapshot");
    uint8_t bytes[2048];
    std::memset(bytes, 'x', sizeof(bytes));
    out.write(bytes, f.exportBytes);
  }
  static void wdtBegin(void *context) {
    Fixture &f = *static_cast<Fixture *>(context);
    ++f.wdtDepth; ++f.wdtBegins; f.record("wdt+");
  }
  static void wdtEnd(void *context) {
    Fixture &f = *static_cast<Fixture *>(context);
    require(f.wdtDepth == 1, "watchdog guard unbalanced");
    --f.wdtDepth; ++f.wdtEnds; f.record("wdt-");
  }
  static void persistHistory(void *context) {
    Fixture &f = *static_cast<Fixture *>(context);
    require(f.wdtDepth == 1, "history persist ran outside import guard");
    ++f.persist; f.record("persist");
  }
  static void setClockFloor(void *context, uint32_t floor) {
    Fixture &f = *static_cast<Fixture *>(context);
    require(f.wdtDepth == 1, "clock floor set outside import guard");
    f.clockFloor = floor; f.record("clock");
  }
  static void flushPrefs(void *context) {
    Fixture &f = *static_cast<Fixture *>(context);
    require(f.wdtDepth == 1, "prefs flush outside watchdog guard");
    f.prefsSawOpenGate = !ui::platform::storageAccess().admissionClosed();
    f.record("prefs");
  }
  static void waitWorker(void *context) { static_cast<Fixture *>(context)->record("wait"); }
  static bool isHistoryBusy(void *context) { return static_cast<Fixture *>(context)->historyBusy; }
  static void flushHistorySoon(void *context) {
    Fixture &f = *static_cast<Fixture *>(context);
    ++f.flushSoon; f.record("flushSoon");
  }
  static bool isSdBusy(void *context) { return static_cast<Fixture *>(context)->sdBusy; }
  static void resetSdBackoff(void *context) { static_cast<Fixture *>(context)->record("backoff"); }
  static bool wipeSd(void *context) {
    Fixture &f = *static_cast<Fixture *>(context);
    ui::platform::StorageLease lease;
    f.wipeBorrowedLease = bool(lease);
    f.record("wipeSd");
    return f.sdWipeOk;
  }
  static bool mayHoldSd(void *context) { return static_cast<Fixture *>(context)->sdMayHoldData; }
  static void factoryReset(void *context) {
    Fixture &f = *static_cast<Fixture *>(context);
    uintptr_t otherContext = reinterpret_cast<uintptr_t>(&f);
    ui::platform::StorageAccess::Reader other(ui::platform::storageAccess(), otherContext);
    f.resetDeniedOtherReader = !other;
    ++f.reset; f.record("reset");
  }
  static void restart(void *context) {
    Fixture &f = *static_cast<Fixture *>(context);
    require(f.wdtDepth == 1, "restart ran outside watchdog guard");
    require(ui::platform::storageAccess().admissionClosed() == (f.reset != 0),
            "restart storage reservation does not match reset/import operation");
    ++f.restarts; f.record("restart");
  }
  Operations::Host host() {
    Operations::Host h;
    h.context = this;
    h.internal = internalFs; h.sd = sdFs;
    h.importBackup = importBackup; h.exportBackup = exportBackup;
    h.wdtBegin = wdtBegin; h.wdtEnd = wdtEnd;
    h.persistHistory = persistHistory; h.setClockFloor = setClockFloor;
    h.flushPrefs = flushPrefs; h.waitHistoryWorker = waitWorker;
    h.historyBusy = isHistoryBusy; h.flushHistorySoon = flushHistorySoon;
    h.sdLifecycleBusy = isSdBusy; h.resetSdMountBackoff = resetSdBackoff;
    h.wipeSdData = wipeSd; h.sdMayHoldData = mayHoldSd;
    h.factoryReset = factoryReset; h.restart = restart;
    h.resetUsesSd = true; h.resetChecksSdLifecycle = true;
    h.resetRequiresVerifiedSd = true;
    return h;
  }
};

void put(fs::FS &filesystem, const char *path, const char *data) {
  File file = filesystem.open(path, FILE_WRITE);
  require(bool(file), "fixture file open failed");
  require(file.write(reinterpret_cast<const uint8_t *>(data), std::strlen(data)) == std::strlen(data),
          "fixture file write failed");
  file.close();
}

void catalogAndPaths() {
  Fixture f;
  put(f.internal, "/good.json", "good");
  put(f.internal, "/.hidden.json", "bad");
  put(f.internal, "/not-json.txt", "bad");
  put(f.sd, "/removable.JSON", "good");
  put(f.sd, "/meshcomod/older.json", "good");
  Operations operations(f.host());
  Catalog catalog;
  operations.scan(catalog);
  require(catalog.count() == 3, "scan filters or backend folders changed");
  require(std::strcmp(catalog.path(0), "int:/good.json") == 0,
          "internal prefix changed");
  require(std::strcmp(catalog.path(1), "sd:/removable.JSON") == 0 &&
              std::strcmp(catalog.path(2), "sd:/meshcomod/older.json") == 0,
          "SD root or meshcomod prefixes changed");
  f.sdAvailable = false;
  require(!operations.deleteFile(Path("sd:/removable.JSON")) && f.sd.exists("/removable.JSON"),
          "unavailable selected SD backend was bypassed");
  require(!operations.deleteFile(Path("other:/good.json")), "unknown path prefix deleted data");
  require(operations.deleteFile(Path("int:/good.json")) && !f.internal.exists("/good.json"),
          "internal delete did not use selected backend");
}

void importAndExport() {
  Fixture f;
  put(f.internal, "/good.json", "good");
  put(f.sd, "/bad.json", "bad");
  Operations operations(f.host());
  f.sdAvailable = false;
  require(operations.beginImport(Path("sd:/good.json")).status == Operations::Status::StorageUnavailable,
          "unavailable import storage was not reported");
  require(f.imports == 0 && f.wdtDepth == 0, "unavailable import entered guarded operation");
  f.sdAvailable = true;
  require(operations.beginImport(Path("sd:/bad.json")).status == Operations::Status::Failed,
          "unreadable import was accepted");
  require(f.persist == 0 && f.restarts == 0 && f.wdtDepth == 0,
          "failed import persisted or leaked watchdog guard");
  require(operations.beginImport(Path("int:/missing.json")).status == Operations::Status::Failed &&
              f.imports == 1 && f.wdtDepth == 0,
          "missing import file called importer or leaked guard");
  f.internal.limitReads(2);
  require(operations.beginImport(Path("int:/good.json")).status == Operations::Status::Failed &&
              f.persist == 0 && f.restarts == 0 && f.wdtDepth == 0,
          "short-read import persisted or restarted");
  f.internal.allowReads();
  const Operations::ImportResult result = operations.beginImport(Path("int:/good.json"));
  require(result.status == Operations::Status::Success && result.contacts == 5 && result.channels == 3,
          "successful import result changed");
  require(f.persist == 1 && f.wdtDepth == 1 && f.restarts == 0,
          "import did not leave guard active for UI feedback");
  operations.finishImport(1234);
  require(f.clockFloor == 1234 && f.restarts == 1 && f.wdtDepth == 0 &&
              f.events.find("persist,clock,prefs,restart,wdt-") != std::string::npos,
          "import completion order or guard cleanup changed");

  auto exported = operations.exportFile("meshcore.json", 12.5, -4.25);
  require(exported.status == Operations::Status::Success &&
              exported.backend == Operations::Backend::Sd &&
              std::strcmp(exported.path, "/meshcore.json") == 0 &&
              f.sd.exists(exported.path) && f.sd.open(exported.path).size() == 2048,
          "export did not prefer mounted SD or report saved path");
  f.sdAvailable = false;
  exported = operations.exportFile("internal.json", 12.5, -4.25);
  require(exported.status == Operations::Status::Success &&
              exported.backend == Operations::Backend::Internal && f.internal.exists(exported.path),
          "export did not fall back to internal when SD unavailable");
  f.sdAvailable = true;
  f.sdUsesUnmounted = true;
  exported = operations.exportFile("unmounted.json", 12.5, -4.25);
  require(exported.status == Operations::Status::Success &&
              exported.backend == Operations::Backend::Internal && f.internal.exists(exported.path),
          "export did not fall back when a selected SD filesystem could not open");
  f.sdUsesUnmounted = false;
  f.sd.limitWrites(500);
  exported = operations.exportFile("partial.json", 12.5, -4.25);
  require(exported.status == Operations::Status::Failed && !f.sd.exists("/partial.json") &&
              !f.internal.exists("/partial.json") && f.wdtDepth == 0,
          "short SD write claimed success, switched backend, or leaked guard");
  f.exportBytes = 12;
  f.sd.limitWrites(5);
  exported = operations.exportFile("short-final-flush.json", 12.5, -4.25);
  require(exported.status == Operations::Status::Failed &&
              !f.sd.exists("/short-final-flush.json") && f.wdtDepth == 0,
          "short final buffer flush claimed export success");
  f.sdAvailable = false;
  f.internalAvailable = false;
  exported = operations.exportFile("no-storage.json", 12.5, -4.25);
  require(exported.status == Operations::Status::OpenFailed && f.wdtDepth == 0,
          "both export opens failing did not return OpenFailed");
  require(operations.exportFile("../escape.json", 12.5, -4.25).status == Operations::Status::InvalidPath,
          "unsafe export filename accepted");
  char tooLong[120];
  std::memset(tooLong, 'a', sizeof(tooLong) - 1);
  tooLong[sizeof(tooLong) - 1] = '\0';
  require(operations.exportFile(tooLong, 12.5, -4.25).status == Operations::Status::InvalidPath,
          "overlong export filename accepted");
}

void nameAndGuardCleanup() {
  char name[96];
  Operations::makeFilename(0, false, 42, name, sizeof(name));
  require(std::strcmp(name, "meshcore-backup-42.json") == 0,
          "uptime filename fallback changed");
  const uint32_t epoch = 1789900000;
  struct tm local;
  require(ui::platform::localTime(static_cast<time_t>(epoch), local),
          "test platform could not convert local time");
  char expected[96];
  std::snprintf(expected, sizeof(expected), "meshcore-%04d%02d%02d-%02d%02d%02d.json",
                local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                local.tm_hour, local.tm_min, local.tm_sec);
  Operations::makeFilename(epoch, true, 42, name, sizeof(name));
  require(std::strcmp(name, expected) == 0, "RTC local-time filename changed");

  Fixture f;
  put(f.internal, "/good.json", "good");
  {
    Operations pending(f.host());
    require(pending.beginImport(Path("int:/good.json")).status == Operations::Status::Success &&
                f.wdtDepth == 1, "pending import did not retain guard");
  }
  require(f.wdtDepth == 0 && f.wdtBegins == f.wdtEnds && f.restarts == 0,
          "destroying a pending import leaked watchdog guard or restarted");
}

void resetPolicy() {
  {
    Fixture f;
    Operations operations(f.host());
    ui::platform::StorageLease reader;
    require(bool(reader), "test reader lease was not acquired");
    require(operations.factoryReset() == Operations::Status::SdBusy &&
                f.reset == 0 && f.restarts == 0 && f.flushSoon == 1 &&
                !ui::platform::storageAccess().admissionClosed(),
            "in-flight storage lease did not block reset or release its reservation");
  }
  {
    Fixture f;
    Operations operations(f.host());
    int ownerToken = 0;
    ui::platform::StorageAccess::Transition owner;
    require(owner.request(ui::platform::storageAccess(),
                          reinterpret_cast<uintptr_t>(&ownerToken)) && owner.enter(),
            "test reservation was not acquired");
    require(operations.factoryReset() == Operations::Status::SdBusy &&
                f.events == "flushSoon" && f.reset == 0 &&
                ui::platform::storageAccess().admissionClosed(),
            "preexisting reservation did not reject reset before prefs flush");
    owner.release();
  }
  Fixture f;
  Operations operations(f.host());
  f.historyBusy = true;
  require(operations.factoryReset() == Operations::Status::HistoryBusy && f.flushSoon == 1 &&
              f.reset == 0 && f.wdtDepth == 0 &&
              f.events.find("prefs,wait,flushSoon,wdt-") != std::string::npos,
          "history worker/busy gate changed");
  f.historyBusy = false;
  f.sdBusy = true;
  require(operations.factoryReset() == Operations::Status::SdBusy && f.flushSoon == 2 &&
              f.reset == 0 && f.wdtDepth == 0,
          "SD lifecycle gate changed");
  f.sdBusy = false;
  f.sdWipeOk = false;
  f.sdMayHoldData = true; // Includes Pager's Unknown card state.
  require(operations.factoryReset() == Operations::Status::SdIncomplete && f.flushSoon == 3 &&
              f.reset == 0 && f.wdtDepth == 0 &&
              !ui::platform::storageAccess().admissionClosed(),
          "unverified SD wipe erased internal data");
  f.sdMayHoldData = false;
  require(operations.factoryReset() == Operations::Status::Success && f.reset == 1 &&
              f.restarts == 1 && f.wdtDepth == 0 &&
              f.prefsSawOpenGate && f.wipeBorrowedLease && f.resetDeniedOtherReader &&
              !ui::platform::storageAccess().admissionClosed() &&
              f.events.find("backoff,wipeSd,reset,restart,wdt-") != std::string::npos,
          "empty-slot reset or restart ordering changed");
  Operations::Host missing = f.host();
  missing.factoryReset = nullptr;
  Operations missingOperation(missing);
  require(missingOperation.factoryReset() == Operations::Status::Failed && f.reset == 1,
          "missing reset callback reached destructive operation");
  Operations::Host inconsistent = f.host();
  inconsistent.resetUsesSd = false;
  Operations inconsistentOperation(inconsistent);
  require(inconsistentOperation.factoryReset() == Operations::Status::Failed && f.reset == 1,
          "inconsistent SD verification flags reached destructive operation");
}
} // namespace

void runBackupOperationsRegression() {
  catalogAndPaths();
  importAndExport();
  nameAndGuardCleanup();
  resetPolicy();
}

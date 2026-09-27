// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace ui {
namespace diagnostics {
struct MemoryRegion {
  uint64_t total = 0, free = 0, minimum = 0, largest = 0;
};
struct Hardware {
  bool available = false;
  uint64_t uptimeSeconds = 0;
  MemoryRegion dram, psram;
  uint64_t dmaFree = 0, stackFree = 0;
  char chip[20]{}, reset[32]{};
  unsigned revision = 0, cores = 0;
  bool ble = false, wifi = false, bt = false;
  uint64_t flash = 0, sketch = 0, slotFree = 0;
  uint64_t nvsUsed = 0, nvsTotal = 0, nvsFree = 0, namespaces = 0;
  uint64_t internalTotal = 0, internalUsed = 0;
};
struct Stall {
  uint32_t atSeconds = 0;
  uint16_t durationMs = 0;
  char tag[32]{};
};
class StallHistory {
public:
  void record(uint32_t nowSeconds, const char *tag, uint32_t durationMs);
  size_t newest(Stall *out, size_t capacity) const;

private:
  Stall _items[16]{};
  size_t _count = 0, _write = 0;
};
struct Snapshot {
  Hardware hardware;
  char clockSource[32]{}, rtcChip[24]{}, rtcStatus[40]{};
  bool clockCurrent = false, rtcWriteVerified = false;
  uint32_t rxEvents = 0, rxRead = 0, rxErrors = 0, rxDrops = 0;
  bool rxBuffered = false;
  bool historySd = false, historyReady = false;
  unsigned historySegments = 0;
  uint64_t historyBytes = 0;
  char historySave[112]{};
  bool sdAvailable = false, sdChecked = false, sdMounted = false, sdBeginOk = false;
  unsigned sdAttempts = 0;
  uint32_t sdHz = 0;
  bool sdCapacitySupported = false, sdCapacityDone = false, sdCapacityOk = false;
  uint64_t sdTotal = 0, sdFree = 0;
  unsigned contacts = 0, maxContacts = 0, orphanedBlobs = 0;
  bool contactsSaveValid = false, contactsSaveInPlace = false;
  unsigned contactsSaved = 0, contactsSaveMs = 0;
  uint32_t nowSeconds = 0;
  Stall stalls[6]{};
  size_t stallCount = 0;
  char firmware[80]{}, buildDate[40]{};
  bool forkBuild = false;
};
// All formatters NUL-terminate when capacity > 0, including unavailable data.
void formatLive(const Snapshot &, char *, size_t);
void formatRest(const Snapshot &, char *, size_t);
void formatMemory(const Hardware &, char *, size_t);
} // namespace diagnostics
} // namespace ui

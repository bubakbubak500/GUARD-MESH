// SPDX-License-Identifier: GPL-3.0-or-later
#include "SystemDiagnostics.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
namespace ui {
namespace diagnostics {
namespace {
// snprintf returns the *required* length. Clamp the cursor, so truncation never
// turns the next append into an out-of-bounds pointer or an underflowed size.
class Text {
public:
  Text(char *out, size_t capacity) : _out(out), _capacity(out ? capacity : 0) {
    if (_capacity)
      _out[0] = 0;
  }
  void append(const char *format, ...) {
    if (_used + 1 >= _capacity)
      return;
    va_list args;
    va_start(args, format);
    int count = vsnprintf(_out + _used, _capacity - _used, format, args);
    va_end(args);
    if (count > 0)
      _used += std::min(size_t(count), _capacity - _used - 1);
  }

private:
  char *_out;
  size_t _capacity, _used = 0;
};
unsigned long long kb(uint64_t value) { return value / 1024; }
unsigned long long roundedKb(uint64_t value) { return value / 1024 + (value % 1024 >= 512); }
unsigned percent(uint64_t used, uint64_t total) {
  return total ? static_cast<unsigned>((static_cast<long double>(std::min(used, total)) * 100) / total) : 0;
}
} // namespace
void StallHistory::record(uint32_t now, const char *tag, uint32_t duration) {
  if (duration < 200)
    return;
  auto &entry = _items[_write];
  entry.atSeconds = now;
  entry.durationMs = uint16_t(std::min(duration, uint32_t(65535)));
  snprintf(entry.tag, sizeof entry.tag, "%s", tag ? tag : "?");
  _write = (_write + 1) % 16;
  if (_count < 16)
    ++_count;
}
size_t StallHistory::newest(Stall *out, size_t capacity) const {
  if (!out)
    return 0;
  size_t count = std::min(capacity, _count);
  for (size_t i = 0; i < count; ++i)
    out[i] = _items[(_write + 15 - i) % 16];
  return count;
}
void formatLive(const Snapshot &s, char *out, size_t capacity) {
  Text t(out, capacity);
  const auto &h = s.hardware;
  if (!h.available)
    return;
  auto seconds = h.uptimeSeconds;
  t.append("Uptime\n  %llud %02lluh %02llum %02llus\n\n", (unsigned long long)(seconds / 86400),
           (unsigned long long)(seconds % 86400 / 3600), (unsigned long long)(seconds % 3600 / 60),
           (unsigned long long)(seconds % 60));
  t.append("Memory\n  DRAM free: %llu KB / %llu KB (min %llu)\n  PSRAM free: %llu KB / %llu KB\n\n",
           kb(h.dram.free), kb(h.dram.total), kb(h.dram.minimum), kb(h.psram.free), kb(h.psram.total));
  const uint64_t read = uint64_t(s.rxRead) + s.rxErrors;
  const unsigned long lost = s.rxEvents > read ? s.rxEvents - read : 0;
  t.append("LoRa RX\n  heard: %lu  read: %lu  err: %lu\n  late-lost: %lu  qdrop: %lu  buffered: %s\n\n",
           (unsigned long)s.rxEvents, (unsigned long)s.rxRead, (unsigned long)s.rxErrors, lost,
           (unsigned long)s.rxDrops, s.rxBuffered ? "on" : "off");
  t.append("Chat store\n  %s   %u seg / %llu KB%s\n  last save: %s\n\n",
           s.historySd ? "SD /meshcomod" : "Internal flash", s.historySegments, kb(s.historyBytes),
           s.historyReady ? "" : "  (migration pending!)", s.historySave);
}
void formatRest(const Snapshot &s, char *out, size_t capacity) {
  Text t(out, capacity);
  const auto &h = s.hardware;
  if (h.available) {
    t.append("Chip\n  %s rev %u, %u core(s)\n  features:%s%s%s\n\n", h.chip, h.revision, h.cores,
             h.ble ? " BLE" : "", h.wifi ? " WiFi" : "", h.bt ? " BT" : "");
    t.append("Clock\n  source: %s (%s)\n", s.clockSource, s.clockCurrent ? "current" : "degraded");
    if (s.rtcChip[0])
      t.append("  %s: %s%s\n", s.rtcChip, s.rtcStatus, s.rtcWriteVerified ? " (write verified)" : "");
    t.append("\nFlash\n  chip: %llu MB\n  sketch: %llu KB used\n  app slot free: %llu KB\n\n",
             (unsigned long long)(h.flash / (1024 * 1024)), kb(h.sketch), kb(h.slotFree));
    if (!s.sdAvailable)
      t.append("microSD\n  not available on this hardware\n\n");
    else if (!s.sdChecked)
      t.append("microSD\n  status: not checked\n\n");
    else if (!s.sdMounted && !s.sdAttempts)
      t.append("microSD\n  status: not mounted\n  mount was not attempted\n\n");
    else {
      char clock[24];
      if (s.sdHz >= 1000000)
        snprintf(clock, sizeof clock, "%lu MHz", (unsigned long)(s.sdHz / 1000000));
      else if (s.sdHz)
        snprintf(clock, sizeof clock, "%lu kHz", (unsigned long)(s.sdHz / 1000));
      else
        snprintf(clock, sizeof clock, "unknown");
      const char *plural = s.sdAttempts == 1 ? "" : "s";
      if (!s.sdMounted)
        t.append("microSD\n  status: not mounted (missing or unreadable)\n"
                 "  last mount: %u attempt%s, %s\n  last init: %s\n\n",
                 s.sdAttempts, plural, clock, s.sdBeginOk ? "ok" : "failed");
      else {
        const bool failed = s.sdCapacitySupported && s.sdCapacityDone && !s.sdCapacityOk;
        t.append("microSD\n  status: %s\n  mount: %u attempt%s @ %s\n",
                 failed ? "mounted, capacity read failed" : "connected / mounted", s.sdAttempts, plural,
                 clock);
        if (s.sdCapacitySupported && !s.sdCapacityDone)
          t.append("  size: checking...\n");
        else if (s.sdCapacitySupported && s.sdCapacityOk)
          t.append("  size: %llu MB\n  free: %llu MB\n", (unsigned long long)(s.sdTotal / (1024 * 1024)),
                   (unsigned long long)(s.sdFree / (1024 * 1024)));
        t.append("\n");
      }
    }
    if (h.nvsTotal)
      t.append("NVS (settings store)\n  %llu / %llu entries (%u%%)\n  free: %llu  namespaces: %llu\n\n",
               (unsigned long long)h.nvsUsed, (unsigned long long)h.nvsTotal, percent(h.nvsUsed, h.nvsTotal),
               (unsigned long long)h.nvsFree, (unsigned long long)h.namespaces);
    t.append("Contact store\n  %u / %u contacts (~%llu KB)\n", s.contacts, s.maxContacts,
             kb(uint64_t(s.contacts) * 152));
    if (h.internalTotal)
      t.append("  internal flash: %llu / %llu KB (%u%%)\n", kb(h.internalUsed), kb(h.internalTotal),
               percent(h.internalUsed, h.internalTotal));
    if (s.contactsSaveValid)
      t.append("  last save: %s, %u rec, %u ms\n", s.contactsSaveInPlace ? "in-place" : "FULL rewrite",
               s.contactsSaved, s.contactsSaveMs);
    if (s.orphanedBlobs)
      t.append("  orphaned blobs: %u\n", s.orphanedBlobs);
    t.append("\nLast reset\n  %s\n\n", h.reset);
  }
  t.append("Loop stalls (>0.2s)\n");
  if (!s.stallCount)
    t.append("  none recorded\n");
  for (size_t i = 0; i < std::min(s.stallCount, size_t(6)); ++i)
    t.append("  -%lus  %s  %ums\n", (unsigned long)(s.nowSeconds - s.stalls[i].atSeconds), s.stalls[i].tag,
             unsigned(s.stalls[i].durationMs));
  t.append("\nBuild\n  %s\n  %s\n  WADAMESH TOUCH\n", s.firmware, s.buildDate);
  if (s.forkBuild)
    t.append("  unofficial fork build\n");
}
void formatMemory(const Hardware &h, char *out, size_t capacity) {
  Text t(out, capacity);
  if (!h.available)
    return;
  const auto region = [&](const char *name, const MemoryRegion &m) {
    t.append("%s\n  used  %llu / %llu KB\n  free  %llu KB (low %llu)\n  largest free  %llu KB\n\n", name,
             roundedKb(m.total > m.free ? m.total - m.free : 0), roundedKb(m.total), roundedKb(m.free),
             roundedKb(m.minimum), roundedKb(m.largest));
  };
  region("Internal DRAM", h.dram);
  region("PSRAM", h.psram);
  t.append("DMA-capable free: %llu KB\nUI loop stack free: %llu B\n\n", roundedKb(h.dmaFree),
           (unsigned long long)h.stackFree);
  t.append("Note: the ESP32 shares one heap\nacross all tasks - no per-task\nsplit like a PC. 'largest "
           "free'\nvs 'free' shows fragmentation.");
}
} // namespace diagnostics
} // namespace ui

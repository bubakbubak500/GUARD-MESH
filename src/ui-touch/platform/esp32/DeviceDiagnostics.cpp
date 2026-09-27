// SPDX-License-Identifier: GPL-3.0-or-later
#include "../DeviceDiagnostics.h"
#include "../../device_caps.h"
#if defined(ESP32)
#include <Arduino.h>
#include <Esp.h>
#include <SPIFFS.h>
#include <esp_chip_info.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <nvs.h>
#if CAP_SD
#include <SD.h>
#endif
#include <cstdio>
namespace ui {
namespace platform {
static const char *resetReasonString(esp_reset_reason_t r) {
  switch (r) {
  case ESP_RST_POWERON:
    return "Power on";
  case ESP_RST_EXT:
    return "External pin";
  case ESP_RST_SW:
    return "Software";
  case ESP_RST_PANIC:
    return "Panic / exception";
  case ESP_RST_INT_WDT:
    return "Int watchdog";
  case ESP_RST_TASK_WDT:
    return "Task watchdog";
  case ESP_RST_WDT:
    return "Other watchdog";
  case ESP_RST_DEEPSLEEP:
    return "Deep sleep wake";
  case ESP_RST_BROWNOUT:
    return "Brownout";
  case ESP_RST_SDIO:
    return "SDIO";
  case ESP_RST_UNKNOWN:
  default:
    return "Unknown";
  }
}
const char *resetReason() { return resetReasonString(esp_reset_reason()); }
static diagnostics::MemoryRegion memory(uint32_t caps) {
  diagnostics::MemoryRegion result;
  result.total = heap_caps_get_total_size(caps);
  result.free = heap_caps_get_free_size(caps);
  result.minimum = heap_caps_get_minimum_free_size(caps);
  result.largest = heap_caps_get_largest_free_block(caps);
  return result;
}
void readHardwareDiagnostics(diagnostics::Hardware &out, bool details) {
  out = diagnostics::Hardware{};
  out.available = true;
  out.uptimeSeconds = uint64_t(esp_timer_get_time()) / 1000000;
  out.dram = memory(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  out.psram = memory(MALLOC_CAP_SPIRAM);
  out.dmaFree = heap_caps_get_free_size(MALLOC_CAP_DMA);
  out.stackFree = uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);
  if (!details)
    return;
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  const char *model = chip.model == CHIP_ESP32S3   ? "ESP32-S3"
                      : chip.model == CHIP_ESP32S2 ? "ESP32-S2"
                      : chip.model == CHIP_ESP32   ? "ESP32"
                                                   : "ESP32-?";
  snprintf(out.chip, sizeof out.chip, "%s", model);
  out.revision = chip.revision;
  out.cores = chip.cores;
  out.ble = chip.features & CHIP_FEATURE_BLE;
  out.wifi = chip.features & CHIP_FEATURE_WIFI_BGN;
  out.bt = chip.features & CHIP_FEATURE_BT;
  out.flash = ESP.getFlashChipSize();
#if !defined(HAS_TANMATSU)
  // The two calls verify the entire image. Pay once, never each refresh.
  // AppFS on Tanmatsu has no standard running OTA partition: do not query it.
  static bool known = false;
  static uint32_t used = 0, free = 0;
  if (!known) {
    used = ESP.getSketchSize();
    free = ESP.getFreeSketchSpace();
    known = true;
  }
  out.sketch = used;
  out.slotFree = free;
#endif
  nvs_stats_t nvs{};
  if (nvs_get_stats(nullptr, &nvs) == ESP_OK) {
    out.nvsUsed = nvs.used_entries;
    out.nvsTotal = nvs.total_entries;
    out.nvsFree = nvs.free_entries;
    out.namespaces = nvs.namespace_count;
  }
  out.internalTotal = SPIFFS.totalBytes();
  out.internalUsed = SPIFFS.usedBytes();
  snprintf(out.reset, sizeof out.reset, "%s", resetReason());
}
StorageUsage::Snapshot readSdUsage(void *) {
  StorageUsage::Snapshot result;
#if CAP_SD
  if (SD.cardType() != CARD_NONE) {
    result.total = SD.totalBytes();
    const uint64_t used = SD.usedBytes();
    result.free = result.total > used ? result.total - used : 0;
    result.ok = result.total > 0;
  }
#endif
  result.done = true;
  return result;
}
} // namespace platform
} // namespace ui
#endif

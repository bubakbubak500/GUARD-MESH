// SPDX-License-Identifier: GPL-3.0-or-later
#include "WifiScanTransport.h"
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
#include "../../../helpers/esp32/WifiRuntimeStore.h"
#include "../../services/WifiScanJob.h"
#include <Arduino.h>
#include <WiFi.h>
#if defined(HAS_TDISPLAY_P4)
#include <C6WifiShim.h>
#endif
namespace ui {
namespace platform {
namespace {
// Watchdog-safe Wi-Fi scan. Starts an ASYNC scan and polls to completion with
// vTaskDelay yields + a hard time cap, so the calling task never blocks long
// enough to starve an idle task / the 5 s task watchdog. Replaces the old
// pattern of two back-to-back *synchronous* scans (~8 s total) with a
// WiFi.disconnect() wedged between them — which, with no AP present (Wi-Fi on
// but no SSID connected), always ran BOTH passes and tripped the task watchdog
// -> panic reboot. Returns the AP count (0 on none/failure/timeout); read
// results with WiFi.SSID(i).
static int wifiScanWatchdogSafe(WifiScanJob::Snapshot &result, uint32_t cap_ms, uint16_t per_chan_ms = 300) {
  result.radioStatus = (uint8_t)WiFi.status();
#if defined(HAS_TANMATSU)
  // esp-hosted C6: a real scan takes ~8 s but it ALSO fires a spurious early
  // "done, 0 items" event the Arduino layer latches. Wait for the real completion;
  // reject a 0 that came back < 4 s (spurious) and re-scan. (Tanmatsu-only — the S3
  // native path below is the original, known-good single pass.)
  for (int attempt = 0; attempt < 5; ++attempt) {
    WiFi.scanDelete();
    const uint32_t t0 = millis();
    const int16_t kick = WiFi.scanNetworks(true, true, false, per_chan_ms, 0);
    result.kick = kick;
    if (kick == WIFI_SCAN_FAILED) {
      result.status = WIFI_SCAN_FAILED;
      result.duration = millis() - t0;
      vTaskDelay(pdMS_TO_TICKS(300));
      continue;
    }
    int16_t st;
    while ((st = WiFi.scanComplete()) == WIFI_SCAN_RUNNING && (millis() - t0) < cap_ms)
      vTaskDelay(pdMS_TO_TICKS(100));
    const uint32_t dur = millis() - t0;
    result.status = st;
    result.duration = dur;
    if (st > 0)
      return st;
    if (dur >= 4000)
      return 0;                     // genuine empty after a full sweep
    vTaskDelay(pdMS_TO_TICKS(200)); // spurious fast-0 -> rescan
  }
  return 0;
#else
  // Native S3 wifi: the FIRST scan after the page opens reliably comes back empty or
  // fails (-2 ~6 s) on a cold radio — confirmed on STOCK beta_19 too ("first scan
  // nothing, rescan shows everything"). The radio itself is fine; it just needs a
  // warm retry. So loop a few attempts until APs appear; cap_ms bounds EACH attempt.
  // Returns on the first attempt that finds anything, so a hot scan is still ~3 s.
  for (int attempt = 0; attempt < 4; ++attempt) {
    WiFi.scanDelete();
    const int16_t kick = WiFi.scanNetworks(true, true, false, per_chan_ms, 0);
    result.kick = kick;
    if (kick == WIFI_SCAN_FAILED) {
      result.status = WIFI_SCAN_FAILED;
      vTaskDelay(pdMS_TO_TICKS(400));
      continue;
    }
    const uint32_t t0 = millis();
    int16_t st;
    while ((st = WiFi.scanComplete()) == WIFI_SCAN_RUNNING && (millis() - t0) < cap_ms)
      vTaskDelay(pdMS_TO_TICKS(50)); // yield -> feeds the task watchdog
    result.status = st;
    result.duration = millis() - t0;
    if (st > 0)
      return st;                    // found APs — done
    vTaskDelay(pdMS_TO_TICKS(400)); // empty / -2 / timeout -> warm up + rescan
  }
  return 0; // genuinely nothing after several tries
#endif
}

void scan(void *, WifiScanJob::Snapshot &result) {
  // Never bring the Wi-Fi driver up from here when BLE owns the radio (fresh device
  // on BLE => Bluedroid holds the internal heap). WiFi.mode(STA)/esp_wifi_init would
  // then OOM-panic (BLE-vs-Wi-Fi mutex, see main.cpp). wantsWifi() mirrors the boot
  // transport choice: true only when Wi-Fi is the active transport.
  if (!wifiConfigWantsWifi()) {
    return;
  }
#if defined(TLORA_PAGER)
  // Cold STA allocation is order-sensitive with NimBLE on the Pager and is
  // owned exclusively by main.cpp. If the driver disappeared after the UI
  // released this request, fail the sweep instead of initializing it here.
  if ((WiFi.getMode() & WIFI_MODE_STA) == 0 || wifiConfigPagerWifiBlocksBle()) {
    return;
  }
  vTaskDelay(pdMS_TO_TICKS(40));
#else
  if ((WiFi.getMode() & WIFI_MODE_STA) == 0) {
    WiFi.mode(WIFI_STA);
    vTaskDelay(pdMS_TO_TICKS(180));
  } else {
    vTaskDelay(pdMS_TO_TICKS(40));
  }
#endif
  result.available = true;
  const int found = wifiScanWatchdogSafe(result, 8000);
  for (int i = 0; i < found && result.count < WifiScanJob::Capacity; ++i) {
    const String ssid = WiFi.SSID(i);
    result.add(ssid.c_str());
  }
  WiFi.scanDelete();
}
} // namespace
bool runWifiScan(WifiScanJob &job) { return job.run(nullptr, scan); }
} // namespace platform
} // namespace ui
#endif

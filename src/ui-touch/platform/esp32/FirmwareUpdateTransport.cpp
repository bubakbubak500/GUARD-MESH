// SPDX-License-Identifier: GPL-3.0-or-later
#include "FirmwareUpdateTransport.h"
#include "../../device_caps.h"
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#if CAP_OTA
#include <Update.h>
#if CAP_SD
#include <SD.h>
#endif
#endif
#if defined(HAS_TDISPLAY_P4)
#include <C6Socket.h>
#include <C6WifiShim.h>
#define WiFiClient C6Client
#endif
#include "../../services/FirmwareUpdateJobs.h"
#include <cstdio>
#include <cstring>
namespace ui {
namespace platform {
namespace {
using Jobs = FirmwareUpdateJobs;
static const char *archive(bool beta) { return beta ? "BETA" : "TOUCH"; }
static int verchkFetchLatest(WiFiClient &client, HTTPClient &http, bool beta) {
  http.setReuse(false);
  http.setConnectTimeout(8000);
  http.setTimeout(12000);
  http.setUserAgent("wadamesh-touch");
  char listurl[72];
  snprintf(listurl, sizeof listurl, "http://firmware.wadamesh.com/releases/%s", archive(beta));
  if (!http.begin(client, listurl)) {
    return -1;
  }
  const int code = http.GET();
  if (code != 200) {
    http.end();
    return -1;
  }
  auto *st = http.getStreamPtr(); // NetworkClient* on arduino 3.x (P4: C6Client behind it)
  ui::ReleaseListing listing;
  uint32_t total = 0;
  const uint32_t LIMIT = 32768;
  const unsigned long t0 = millis();
  while (http.connected() && total < LIMIT && (millis() - t0) < 12000) {
    int avail = st ? st->available() : 0;
    if (avail <= 0) {
      if (!st || !st->connected())
        break;
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    while (avail-- > 0 && total < LIMIT) {
      const int c = st->read();
      if (c < 0)
        break;
      ++total;
      listing.feed(static_cast<char>(c));
    }
  }
  http.end();
  return listing.latest();
}

#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && CAP_OTA
// Per-board OTA download bin name (the app-only <name>.bin under releases/<ch>/beta_<N>/).
// MUST match the release artifact names exactly. Every touch board is dual-slot OTA-capable
// (CAP_OTA=1 + app0/app1 partitions + FIRMWARE_OTA_ENV) EXCEPT the Tanmatsu (AppFS/launcher,
// CAP_OTA=0, never reaches this file).
//
// A missing entry does NOT 404. This chain used to end in a bare #else naming the Heltec V4 TFT,
// so a board nobody added here silently fetched ANOTHER board's firmware -- and Update.end() only
// checks the image is valid for an ESP32-S3, which the V4 build is. The Wio Tracker L2 shipped
// exactly like that (beta_79's image carries "wadamesh-heltec-v4-tft"): an on-device update
// would flash V4 firmware onto Wio hardware. The T-Deck Pro would have followed it the moment it
// was published. Every board is now listed explicitly and anything else refuses to compile, so
// forgetting this table is a build break instead of a field brick.
#if defined(HAS_TDECK_GT911)
static const char *const OTA_BIN_NAME = "wadamesh-tdeck";
#elif defined(HAS_TDECK_PRO)
static const char *const OTA_BIN_NAME = "wadamesh-tdeck-pro";
#elif defined(HAS_TDISPLAY_P4)
#if defined(HAS_TDP4_LCD)
static const char *const OTA_BIN_NAME = "wadamesh-tdisplay-p4-lcd"; // T-Display P4 TFT-LCD SKU
#else
static const char *const OTA_BIN_NAME = "wadamesh-tdisplay-p4"; // T-Display P4 AMOLED SKU
#endif
#elif defined(HELTEC_LORA_V4_R8)
static const char *const OTA_BIN_NAME = "wadamesh-heltec-v4-r8-tft"; // must precede the V4-TFT fallback
#elif defined(HAS_THINKNODE_M9)
static const char *const OTA_BIN_NAME = "wadamesh-thinknode-m9";
#elif defined(HAS_RAK_TAP_V2)
static const char *const OTA_BIN_NAME = "wadamesh-rak-tap-v2";
#elif defined(TLORA_PAGER)
#if defined(USE_LR1121)
static const char *const OTA_BIN_NAME = "wadamesh-tlora-pager-lr1121";
#else
static const char *const OTA_BIN_NAME = "wadamesh-tlora-pager-sx1262";
#endif
#elif defined(ATTAKY_MESH_SERIES)
static const char *const OTA_BIN_NAME = "wadamesh-attaky";
#elif defined(HAS_WIO_TRACKER_L2)
static const char *const OTA_BIN_NAME = "wadamesh-wio-tracker-l2";
#elif defined(HELTEC_LORA_V4_TFT)
static const char *const OTA_BIN_NAME =
    "wadamesh-heltec-v4-tft"; // V4-R8 also defines this; its branch is above
#else
#error                                                                                                       \
    "OTA_BIN_NAME: this board has no release artifact name. Add it here AND to scripts/release.sh ENVS, or a self-update fetches another board's firmware."
#endif
// Download the latest published app-only bin over plain HTTP and flash it into the spare A/B slot
// via the Arduino Update writer. Runs on the tile-fetcher worker (off the UI thread); reports
// progress through the job owner for the UI poll timer. We fetch the
// IMMUTABLE versioned path (releases/TOUCH/beta_<N>/...) rather than latest/* to dodge the CDN
// cache on the mutable bin. esp_https_ota isn't usable here (no on-device TLS — see tile notes).
static void otaWorkerRun(WiFiClient &client, HTTPClient &http, const Jobs::InstallRequest &request,
                         Jobs::Progress progress, Jobs::InstallResult &result) {
  progress.report(progress.context, 0);
  char url[176];
  snprintf(url, sizeof url, "http://firmware.wadamesh.com/releases/%s/beta_%d/%s.bin", archive(request.beta),
           request.version, OTA_BIN_NAME);
  http.setReuse(false);
  http.setConnectTimeout(8000);
  http.setTimeout(20000);
  http.setUserAgent("wadamesh-touch");
  if (!http.begin(client, url)) {
    snprintf(result.message, sizeof result.message, "connect failed");
    result.ok = false;
    return;
  }
  const int code = http.GET();
  if (code != 200) {
    snprintf(result.message, sizeof result.message, "HTTP %d", code);
    http.end();
    result.ok = false;
    return;
  }
  const int len = http.getSize();
  if (len <= 0) {
    snprintf(result.message, sizeof result.message, "bad length");
    http.end();
    result.ok = false;
    return;
  }
  // Update.begin() picks the spare OTA slot AND verifies the image fits it -> the size guard. A
  // legacy too-small slot (old partition table, only app-OTA'd since) fails here, cleanly.
  if (!Update.begin((size_t)len)) {
    snprintf(result.message, sizeof result.message, "won't fit: %s", Update.errorString());
    http.end();
    result.ok = false;
    return;
  }
  Update.onProgress([progress](size_t done, size_t total) {
    progress.report(progress.context, total ? (int)((uint64_t)done * 100 / total) : 0);
  });
  auto *stream = http.getStreamPtr(); // NetworkClient* on arduino 3.x (P4: C6Client behind it)
  const size_t written = stream ? Update.writeStream(*stream) : 0;
  Update.onProgress(nullptr);
  http.end();
  if (written != (size_t)len) {
    snprintf(result.message, sizeof result.message, "short read %u/%d", (unsigned)written, len);
    Update.abort();
    result.ok = false;
    return;
  }
  if (!Update.end(true)) { // finalize, verify, mark the new slot bootable
    snprintf(result.message, sizeof result.message, "verify: %s", Update.errorString());
    result.ok = false;
    return;
  }
  progress.report(progress.context, 100);
  result.ok = true; // success -> the UI poll timer reboots into the new slot
}

#if CAP_SD && defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && CAP_OTA
// Stream the app-only bin for the active update channel onto the SD card
// (/BINS/wadamesh-beta_<N>-<stable|beta>.bin) so the Launcher can flash it —
// the no-A/B-slot counterpart to otaWorkerRun above. Same immutable versioned
// URL, same worker, but the bytes go to SD instead of the spare OTA slot.
//
static void sdFwWorkerRun(WiFiClient &client, HTTPClient &http, const Jobs::InstallRequest &request,
                          Jobs::Progress progress, Jobs::InstallResult &result) {
  progress.report(progress.context, 0);
  if (SD.cardType() == CARD_NONE) {
    snprintf(result.message, sizeof result.message, "no SD card");
    result.ok = false;
    return;
  }
  if (!SD.exists("/BINS") && !SD.mkdir("/BINS")) {
    snprintf(result.message, sizeof result.message, "can't create /BINS");
    result.ok = false;
    return;
  }
  char path[56];
  snprintf(path, sizeof path, "/BINS/wadamesh-beta_%d-%s.bin", request.version,
           request.beta ? "beta" : "stable");
  char url[176];
  snprintf(url, sizeof url, "http://firmware.wadamesh.com/releases/%s/beta_%d/%s.bin", archive(request.beta),
           request.version, OTA_BIN_NAME);
  http.setReuse(false);
  http.setConnectTimeout(8000);
  http.setTimeout(20000);
  http.setUserAgent("wadamesh-touch");
  if (!http.begin(client, url)) {
    snprintf(result.message, sizeof result.message, "connect failed");
    result.ok = false;
    return;
  }
  const int code = http.GET();
  if (code != 200) {
    snprintf(result.message, sizeof result.message, "HTTP %d", code);
    http.end();
    result.ok = false;
    return;
  }
  const int len = http.getSize();
  if (len <= 0) {
    snprintf(result.message, sizeof result.message, "bad length");
    http.end();
    result.ok = false;
    return;
  }
  // Free space, BEFORE writing a byte. Without this a full card fails partway
  // through with a bare "SD write failed" — indistinguishable from the DMA-buffer
  // short-write this path used to have, and the reported failure percentage is
  // just however far the remaining space stretched. The tile cache is the usual
  // culprit: it fills the card happily, and each tile is small enough to keep
  // succeeding long after a 2.8 MB firmware image cannot fit.
  SD.remove(path); // refresh any stale copy of the same tag (frees its space first)
  {
    const uint64_t total = SD.totalBytes(), used = SD.usedBytes();
    const uint64_t freeb = (total > used) ? (total - used) : 0;
    if (freeb < (uint64_t)len + (256u * 1024u)) { // headroom for FAT metadata
      snprintf(result.message, sizeof result.message, "need %u MB, %u MB free",
               (unsigned)(((uint64_t)len + 1048575u) / 1048576u), (unsigned)(freeb / 1048576u));
      http.end();
      result.ok = false;
      return;
    }
  }
  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    snprintf(result.message, sizeof result.message, "SD open failed");
    http.end();
    result.ok = false;
    return;
  }
  // The copy buffer MUST be internal DMA-capable RAM, NOT PSRAM. f.write() hands
  // the pointer down to SD/SPI, which DMAs from it; a PSRAM source is not reliably
  // DMA-capable, so the write commits fewer bytes than asked and the transfer dies
  // at a random offset — this is the "SD write failed" at 1-7% that three T-Decks
  // reported, and it has been broken since the feature landed in beta_36. The tile
  // cache never hit it because it copies through a 1 KB *stack* buffer; the .lang
  // installer did hit it and is fixed the same way (see langWriteAll).
  // Do NOT "optimise" this back onto PSRAM to save internal heap.
  size_t bufsz = 4096;
  uint8_t *buf = (uint8_t *)heap_caps_malloc(bufsz, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  if (!buf) {
    bufsz = 1024;
    buf = (uint8_t *)heap_caps_malloc(bufsz, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  }
  if (!buf) {
    f.close();
    SD.remove(path);
    http.end();
    snprintf(result.message, sizeof result.message, "out of memory");
    result.ok = false;
    return;
  }
  auto *st = http.getStreamPtr(); // NetworkClient* on arduino 3.x (P4: C6Client behind it)
  int received = 0;
  unsigned long last_data = millis();
  bool ok = true;
  while (received < len) {
    int avail = st ? st->available() : 0;
    if (avail <= 0) {
      if (!http.connected() || (millis() - last_data) > 15000) {
        ok = false;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    size_t want = (size_t)avail > bufsz ? bufsz : (size_t)avail;
    if (want > (size_t)(len - received))
      want = (size_t)(len - received);
    int n = st->read(buf, want);
    if (n <= 0) {
      if ((millis() - last_data) > 15000) {
        ok = false;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    last_data = millis();
    // A busy card can legitimately commit a short write; push the remainder
    // rather than declaring failure on the first one (four dead attempts in a
    // row means the card really is gone).
    int put = 0, stalls = 0;
    while (put < n) {
      const size_t w = f.write(buf + put, (size_t)(n - put));
      if (!w) {
        if (++stalls > 3)
          break;
        vTaskDelay(pdMS_TO_TICKS(50));
        continue;
      }
      stalls = 0;
      put += (int)w;
    }
    if (put != n) {
      snprintf(result.message, sizeof result.message, "SD write failed");
      ok = false;
      break;
    }
    received += n;
    progress.report(progress.context, (int)((uint64_t)received * 100 / (uint64_t)len));
    // Feed the IDLE task every chunk. read()'s internal yield only runs equal-
    // priority tasks, and a ~3 MB download over a fast link spins this loop hot
    // for long enough to starve the task watchdog — the same trap the tile
    // fetcher documents a few hundred lines down.
    vTaskDelay(1);
  }
  free(buf);
  f.close();
  http.end();
  if (!ok || received != len) {
    SD.remove(path); // never leave a truncated bin for the Launcher
    if (!result.message[0])
      snprintf(result.message, sizeof result.message, "short read %d/%d", received, len);
    result.ok = false;
    return;
  }
  strncpy(result.message, path, sizeof result.message - 1);
  result.message[sizeof result.message - 1] = 0;
  progress.report(progress.context, 100);
  result.ok = true;
}
#endif
#endif

struct Context {
  WiFiClient &client;
  HTTPClient &http;
};
} // namespace
bool runFirmwareCheck(FirmwareUpdateJobs &jobs, void *client, void *http) {
  Context context{*static_cast<WiFiClient *>(client), *static_cast<HTTPClient *>(http)};
  const bool ran = jobs.runCheck({&context,
                                  [](void *value, bool beta) {
                                    auto &c = *static_cast<Context *>(value);
                                    return verchkFetchLatest(c.client, c.http, beta);
                                  },
                                  nullptr});
  if (ran) {
    context.http.end();
    context.client.stop();
  }
  return ran;
}
bool runFirmwareInstall(FirmwareUpdateJobs &jobs, void *client, void *http) {
  Context context{*static_cast<WiFiClient *>(client), *static_cast<HTTPClient *>(http)};
  const bool ran = jobs.runInstall({&context, nullptr,
                                    [](void *value, const Jobs::InstallRequest &request,
                                       Jobs::Progress progress, Jobs::InstallResult &result) {
                                      auto &c = *static_cast<Context *>(value);
#if CAP_OTA
                                      if (request.destination == Jobs::Destination::Ota) {
                                        otaWorkerRun(c.client, c.http, request, progress, result);
                                        return;
                                      }
#if CAP_SD
                                      if (request.destination == Jobs::Destination::Sd) {
                                        sdFwWorkerRun(c.client, c.http, request, progress, result);
                                        return;
                                      }
#endif
#endif
                                      (void)c;
                                      (void)request;
                                      (void)progress;
                                      snprintf(result.message, sizeof result.message, "Update unavailable");
                                    }});
  if (ran) {
    context.http.end();
    context.client.stop();
  }
  return ran;
}
} // namespace platform
} // namespace ui
#endif

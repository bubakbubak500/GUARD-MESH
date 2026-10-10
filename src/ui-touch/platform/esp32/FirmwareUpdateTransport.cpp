// SPDX-License-Identifier: GPL-3.0-or-later
#include "FirmwareUpdateTransport.h"
#include "../../services/FirmwareUpdateJobs.h"
#include "../../services/FirmwareUpdateSource.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && (defined(LILYGO_TDECK) || defined(HELTEC_LORA_V4_TFT))
#include <Arduino.h>
#include <WiFi.h>
#include <Update.h>
#include <esp_http_client.h>
// Arduino's WiFiClientSecure shadows the ESP-IDF header with its own bundle
// wrapper. The HTTP client uses the SDK's embedded certificate bundle directly.
extern "C" esp_err_t esp_crt_bundle_attach(void *);
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <memory>
#include <new>
#include <time.h>
#include "../OtaCapability.h"
#include "SharedNetworkExecutor.h"
#include "../../../helpers/esp32/WdtHeavyGuard.h"
namespace {
using Jobs = ui::FirmwareUpdateJobs;
#if defined(LILYGO_TDECK)
const char board[] = "TDeck";
#else
const char board[] = "Heltec-V4-TFT";
#endif
bool ready(char *message, size_t capacity) {
  const char *error = nullptr;
  if (ui::platform::SharedNetworkExecutor::stackBytes() < 8 * 1024) error = "Update worker unavailable";
  else if (WiFi.status() != WL_CONNECTED) error = "Connect to Wi-Fi first";
  else if (time(nullptr) < 1700000000) error = "Waiting for clock sync";
  else if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 70 * 1024 ||
           heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 32 * 1024)
    error = "Not enough memory for HTTPS. Turn Bluetooth off first.";
  if (error) snprintf(message, capacity, "%s", error);
  return !error;
}
bool allowedUrl(const char *url) {
  for (auto prefix : {"https://api.github.com/", "https://github.com/",
                      "https://release-assets.githubusercontent.com/", "https://objects.githubusercontent.com/"})
    if (!strncmp(url, prefix, strlen(prefix))) return true;
  return false;
}
// Allocate this owner on the heap: GitHub's signed redirect URLs can be long.
class Http {
public:
  ~Http() { if (_client) esp_http_client_cleanup(_client); }
  bool open(const char *url, char *message, size_t capacity) {
    esp_http_client_config_t config{};
    config.url = url;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.disable_auto_redirect = true;
    config.timeout_ms = 12000;
    config.buffer_size = 1024;
    config.buffer_size_tx = 1024;
    config.event_handler = event;
    config.user_data = this;
    _client = esp_http_client_init(&config);
    if (!_client) return fail(message, capacity, "Not enough memory");
    esp_http_client_set_header(_client, "User-Agent", "GUARD-MESH-OTA");
    esp_http_client_set_header(_client, "Accept", "application/vnd.github+json");
    for (unsigned hop = 0; hop <= 5; ++hop) {
      _location[0] = 0;
      if (esp_http_client_open(_client, 0) != ESP_OK || esp_http_client_fetch_headers(_client) < 0)
        return fail(message, capacity, "GitHub connection failed");
      const int code = esp_http_client_get_status_code(_client);
      if (code == 200) return true;
      if ((code != 301 && code != 302 && code != 303 && code != 307 && code != 308) ||
          !_location[0] || !allowedUrl(_location) || hop == 5)
        return fail(message, capacity, code == 403 || code == 429 ? "GitHub rate limit. Try again later." : "GitHub download failed");
      esp_http_client_close(_client);
      if (esp_http_client_set_url(_client, _location) != ESP_OK)
        return fail(message, capacity, "GitHub download failed");
    }
    return false;
  }
  int read(void *buffer, size_t size) { return esp_http_client_read(_client, static_cast<char *>(buffer), size); }
  int length() const { return esp_http_client_is_chunked_response(_client) ? -1 : esp_http_client_get_content_length(_client); }
  bool complete() const { return esp_http_client_is_complete_data_received(_client); }
private:
  static bool fail(char *out, size_t capacity, const char *message) {
    snprintf(out, capacity, "%s", message); return false;
  }
  static esp_err_t event(esp_http_client_event_t *event) {
    auto *self = static_cast<Http *>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key &&
        !strcasecmp(event->header_key, "Location") && event->header_value) {
      const size_t length = strlen(event->header_value);
      if (length < sizeof self->_location) memcpy(self->_location, event->header_value, length + 1);
    }
    return ESP_OK;
  }
  esp_http_client_handle_t _client = nullptr;
  char _location[2048]{};
};
void resolve(void *, Jobs::CheckResult &result) {
  if (result.request.beta || !ready(result.message, sizeof result.message)) return;
  std::unique_ptr<Http> http(new (std::nothrow) Http);
  if (!http) { snprintf(result.message, sizeof result.message, "Not enough memory"); return; }
  if (!http->open(ui::firmwareUpdate::latestUrl, result.message, sizeof result.message)) return;
  constexpr size_t capacity = 65536;
  auto *buffer = static_cast<char *>(heap_caps_malloc(capacity + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buffer) { snprintf(result.message, sizeof result.message, "Not enough memory"); return; }
  size_t count = 0;
  const int length = http->length();
  bool ok = length <= int(capacity);
  const unsigned long started = millis();
  while (ok && count < capacity && millis() - started < 60000) {
    const int read = http->read(buffer + count, std::min(size_t(1024), capacity - count));
    if (read < 0) { ok = false; break; }
    if (!read) break;
    count += read;
    vTaskDelay(1);
  }
  ok = ok && http->complete() && (length < 0 || count == size_t(length));
  buffer[count] = 0;
  http.reset(); // Release TLS before the JSON document needs internal memory.
  if (ok) result.ok = ui::parseFirmwareRelease(buffer, count, board, result.release,
                                               result.message, sizeof result.message);
  else snprintf(result.message, sizeof result.message, "Incomplete GitHub response");
  heap_caps_free(buffer);
}
void install(void *, const Jobs::InstallRequest &request, Jobs::Progress progress, Jobs::InstallResult &result) {
  auto fail = [&](const char *message) { snprintf(result.message, sizeof result.message, "%s", message); };
  if (request.destination != Jobs::Destination::Ota || request.beta ||
      !ui::validFirmwareRelease(request.release, board)) { fail("Invalid firmware asset"); return; }
  if (!ui::platform::hasOtaUpdateSlot()) { fail("No compatible OTA partition. Use USB."); return; }
  const auto *partition = esp_ota_get_next_update_partition(nullptr);
  if (!partition || request.release.size > partition->size) { fail("Firmware does not fit the OTA partition"); return; }
  if (Update.isRunning()) { fail("Another firmware update is running"); return; }
  if (!ready(result.message, sizeof result.message)) return;
  std::unique_ptr<Http> http(new (std::nothrow) Http);
  if (!http) { fail("Not enough memory"); return; }
  if (!http->open(request.release.url, result.message, sizeof result.message)) return;
  if (http->length() != int(request.release.size)) { fail("Firmware size mismatch"); return; }
  // ESP32-S3 application descriptor distinguishes an app from a bootloader or
  // merged USB image even when both begin with the ESP image magic byte.
  uint8_t header[36];
  size_t received = 0;
  const unsigned long headerStarted = millis();
  while (received < sizeof header && millis() - headerStarted < 30000) {
    const int read = http->read(header + received, sizeof header - received);
    if (read <= 0) { fail("Firmware download interrupted"); return; }
    received += read;
  }
  if (received != sizeof header) { fail("Firmware download interrupted"); return; }
  if (header[0] != 0xe9 || !header[1] || header[1] > 16 || header[12] != 9 || header[13] ||
      header[32] != 0x32 || header[33] != 0x54 || header[34] != 0xcd || header[35] != 0xab) {
    fail("Invalid firmware image"); return;
  }
  auto *buffer = static_cast<uint8_t *>(heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!buffer) { fail("Not enough memory"); return; }
  esp_ota_handle_t handle = 0;
  esp_err_t error;
  {
    WdtHeavyGuard guard;
    error = esp_ota_begin(partition, request.release.size, &handle);
  }
  if (error != ESP_OK) { heap_caps_free(buffer); fail("Could not start OTA write"); return; }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  bool ok = mbedtls_sha256_starts_ret(&sha, 0) == 0 &&
            mbedtls_sha256_update_ret(&sha, header, sizeof header) == 0 &&
            esp_ota_write(handle, header, sizeof header) == ESP_OK;
  size_t count = sizeof header;
  const unsigned long started = millis();
  while (ok && count < request.release.size && millis() - started < 300000) {
    const int read = http->read(buffer, std::min(size_t(4096), size_t(request.release.size) - count));
    if (read <= 0) { ok = false; break; }
    ok = mbedtls_sha256_update_ret(&sha, buffer, read) == 0 && esp_ota_write(handle, buffer, read) == ESP_OK;
    count += read;
    if (progress.report) progress.report(progress.context, int(uint64_t(count) * 99 / request.release.size));
    vTaskDelay(1);
  }
  uint8_t digest[32];
  char hex[65];
  ok = ok && count == request.release.size && http->complete() && mbedtls_sha256_finish_ret(&sha, digest) == 0;
  if (ok) {
    for (unsigned i = 0; i < 32; ++i) snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    if (strcmp(hex, request.release.sha256)) { fail("Firmware SHA-256 mismatch"); ok = false; }
  }
  mbedtls_sha256_free(&sha);
  heap_caps_free(buffer);
  http.reset();
  if (!ok) {
    esp_ota_abort(handle);
    if (!result.message[0]) fail("Firmware download interrupted");
    return;
  }
  {
    WdtHeavyGuard guard;
    error = esp_ota_end(handle);
  }
  if (error != ESP_OK) { fail("Firmware validation failed"); return; }
  if (esp_ota_set_boot_partition(partition) != ESP_OK) { fail("Could not activate firmware"); return; }
  result.ok = true;
}
} // namespace
#endif
namespace ui { namespace platform {
bool runFirmwareCheck(FirmwareUpdateJobs &jobs, void *client, void *http) {
  (void)client; (void)http;
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && (defined(LILYGO_TDECK) || defined(HELTEC_LORA_V4_TFT))
  return jobs.runCheck({nullptr, nullptr, nullptr, resolve});
#else
  return jobs.runCheck({nullptr, nullptr, nullptr, [](void *, FirmwareUpdateJobs::CheckResult &result) {
    snprintf(result.message, sizeof result.message, "Wi-Fi OTA is unavailable in this build");
  }});
#endif
}
bool runFirmwareInstall(FirmwareUpdateJobs &jobs, void *client, void *http) {
  (void)client; (void)http;
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && (defined(LILYGO_TDECK) || defined(HELTEC_LORA_V4_TFT))
  return jobs.runInstall({nullptr, nullptr, install});
#else
  return jobs.runInstall({nullptr, nullptr, [](void *, const FirmwareUpdateJobs::InstallRequest &,
      FirmwareUpdateJobs::Progress, FirmwareUpdateJobs::InstallResult &result) {
    snprintf(result.message, sizeof result.message, "Wi-Fi OTA is unavailable in this build");
  }});
#endif
}
} } // namespace ui::platform

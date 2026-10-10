// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_8BIT = 2, MALLOC_CAP_SPIRAM = 4;
constexpr int WL_CONNECTED = 3;
unsigned long millis();
void vTaskDelay(unsigned);
inline void *xTaskGetCurrentTaskHandle() { return reinterpret_cast<void *>(uintptr_t(1)); }
void disableCore0WDT();
void enableCore0WDT();
size_t heap_caps_get_free_size(int);
size_t heap_caps_get_largest_free_block(int);
void *heap_caps_malloc(size_t, int);
void heap_caps_free(void *);
struct Wifi { int status() const; };
extern Wifi WiFi;
struct Updater { bool isRunning() const; };
extern Updater Update;
struct esp_partition_t { uint32_t size; };
using esp_ota_handle_t = unsigned;
const esp_partition_t *esp_ota_get_next_update_partition(void *);
int esp_ota_begin(const esp_partition_t *, size_t, esp_ota_handle_t *);
int esp_ota_write(esp_ota_handle_t, const void *, size_t);
int esp_ota_abort(esp_ota_handle_t);
int esp_ota_end(esp_ota_handle_t);
int esp_ota_set_boot_partition(const esp_partition_t *);
struct Client;
using esp_http_client_handle_t = Client *;
constexpr int HTTP_EVENT_ON_HEADER = 1;
struct esp_http_client_event_t { void *user_data; int event_id; char *header_key; char *header_value; };
struct esp_http_client_config_t {
  const char *url = nullptr;
  int (*crt_bundle_attach)(void *) = nullptr;
  bool disable_auto_redirect = false;
  bool skip_cert_common_name_check = false;
  int timeout_ms = 0, buffer_size = 0, buffer_size_tx = 0;
  int (*event_handler)(esp_http_client_event_t *) = nullptr;
  void *user_data = nullptr;
};
Client *esp_http_client_init(const esp_http_client_config_t *);
int esp_http_client_cleanup(Client *);
int esp_http_client_set_header(Client *, const char *, const char *);
int esp_http_client_open(Client *, int);
int esp_http_client_fetch_headers(Client *);
int esp_http_client_get_status_code(Client *);
int esp_http_client_close(Client *);
int esp_http_client_set_url(Client *, const char *);
int esp_http_client_read(Client *, char *, size_t);
int esp_http_client_get_content_length(Client *);
bool esp_http_client_is_complete_data_received(Client *);
bool esp_http_client_is_chunked_response(Client *);
extern "C" int esp_crt_bundle_attach(void *);
struct mbedtls_sha256_context { size_t bytes; };
void mbedtls_sha256_init(mbedtls_sha256_context *);
void mbedtls_sha256_free(mbedtls_sha256_context *);
int mbedtls_sha256_starts_ret(mbedtls_sha256_context *, int);
int mbedtls_sha256_update_ret(mbedtls_sha256_context *, const uint8_t *, size_t);
int mbedtls_sha256_finish_ret(mbedtls_sha256_context *, uint8_t *);

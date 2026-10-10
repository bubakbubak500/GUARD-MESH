// SPDX-License-Identifier: GPL-3.0-or-later
#include "FakeRuntime.h"
#include "ui-touch/services/FirmwareUpdateJobs.h"
#include "ui-touch/services/FirmwareUpdateSource.h"
#include "ui-touch/platform/esp32/FirmwareUpdateTransport.h"
#include "ui-touch/platform/esp32/SharedNetworkExecutor.h"
#include <algorithm>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <iterator>
namespace {
using Jobs = ui::FirmwareUpdateJobs;
struct Response {
  std::string body, location;
  int code = 200, length = 0;
  size_t stop = SIZE_MAX;
  bool chunked = false, tlsFail = false;
};
std::vector<Response> responses;
unsigned nextResponse = 0, active = 0, opens = 0, begins = 0, writes = 0, aborts = 0, ends = 0, boots = 0;
unsigned ticks = 0, wdtDepth = 0;
size_t freeHeap = 100 * 1024, largest = 64 * 1024, hashed = 0, written = 0;
size_t stackBytes = 8192;
bool connected = true, slot = true, psram = true, another = false, writeOk = true, endOk = true, activateOk = true;
esp_partition_t partition{0x3e0000};
void reset() {
  assert(!active && !wdtDepth);
  responses.clear(); nextResponse = opens = begins = writes = aborts = ends = boots = ticks = 0;
  freeHeap = 100 * 1024; largest = 64 * 1024; hashed = written = 0;
  stackBytes = 8192;
  connected = slot = psram = writeOk = endOk = activateOk = true; another = false;
}
ui::FirmwareRelease release() {
  ui::FirmwareRelease value;
  strcpy(value.tag, "guardian-2026.10.10.2");
  strcpy(value.url, "https://github.com/bubakbubak500/GUARD-MESH/releases/download/guardian-2026.10.10.2/Guard-Mesh-TDeck-guardian-2026.10.10.2-app-ota.bin");
  for (unsigned i = 0; i < 32; ++i) memcpy(value.sha256 + i * 2, "5a", 2);
  value.size = 4232;
  return value;
}
Response image() {
  Response response; response.body.assign(4232, 'x');
  response.body[0] = char(0xe9); response.body[1] = 4; response.body[12] = 9; response.body[13] = 0;
  response.body[32] = 0x32; response.body[33] = 0x54; response.body[34] = char(0xcd); response.body[35] = char(0xab);
  response.length = 4232; return response;
}
Jobs::InstallResult install(ui::FirmwareRelease value = release()) {
  Jobs jobs; assert(jobs.requestInstall(value));
  assert(ui::platform::runFirmwareInstall(jobs, nullptr, nullptr));
  Jobs::InstallResult result;
  assert(jobs.takeInstall(Jobs::Destination::Ota, result));
  assert(!active && !wdtDepth);
  assert(jobs.progress() == (result.ok ? 100 : jobs.progress()));
  return result;
}
void installs() {
  reset(); responses.push_back(image());
  assert(install().ok && begins == 1 && writes == 3 && ends == 1 && boots == 1 && !aborts && written == 4232 && hashed == 4232);
  reset(); Response redirect; redirect.code = 302; redirect.location = "https://release-assets.githubusercontent.com/a/signed?token=test";
  responses = {redirect, image()}; assert(install().ok && opens == 2 && boots == 1);
  reset(); redirect.location = "http://release-assets.githubusercontent.com/unsafe"; responses = {redirect};
  assert(!install().ok && !begins && !boots);
  reset(); redirect.location = "https://evil.test/firmware.bin"; responses = {redirect};
  assert(!install().ok && !begins && !boots);
  reset(); Response shortRead = image(); shortRead.stop = 4096; responses = {shortRead};
  assert(!install().ok && begins == 1 && aborts == 1 && !ends && !boots);
  reset(); auto value = release(); value.sha256[0] = '0'; responses = {image()};
  assert(!install(value).ok && aborts == 1 && !ends && !boots);
  reset(); auto merged = image(); merged.body[0] = 0; responses = {merged};
  assert(!install().ok && !begins && !writes && !boots);
  reset(); merged = image(); merged.body[32] = 0; responses = {merged};
  assert(!install().ok && !begins && !boots);
  reset(); auto wrongSize = image(); --wrongSize.length; responses = {wrongSize};
  assert(!install().ok && !begins && !boots);
  reset(); auto failedTls = image(); failedTls.tlsFail = true; responses = {failedTls};
  assert(!install().ok && !begins && !boots);
  reset(); freeHeap = 60 * 1024; assert(!install().ok && !opens && !begins);
  reset(); stackBytes = 6144; assert(!install().ok && !opens && !begins);
  reset(); largest = 16 * 1024; assert(!install().ok && !opens);
  reset(); slot = false; assert(!install().ok && !opens);
  reset(); connected = false; assert(!install().ok && !opens);
  reset(); another = true; assert(!install().ok && !opens);
  reset(); value = release(); value.size = partition.size + 1; assert(!install(value).ok && !opens);
  reset(); responses = {image()}; writeOk = false;
  assert(!install().ok && aborts == 1 && !boots);
  reset(); responses = {image()}; endOk = false;
  assert(!install().ok && ends == 1 && !boots);
  reset(); responses = {image()}; activateOk = false;
  assert(!install().ok && ends == 1 && boots == 1);
}
void checks() {
  reset(); Response response;
  auto value = release();
  response.body = std::string("{\"tag_name\":\"") + value.tag + "\",\"draft\":false,\"prerelease\":false,\"assets\":[{\"name\":\"Guard-Mesh-TDeck-guardian-2026.10.10.2-app-ota.bin\",\"state\":\"uploaded\",\"size\":4232,\"digest\":\"sha256:" + value.sha256 + "\",\"browser_download_url\":\"" + value.url + "\"}]}";
  response.length = int(response.body.size());
  for (unsigned scenario = 0; scenario < 4; ++scenario) {
    reset(); auto reply = response;
    if (scenario == 1) reply.chunked = true;
    if (scenario == 2) reply.stop = 20;
    if (scenario == 3) psram = false;
    responses = {reply}; Jobs jobs;
    assert(jobs.requestCheck(false, 55));
    assert(ui::platform::runFirmwareCheck(jobs, nullptr, nullptr));
    Jobs::CheckResult result; assert(jobs.takeCheck(result));
    assert(result.request.generation == 55 && result.ok == (scenario < 2));
    assert(!active && !begins && !boots);
  }
}
} // namespace
struct Client { esp_http_client_config_t config; std::string url; Response response; size_t offset = 0; };
Wifi WiFi; Updater Update;
int Wifi::status() const { return connected ? WL_CONNECTED : 0; }
bool Updater::isRunning() const { return another; }
unsigned long millis() { return ticks; }
void vTaskDelay(unsigned value) { ticks += value; }
void disableCore0WDT() { ++wdtDepth; }
void enableCore0WDT() { assert(wdtDepth); --wdtDepth; }
size_t heap_caps_get_free_size(int) { return freeHeap; }
size_t heap_caps_get_largest_free_block(int) { return largest; }
void *heap_caps_malloc(size_t bytes, int caps) { return caps & MALLOC_CAP_SPIRAM && !psram ? nullptr : std::malloc(bytes); }
void heap_caps_free(void *p) { std::free(p); }
Client *esp_http_client_init(const esp_http_client_config_t *config) {
  assert(config->crt_bundle_attach == esp_crt_bundle_attach && !config->skip_cert_common_name_check && config->disable_auto_redirect);
  auto *client = new Client; client->config = *config; client->url = config->url; ++active; return client;
}
int esp_http_client_cleanup(Client *client) { --active; delete client; return ESP_OK; }
int esp_http_client_set_header(Client *, const char *, const char *) { return ESP_OK; }
int esp_http_client_open(Client *client, int) {
  ++opens; assert(nextResponse < responses.size()); client->response = responses[nextResponse++]; client->offset = 0;
  if (!client->response.location.empty()) {
    char name[] = "Location";
    esp_http_client_event_t event{client->config.user_data, HTTP_EVENT_ON_HEADER, name, &client->response.location[0]};
    client->config.event_handler(&event);
  }
  return client->response.tlsFail ? -1 : ESP_OK;
}
int esp_http_client_fetch_headers(Client *client) { return client->response.chunked ? 0 : client->response.length; }
int esp_http_client_get_status_code(Client *client) { return client->response.code; }
int esp_http_client_close(Client *) { return ESP_OK; }
int esp_http_client_set_url(Client *client, const char *url) { client->url = url; return ESP_OK; }
int esp_http_client_read(Client *client, char *buffer, size_t bytes) {
  if (client->offset >= client->response.stop) return -1;
  const size_t count = std::min(bytes, std::min(client->response.body.size(), client->response.stop) - client->offset);
  memcpy(buffer, client->response.body.data() + client->offset, count); client->offset += count; return int(count);
}
int esp_http_client_get_content_length(Client *client) { return client->response.chunked ? 0 : client->response.length; }
bool esp_http_client_is_complete_data_received(Client *client) { return client->offset == client->response.body.size(); }
bool esp_http_client_is_chunked_response(Client *client) { return client->response.chunked; }
extern "C" int esp_crt_bundle_attach(void *) { return ESP_OK; }
const esp_partition_t *esp_ota_get_next_update_partition(void *) { return &partition; }
int esp_ota_begin(const esp_partition_t *, size_t, esp_ota_handle_t *handle) { ++begins; *handle = 1; return ESP_OK; }
int esp_ota_write(esp_ota_handle_t, const void *, size_t count) { ++writes; written += count; return writeOk ? ESP_OK : -1; }
int esp_ota_abort(esp_ota_handle_t) { ++aborts; return ESP_OK; }
int esp_ota_end(esp_ota_handle_t) { ++ends; return endOk ? ESP_OK : -1; }
int esp_ota_set_boot_partition(const esp_partition_t *) { ++boots; return activateOk ? ESP_OK : -1; }
namespace ui { namespace platform {
bool hasOtaUpdateSlot() { return slot; }
size_t SharedNetworkExecutor::stackBytes() { return ::stackBytes; }
} }
void mbedtls_sha256_init(mbedtls_sha256_context *c) { c->bytes = 0; }
void mbedtls_sha256_free(mbedtls_sha256_context *) {}
int mbedtls_sha256_starts_ret(mbedtls_sha256_context *, int) { return 0; }
int mbedtls_sha256_update_ret(mbedtls_sha256_context *c, const uint8_t *, size_t count) { c->bytes += count; return 0; }
int mbedtls_sha256_finish_ret(mbedtls_sha256_context *c, uint8_t *out) { hashed = c->bytes; memset(out, 0x5a, 32); return 0; }
int main(int argc, char **argv) {
  installs(); checks();
  if (argc == 2) {
    std::ifstream file(argv[1], std::ios::binary);
    std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    ui::FirmwareRelease resolved; char error[80];
    assert(ui::parseFirmwareRelease(json.data(), json.size(), "TDeck", resolved, error, sizeof error));
    printf("Live GitHub metadata fixture: %s, %u bytes, exact board asset and SHA-256 validated.\n", resolved.tag, resolved.size);
  }
  puts("ESP32 OTA transport: TLS, redirects, streaming, digest, errors, admission and chunked metadata passed.");
}

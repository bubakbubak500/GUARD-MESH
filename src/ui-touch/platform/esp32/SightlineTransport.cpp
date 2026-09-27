// SPDX-License-Identifier: GPL-3.0-or-later
#include "SightlineTransport.h"
#include "../../device_caps.h"
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
#include "../../services/SightlineJob.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#if defined(HAS_TDISPLAY_P4)
#include <C6Socket.h>
#include <C6WifiShim.h>
#define WiFiClient C6Client
#endif
#include <cstring>
namespace ui {
namespace platform {
namespace {
// HTTPClient decodes chunk framing into this capped sink. A truncated body is
// never treated as a sparse profile; the request fails and can be retried.
class Body : public Stream {
public:
  explicit Body(char *storage) : data(storage), started(millis()) { data[0] = 0; }
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t *values, size_t count) override {
    if (millis() - started >= 12000 || count > 1023 - used) {
      failed = true;
      return 0;
    }
    std::memcpy(data + used, values, count);
    used += count;
    data[used] = 0;
    return count;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  char *data;
  uint32_t started;
  size_t used = 0;
  bool failed = false;
};
struct Context {
  WiFiClient &client;
  HTTPClient &http;
};
int fetch(void *value, const SightlineJob::Request &request, float *heights, int &code) {
  auto &context = *static_cast<Context *>(value);
  auto &http = context.http;
  if (WiFi.status() != WL_CONNECTED) {
    code = -1;
    return 0;
  }
  // One allocation per attempt, prefer PSRAM and release before leaving worker.
  char *storage = static_cast<char *>(heap_caps_malloc(2124, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!storage)
    storage = static_cast<char *>(heap_caps_malloc(2124, MALLOC_CAP_8BIT));
  if (!storage) {
    code = -3;
    return 0;
  }
  int parsed = 0;
  if (sightline::sampleUrl(request.path, request.server, storage, 1100)) {
    http.setReuse(false);
    http.setConnectTimeout(5000);
    http.setTimeout(9000);
    if (http.begin(context.client, storage)) {
      code = http.GET();
      if (code == HTTP_CODE_OK) {
        Body body(storage + 1100);
        const int size = http.getSize();
        if (size <= 1023) {
          const int written = http.writeToStream(&body);
          if (written >= 0 && !body.failed && (size < 0 || size == written))
            parsed = sightline::parseElevations(body.data, heights, sightline::Samples);
          else
            code = -4;
        } else
          code = -4;
      }
    } else
      code = -2;
  } else
    code = -5;
  http.end();
  context.client.stop();
  heap_caps_free(storage);
  return parsed;
}
} // namespace
bool runSightlineJob(SightlineJob &job, void *client, void *http) {
  Context context{*static_cast<WiFiClient *>(client), *static_cast<HTTPClient *>(http)};
  const bool ran = job.run({&context, fetch, [](void *) { vTaskDelay(pdMS_TO_TICKS(900)); }});
  if (ran) {
    context.http.setConnectTimeout(8000);
    context.http.setTimeout(15000);
  }
  return ran;
}
} // namespace platform
} // namespace ui
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
#include "TileFetchTransport.h"
#include "../../device_caps.h"

#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdio>
#include <cstring>
#if defined(HAS_TDISPLAY_P4)
#include <C6Socket.h>
#include <C6WifiShim.h>
#define WiFiClient C6Client
#endif

namespace ui { namespace platform {
namespace {
using Executor = SharedNetworkExecutor;

bool terminated(const char *text, size_t capacity) {
  for (size_t i = 0; i < capacity; ++i)
    if (text[i] == '\0') return true;
  return false;
}

bool formatted(int count, size_t capacity) {
  return count >= 0 && static_cast<size_t>(count) < capacity;
}

struct Cache {
  const TileFetchBackend &backend;
  const TileFetchHost &host;

  bool path(char *out, size_t capacity, const char *relative) const {
    return formatted(snprintf(out, capacity, "%s%s", backend.prefix, relative), capacity);
  }
  void mark() const {
    if (backend.markStorageIo) host.markSdIo(host.context);
  }
  File open(const char *relative, const char *mode) const {
    if (!backend.filesystem) return File();
    char full[80];
    if (!path(full, sizeof(full), relative)) return File();
    mark();
    return backend.filesystem->open(full, mode);
  }
  void remove(const char *relative) const {
    if (!backend.filesystem) return;
    char full[80];
    if (!path(full, sizeof(full), relative)) return;
    mark();
    backend.filesystem->remove(full);
  }
  bool mkdir(const char *relative) const {
    if (!backend.filesystem) return false;
    char full[80];
    if (!path(full, sizeof(full), relative)) return false;
    mark();
    backend.filesystem->mkdir(full); // Existing directories are expected.
    return true;
  }
};

bool ensureDirectories(const Cache &cache, uint8_t zoom, int32_t x, const char *root) {
  char path[48];
  if (!cache.mkdir("/tiles") || !cache.mkdir(root)) return false;
  if (!formatted(snprintf(path, sizeof(path), "%s/%u", root, unsigned(zoom)), sizeof(path)) ||
      !cache.mkdir(path)) return false;
  if (!formatted(snprintf(path, sizeof(path), "%s/%u/%ld", root, unsigned(zoom), long(x)), sizeof(path)))
    return false;
  return cache.mkdir(path);
}

struct Request {
  const Executor::TileWork &work;
  WiFiClient &client;
  HTTPClient &http;
  const TileFetchHost &host;
};

TileFetchResult underLease(void *opaque) {
  Request &request = *static_cast<Request *>(opaque);
  const Executor::TileKey &key = request.work.key;
  const TileFetchHost &host = request.host;
  TileFetchResult result;
  result.zoom = key.zoom;
  TileFetchBackend backend;
  if (!host.captureBackend(host.context, &backend) ||
      !terminated(backend.prefix, sizeof(backend.prefix)) ||
      !terminated(backend.server, sizeof(backend.server))) {
    Executor::noteFailure();
    return result;
  }
  const Cache cache{backend, host};
  const char *root = backend.style == 1 ? "/tiles/topo" : "/tiles";
  const char *segment = backend.style == 1 ? "/opentopo" : "";
  char jpg[48], png[48];
  if (!formatted(snprintf(jpg, sizeof(jpg), "%s/%u/%ld/%ld.jpg", root,
                          unsigned(key.zoom), long(key.x), long(key.y)), sizeof(jpg)) ||
      !formatted(snprintf(png, sizeof(png), "%s/%u/%ld/%ld.png", root,
                          unsigned(key.zoom), long(key.x), long(key.y)), sizeof(png))) {
    Executor::noteWrite('O');
    Executor::noteFailure();
    return result;
  }
  // FFat can report false for exists() and zero for size(); inspect the file.
  {
    File cached = cache.open(jpg, "r");
    if (cached) {
      uint8_t magic[3] = {0, 0, 0};
      const int count = cached.read(magic, sizeof(magic));
      cached.close();
      if (count == 3 && magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF) {
        Executor::noteSuccess();
        result.completion = Executor::Completion(Executor::Outcome::Success);
        result.publish = true;
        result.cacheHit = true;
        return result;
      }
      cache.remove(jpg);
    }
  }
  // Old PNGs belong to the firmware's private cache only. On an SD-backed
  // cache they may be the user's offline map pack.
  if (backend.ownLittleFs) cache.remove(png);

  int waits = 0;
  while (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 18 * 1024 && waits < 24) {
    Executor::noteStep('h');
    vTaskDelay(pdMS_TO_TICKS(150));
    ++waits;
  }
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 12 * 1024) {
    Executor::noteWrite('H');
    Executor::noteFailure();
    return result;
  }
  if (host.lowSpace(host.context)) {
    Executor::noteWrite('S');
    Executor::noteFailure();
    return result;
  }
  if (!ensureDirectories(cache, key.zoom, key.x, root)) {
    Executor::noteWrite('O');
    Executor::noteFailure();
    return result;
  }

  size_t baseLength = strlen(backend.server);
  while (baseLength && backend.server[baseLength - 1] == '/') backend.server[--baseLength] = '\0';
  char url[160];
  if (!formatted(snprintf(url, sizeof(url), "%s%s/%u/%ld/%ld.jpg", backend.server,
                          segment, unsigned(key.zoom), long(key.x), long(key.y)), sizeof(url))) {
    Executor::noteWrite('O');
    Executor::noteFailure();
    return result;
  }
  request.http.begin(request.client, url);
  request.http.setConnectTimeout(3000);
  request.http.setTimeout(2000);
  request.http.addHeader("User-Agent", "wadamesh-touch/0.4 (https://github.com/ALLFATHER-BV/wadamesh)");
  Executor::noteStep('g');
  const int code = request.http.GET();
  Executor::noteHttpCode(static_cast<int16_t>(code));
  bool wrote = false;
  if (code != HTTP_CODE_OK) {
    Executor::noteStep('d');
    Executor::noteWrite('e');
  }
  if (code == HTTP_CODE_OK && host.lateCardAllowed(host.context, backend.cardBackend)) {
    Executor::noteStep('r');
    const int contentLength = request.http.getSize();
    if (contentLength > 0 && contentLength <= 100 * 1024) {
      File output = cache.open(jpg, "w");
      if (output) {
        auto *stream = request.http.getStreamPtr();
        uint8_t buffer[1024];
        int remaining = contentLength;
        size_t written = 0;
        bool diskError = false;
        bool badContent = false;
        const uint32_t deadline = millis() + 12000;
        while (remaining > 0 && request.http.connected()) {
          const size_t want = remaining > int(sizeof(buffer)) ? sizeof(buffer) : size_t(remaining);
          const int count = stream->readBytes(buffer, want);
          if (count <= 0) break;
          // Preserve the original two-byte proxy-body gate. A later cache
          // read requires the stricter three-byte JPEG prefix.
          if (written == 0 && (count < 2 || buffer[0] != 0xFF || buffer[1] != 0xD8)) {
            badContent = true;
            break;
          }
          const size_t committed = output.write(buffer, count);
          written += committed;
          if (committed != size_t(count)) { diskError = true; break; }
          remaining -= count;
          vTaskDelay(1);
          if (int32_t(millis() - deadline) > 0) break;
        }
        output.close();
        if (!diskError && !badContent && remaining == 0 && written == size_t(contentLength)) {
          wrote = true;
          host.noteWritten(host.context, written);
        } else {
          cache.remove(jpg);
          if (badContent) {
            Executor::noteWrite('J');
          } else {
            Executor::noteShortWrite();
            Executor::noteWrite('P');
            if (backend.cardBackend) host.cardWriteFailed(host.context);
          }
        }
      } else {
        Executor::noteOpenFailure();
        Executor::noteWrite('O');
        if (backend.cardBackend) host.cardWriteFailed(host.context);
      }
    } else {
      Executor::noteWrite('Z');
    }
  }
  // The executor completes the ledger and paces only after both resources
  // have closed. withBackendLease releases the backend immediately after this.
  request.http.end();
  request.client.stop();
  const bool permanent = code >= 400 && code < 500 && code != 408 && code != 429;
  if (wrote) {
    Executor::noteStep('w');
    Executor::noteWrite('w');
    Executor::noteSuccess();
    result.completion = Executor::Completion(Executor::Outcome::Success, 500);
    result.publish = true;
  } else {
    Executor::noteFailure();
    result.completion = Executor::Completion(
        permanent ? Executor::Outcome::PermanentFailure : Executor::Outcome::RetryableFailure, 500);
  }
  return result;
}
} // namespace

Executor::Completion TileFetchTransport::run(
    const Executor::TileWork &work, void *client, void *http, const TileFetchHost &host) {
  if (!client || !http || !host.withBackendLease || !host.captureBackend || !host.lowSpace ||
      !host.noteWritten || !host.markSdIo || !host.lateCardAllowed ||
      !host.cardWriteFailed || !host.publishVisible) {
    Executor::noteFailure();
    return Executor::Completion();
  }
  if (WiFi.status() != WL_CONNECTED) {
    Executor::noteStep('!');
    Executor::noteWrite('W');
    Executor::noteFailure();
    return Executor::Completion();
  }
  Request request{work, *static_cast<WiFiClient *>(client), *static_cast<HTTPClient *>(http), host};
  const TileFetchResult result = host.withBackendLease(host.context, underLease, &request);
  if (result.publish) host.publishVisible(host.context, result.zoom, result.cacheHit);
  return result.completion;
}

} } // namespace ui::platform
#endif

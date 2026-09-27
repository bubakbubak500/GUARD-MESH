// SPDX-License-Identifier: GPL-3.0-or-later
// Deterministic fake runtime for the actual TileFetchTransport implementation.
#include "ui-touch/platform/esp32/TileFetchTransport.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/task.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using Executor = ui::platform::SharedNetworkExecutor;
using Transport = ui::platform::TileFetchTransport;
using Backend = ui::platform::TileFetchBackend;
using Host = ui::platform::TileFetchHost;
using Result = ui::platform::TileFetchResult;

namespace fake {
std::vector<std::string> events;
uint32_t clockMs = 1;
uint32_t delayAdvance = 0;
size_t heapFree = 64 * 1024;
bool lease = false;
Executor::Diagnostics diagnostics;
void event(const char *name) { events.push_back(name); }
size_t index(const char *name) {
  const auto it = std::find(events.begin(), events.end(), name);
  assert(it != events.end());
  return size_t(it - events.begin());
}
void reset() {
  events.clear();
  clockMs = 1;
  delayAdvance = 0;
  heapFree = 64 * 1024;
  lease = false;
  diagnostics = Executor::Diagnostics{};
  WiFi.connection = WL_CONNECTED;
}
}

FakeWiFi WiFi;
uint32_t millis() { return fake::clockMs; }
void vTaskDelay(unsigned ticks) {
  fake::event("delay");
  fake::clockMs += fake::delayAdvance ? fake::delayAdvance : ticks;
}
size_t heap_caps_get_free_size(unsigned caps) {
  assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  return fake::heapFree;
}
void WiFiClient::stop() { stopped = true; fake::event("client.stop"); }

File::File(fs::FS *owner, const char *path, bool writing)
  : _owner(owner), _path(path), _writing(writing), _open(true) { ++_owner->openCount; }
int File::read(uint8_t *buffer, size_t count) {
  assert(_open && !_writing);
  const std::vector<uint8_t> &data = _owner->files[_path];
  const size_t amount = std::min(count, std::min(_owner->readLimit, data.size() - _position));
  std::copy(data.begin() + _position, data.begin() + _position + amount, buffer);
  _position += amount;
  return int(amount);
}
size_t File::write(const uint8_t *buffer, size_t count) {
  assert(_open && _writing);
  const size_t amount = std::min(count, _owner->writeLimit);
  std::vector<uint8_t> &data = _owner->files[_path];
  data.insert(data.end(), buffer, buffer + amount);
  fake::event("file.write");
  return amount;
}
void File::close() {
  if (!_open) return;
  _open = false;
  --_owner->openCount;
  fake::event("file.close");
}
File fs::FS::open(const char *path, const char *mode) {
  const bool writing = mode[0] == 'w';
  fake::event(writing ? "file.open.w" : "file.open.r");
  if ((writing && failWriteOpen) || (!writing && (failReadOpen || files.count(path) == 0)))
    return File();
  if (writing) files[path].clear();
  return File(this, path, writing);
}
bool fs::FS::remove(const char *path) {
  fake::event("file.remove");
  removed.push_back(path);
  return files.erase(path) != 0;
}
bool fs::FS::mkdir(const char *path) {
  fake::event("mkdir");
  directories.push_back(path);
  return true;
}

int FakeStream::readBytes(uint8_t *out, size_t count) {
  fake::event("net.read");
  const size_t amount = std::min(count, std::min(readLimit, body.size() - position));
  std::copy(body.begin() + position, body.begin() + position + amount, out);
  position += amount;
  return int(amount);
}
bool HTTPClient::begin(WiFiClient &, const char *value) {
  begun = true;
  url = value;
  fake::event("http.begin");
  return true;
}
void HTTPClient::addHeader(const char *name, const char *value) {
  assert(std::string(name) == "User-Agent");
  userAgent = value;
}
int HTTPClient::GET() {
  fake::event("http.GET");
  return responseCode;
}
void HTTPClient::end() { ended = true; fake::event("http.end"); }

namespace ui { namespace platform {
void SharedNetworkExecutor::noteStep(char value) { fake::diagnostics.step = value; }
void SharedNetworkExecutor::noteHttpCode(int16_t value) { fake::diagnostics.lastHttpCode = value; }
void SharedNetworkExecutor::noteWrite(char value) { fake::diagnostics.lastWrite = value; }
void SharedNetworkExecutor::noteOpenFailure() { ++fake::diagnostics.openFailures; }
void SharedNetworkExecutor::noteShortWrite() { ++fake::diagnostics.shortWrites; }
void SharedNetworkExecutor::noteSuccess() { ++fake::diagnostics.ok; }
void SharedNetworkExecutor::noteFailure() { ++fake::diagnostics.failed; }
} }

struct Env {
  fs::FS filesystem;
  Backend snapshot;
  HTTPClient http;
  WiFiClient client;
  bool low = false;
  bool lateAllowed = true;
  bool cacheHitDirty = false;
  unsigned publishCalls = 0;
  unsigned dirtyCalls = 0;
  size_t bytesWritten = 0;
  unsigned sdMarks = 0;
  unsigned cardFailures = 0;
  unsigned lateCalls = 0;
  Env() {
    snapshot.filesystem = &filesystem;
    std::strcpy(snapshot.server, "http://tiles.example/");
  }
  Host host() {
    Host value;
    value.context = this;
    value.withBackendLease = [](void *context, Result (*run)(void *), void *argument) {
      Env &env = *static_cast<Env *>(context);
      assert(!fake::lease);
      fake::lease = true;
      fake::event("lease.enter");
      Result result = run(argument);
      assert(env.filesystem.openCount == 0);
      if (env.http.begun) assert(env.http.ended && env.client.stopped);
      fake::lease = false;
      fake::event("lease.leave");
      return result;
    };
    value.captureBackend = [](void *context, Backend *out) {
      assert(fake::lease);
      *out = static_cast<Env *>(context)->snapshot;
      fake::event("capture");
      return true;
    };
    value.lowSpace = [](void *context) { return static_cast<Env *>(context)->low; };
    value.noteWritten = [](void *context, size_t count) {
      static_cast<Env *>(context)->bytesWritten += count;
    };
    value.markSdIo = [](void *context) { ++static_cast<Env *>(context)->sdMarks; };
    value.lateCardAllowed = [](void *context, bool) {
      Env &env = *static_cast<Env *>(context);
      assert(fake::lease && env.http.begun);
      ++env.lateCalls;
      fake::event("late.card");
      return env.lateAllowed;
    };
    value.cardWriteFailed = [](void *context) {
      ++static_cast<Env *>(context)->cardFailures;
    };
    value.publishVisible = [](void *context, uint8_t zoom, bool cacheHit) {
      Env &env = *static_cast<Env *>(context);
      assert(!fake::lease && env.filesystem.openCount == 0);
      assert(!env.http.begun || (env.http.ended && env.client.stopped));
      assert(zoom == 2);
      ++env.publishCalls;
      if (!cacheHit || env.cacheHitDirty) ++env.dirtyCalls;
      fake::event("publish");
    };
    return value;
  }
};

const char *kJpg = "/tiles/2/3/4.jpg";
const char *kPng = "/tiles/2/3/4.png";
std::vector<uint8_t> jpeg() { return {0xFF, 0xD8, 0xFF, 0x11, 0x22}; }
Executor::Completion run(Env &env) {
  Executor::TileWork work{};
  work.key = Executor::TileKey(2, 3, 4);
  return Transport::run(work, &env.client, &env.http, env.host());
}
void expectOutcome(const Executor::Completion &value, Executor::Outcome outcome, unsigned pacing) {
  assert(value.outcome == outcome && value.pacingMs == pacing);
}

void cachedHit() {
  fake::reset(); Env env;
  env.filesystem.files[kJpg] = jpeg();
  expectOutcome(run(env), Executor::Outcome::Success, 0);
  assert(!env.http.begun && env.publishCalls == 1 && env.dirtyCalls == 0);
  assert(fake::diagnostics.ok == 1 && env.filesystem.openCount == 0);
  assert(fake::index("file.close") < fake::index("lease.leave"));
  assert(fake::index("lease.leave") < fake::index("publish"));
  fake::reset(); Env m9;
  m9.filesystem.files[kJpg] = jpeg(); m9.cacheHitDirty = true;
  run(m9); assert(m9.dirtyCalls == 1);
}

void corruptAndPngProtection() {
  for (int own = 0; own != 2; ++own) {
    fake::reset(); Env env;
    env.snapshot.ownLittleFs = own != 0;
    env.snapshot.markStorageIo = !env.snapshot.ownLittleFs;
    env.filesystem.files[kJpg] = {0x01, 0x02, 0x03};
    env.filesystem.files[kPng] = {0x89, 0x50};
    env.http.stream.body = jpeg();
    expectOutcome(run(env), Executor::Outcome::Success, 500);
    assert(env.filesystem.files[kJpg] == jpeg());
    assert(env.filesystem.files.count(kPng) == unsigned(own == 0));
    assert(env.sdMarks > 0 == (own == 0));
    assert(env.bytesWritten == jpeg().size() && env.dirtyCalls == 1);
    assert(fake::index("file.close") < fake::index("http.end"));
    assert(fake::index("http.end") < fake::index("client.stop"));
    assert(fake::index("client.stop") < fake::index("lease.leave"));
    assert(fake::index("lease.leave") < fake::index("publish"));
    assert(env.http.connectTimeout == 3000 && env.http.timeout == 2000);
    assert(env.http.userAgent.find("wadamesh-touch") != std::string::npos);
  }
}

void writeAndBodyFailures() {
  fake::reset(); Env partial;
  partial.snapshot.cardBackend = true;
  partial.filesystem.writeLimit = 2;
  partial.http.stream.body = jpeg();
  expectOutcome(run(partial), Executor::Outcome::RetryableFailure, 500);
  assert(partial.filesystem.files.count(kJpg) == 0 && partial.cardFailures == 1);
  assert(fake::diagnostics.shortWrites == 1 && fake::diagnostics.lastWrite == 'P');

  fake::reset(); Env shortRead;
  shortRead.http.stream.body = jpeg(); shortRead.http.stream.readLimit = 0;
  expectOutcome(run(shortRead), Executor::Outcome::RetryableFailure, 500);
  assert(shortRead.filesystem.files.count(kJpg) == 0 && fake::diagnostics.shortWrites == 1);

  fake::reset(); Env invalid;
  invalid.http.stream.body = {'<', 'h', 't', 'm', 'l'};
  expectOutcome(run(invalid), Executor::Outcome::RetryableFailure, 500);
  assert(invalid.filesystem.files.count(kJpg) == 0 && fake::diagnostics.lastWrite == 'J');

  fake::reset(); Env openFail;
  openFail.snapshot.cardBackend = true; openFail.filesystem.failWriteOpen = true;
  openFail.http.stream.body = jpeg();
  expectOutcome(run(openFail), Executor::Outcome::RetryableFailure, 500);
  assert(openFail.cardFailures == 1 && fake::diagnostics.openFailures == 1);

  fake::reset(); Env deadline;
  deadline.http.stream.body.resize(2048, 0x11);
  deadline.http.stream.body[0] = 0xFF; deadline.http.stream.body[1] = 0xD8;
  fake::delayAdvance = 13000;
  expectOutcome(run(deadline), Executor::Outcome::RetryableFailure, 500);
  assert(deadline.filesystem.files.count(kJpg) == 0 && fake::diagnostics.shortWrites == 1);
}

void existingTwoByteBodyPolicy() {
  fake::reset(); Env first;
  first.http.stream.body = {0xFF, 0xD8, 0x00};
  expectOutcome(run(first), Executor::Outcome::Success, 500);
  assert(first.filesystem.files[kJpg] == first.http.stream.body);
  // A subsequent cache read requires FF D8 FF and rejects that successful
  // download. The transport deliberately retains this pre-existing mismatch.
  fake::reset(); Env second;
  second.filesystem.files[kJpg] = first.filesystem.files[kJpg];
  second.http.stream.body = jpeg();
  expectOutcome(run(second), Executor::Outcome::Success, 500);
  assert(fake::index("file.remove") < fake::index("http.GET"));
  assert(second.filesystem.files[kJpg] == jpeg());
}

void sizeAndHttpPolicy() {
  for (int size : {-1, 0, 100 * 1024 + 1}) {
    fake::reset(); Env env;
    env.http.stream.body = jpeg(); env.http.declaredSize = size;
    expectOutcome(run(env), Executor::Outcome::RetryableFailure, 500);
    assert(env.filesystem.files.count(kJpg) == 0 && fake::diagnostics.lastWrite == 'Z');
  }
  fake::reset(); Env maximum;
  maximum.http.stream.body.resize(100 * 1024, 0x11);
  maximum.http.stream.body[0] = 0xFF; maximum.http.stream.body[1] = 0xD8;
  expectOutcome(run(maximum), Executor::Outcome::Success, 500);
  assert(maximum.bytesWritten == 100 * 1024 && maximum.filesystem.files[kJpg].size() == 100 * 1024);
  for (int code : {404, 408, 429, 500}) {
    fake::reset(); Env env;
    env.http.responseCode = code;
    expectOutcome(run(env), code == 404 ? Executor::Outcome::PermanentFailure
                                        : Executor::Outcome::RetryableFailure, 500);
    assert(fake::diagnostics.lastHttpCode == code && fake::diagnostics.lastWrite == 'e');
    assert(env.filesystem.files.empty());
  }
}

void admissionGates() {
  fake::reset(); Env wifi;
  WiFi.connection = 0;
  expectOutcome(run(wifi), Executor::Outcome::RetryableFailure, 0);
  assert(!wifi.http.begun && fake::diagnostics.lastWrite == 'W');
  assert(std::find(fake::events.begin(), fake::events.end(), "lease.enter") == fake::events.end());

  fake::reset(); Env heap;
  fake::heapFree = 11 * 1024;
  expectOutcome(run(heap), Executor::Outcome::RetryableFailure, 0);
  assert(!heap.http.begun && fake::diagnostics.lastWrite == 'H');

  fake::reset(); Env space;
  space.low = true;
  expectOutcome(run(space), Executor::Outcome::RetryableFailure, 0);
  assert(!space.http.begun && fake::diagnostics.lastWrite == 'S');
}

void lateGateAndSnapshot() {
  fake::reset(); Env late;
  late.http.stream.body = jpeg(); late.lateAllowed = false;
  expectOutcome(run(late), Executor::Outcome::RetryableFailure, 500);
  assert(late.lateCalls == 1 && fake::index("http.GET") < fake::index("late.card"));
  assert(std::find(fake::events.begin(), fake::events.end(), "file.open.w") == fake::events.end());
  assert(late.filesystem.files.empty());

  fake::reset(); Env topo;
  topo.snapshot.style = 1;
  std::strcpy(topo.snapshot.server, "http://server///");
  topo.http.stream.body = jpeg();
  expectOutcome(run(topo), Executor::Outcome::Success, 500);
  assert(topo.http.url == "http://server/opentopo/2/3/4.jpg");
  assert(topo.filesystem.files.count("/tiles/topo/2/3/4.jpg") == 1);
  assert(topo.filesystem.directories.size() == 4);

  fake::reset(); Env prefixed;
  std::strcpy(prefixed.snapshot.prefix, "/cache");
  prefixed.http.stream.body = jpeg();
  expectOutcome(run(prefixed), Executor::Outcome::Success, 500);
  assert(prefixed.filesystem.files.count("/cache/tiles/2/3/4.jpg") == 1);

  fake::reset(); Env malformed;
  std::memset(malformed.snapshot.server, 'x', sizeof(malformed.snapshot.server));
  expectOutcome(run(malformed), Executor::Outcome::RetryableFailure, 0);
  assert(!malformed.http.begun && malformed.filesystem.files.empty());
  fake::reset(); Env badPrefix;
  std::memset(badPrefix.snapshot.prefix, 'x', sizeof(badPrefix.snapshot.prefix));
  expectOutcome(run(badPrefix), Executor::Outcome::RetryableFailure, 0);
  assert(!badPrefix.http.begun);

  fake::reset(); Env unavailable;
  unavailable.snapshot.filesystem = nullptr;
  expectOutcome(run(unavailable), Executor::Outcome::RetryableFailure, 0);
  assert(!unavailable.http.begun && fake::diagnostics.failed == 1);
}

int main() {
  cachedHit();
  corruptAndPngProtection();
  writeAndBodyFailures();
  existingTwoByteBodyPolicy();
  sizeAndHttpPolicy();
  admissionGates();
  lateGateAndSnapshot();
  std::cout << "tile fetch transport: PASS\n";
}

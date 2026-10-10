// SPDX-License-Identifier: GPL-3.0-or-later
#include "FirmwareRelease.h"
#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <cstddef>
#include <cstdlib>
namespace ui {
namespace {
class LimitedAllocator : public ArduinoJson::Allocator {
public:
  explicit LimitedAllocator(size_t limit) : _limit(limit) {}
  void *allocate(size_t bytes) override { return reallocate(nullptr, bytes); }
  void deallocate(void *pointer) override {
    if (!pointer) return;
    auto *block = static_cast<Block *>(pointer) - 1;
    _used -= block->size;
    std::free(block);
  }
  void *reallocate(void *pointer, size_t bytes) override {
    auto *block = pointer ? static_cast<Block *>(pointer) - 1 : nullptr;
    const size_t old = block ? block->size : 0;
    if (bytes > _limit - (_used - old)) return nullptr;
    auto *next = static_cast<Block *>(std::realloc(block, sizeof(Block) + bytes));
    if (!next) return nullptr;
    next->size = bytes; _used = _used - old + bytes;
    return next + 1;
  }
private:
  union Block { size_t size; std::max_align_t alignment; };
  size_t _limit, _used = 0;
};
bool version(const char *tag, unsigned values[4]) {
  if (!tag || strncmp(tag, "guardian-", 9)) return false;
  const char *p = tag + 9;
  for (unsigned i = 0; i < 4; ++i) {
    if (i == 3 && !*p) { values[i] = 0; return true; }
    if (*p < '0' || *p > '9') return false;
    unsigned number = 0, digits = 0;
    while (*p >= '0' && *p <= '9') {
      if (++digits > 6) return false;
      number = number * 10 + unsigned(*p++ - '0');
    }
    values[i] = number;
    if (i == 3) return !*p;
    if (i == 2 && !*p) { values[3] = 0; return true; }
    if (*p++ != '.') return false;
  }
  return false;
}
bool expected(const char *tag, const char *board, char *name, size_t n, char *url, size_t u) {
  unsigned numbers[4];
  if (!version(tag, numbers) || !board ||
      (strcmp(board, "TDeck") && strcmp(board, "Heltec-V4-TFT"))) return false;
  int count = snprintf(name, n, "Guard-Mesh-%s-%s-app-ota.bin", board, tag);
  if (count < 0 || size_t(count) >= n) return false;
  count = snprintf(url, u, "https://github.com/bubakbubak500/GUARD-MESH/releases/download/%s/%s", tag, name);
  return count >= 0 && size_t(count) < u;
}
bool hash(const char *text) {
  if (!text || strlen(text) != 64) return false;
  for (unsigned i = 0; i < 64; ++i)
    if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f'))) return false;
  return true;
}
bool fail(char *message, size_t capacity, const char *text) {
  if (message && capacity) snprintf(message, capacity, "%s", text);
  return false;
}
} // namespace
bool validFirmwareRelease(const FirmwareRelease &release, const char *board) {
  char name[128], url[256];
  return release.tag[sizeof release.tag - 1] == 0 && release.url[sizeof release.url - 1] == 0 &&
         release.sha256[64] == 0 && release.size > 24 && hash(release.sha256) &&
         expected(release.tag, board, name, sizeof name, url, sizeof url) && !strcmp(release.url, url);
}
bool newerFirmwareRelease(const char *candidate, const char *installed) {
  unsigned a[4], b[4];
  if (!version(candidate, a)) return false;
  if (!version(installed, b)) return true; // Explicit install from a development build.
  for (unsigned i = 0; i < 4; ++i) if (a[i] != b[i]) return a[i] > b[i];
  return false;
}
bool parseFirmwareRelease(const char *json, size_t bytes, const char *board, FirmwareRelease &out,
                          char *message, size_t capacity) {
  out = FirmwareRelease{};
  if (!json || !bytes || bytes > 65536) return fail(message, capacity, "Invalid GitHub release");
  LimitedAllocator filterMemory(8192), documentMemory(16384);
  JsonDocument filter(&filterMemory), doc(&documentMemory);
  filter["tag_name"] = true; filter["draft"] = true; filter["prerelease"] = true;
  for (auto key : {"name", "browser_download_url", "digest", "size", "state"}) filter["assets"][0][key] = true;
  if (filter.overflowed()) return fail(message, capacity, "Not enough memory");
  if (deserializeJson(doc, json, bytes, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(8)))
    return fail(message, capacity, "Invalid GitHub release");
  if (!doc["draft"].is<bool>() || doc["draft"].as<bool>() ||
      !doc["prerelease"].is<bool>() || doc["prerelease"].as<bool>())
    return fail(message, capacity, "Invalid GitHub release");
  const char *tag = doc["tag_name"].as<const char *>();
  char name[128], url[256];
  if (!tag || strlen(tag) >= sizeof out.tag || !expected(tag, board, name, sizeof name, url, sizeof url))
    return fail(message, capacity, "Unsupported firmware release");
  JsonArrayConst assets = doc["assets"].as<JsonArrayConst>();
  bool found = false;
  for (JsonObjectConst asset : assets) {
    const char *assetName = asset["name"] | "";
    if (strcmp(assetName, name)) continue;
    if (found) { out = FirmwareRelease{}; return fail(message, capacity, "Invalid GitHub release"); }
    const char *digest = asset["digest"] | "";
    if (strcmp(asset["state"] | "", "uploaded") || strcmp(asset["browser_download_url"] | "", url) ||
        strncmp(digest, "sha256:", 7) || !hash(digest + 7) || !asset["size"].is<uint32_t>())
      return fail(message, capacity, "Invalid firmware asset");
    snprintf(out.tag, sizeof out.tag, "%s", tag);
    snprintf(out.url, sizeof out.url, "%s", url);
    snprintf(out.sha256, sizeof out.sha256, "%s", digest + 7);
    out.size = asset["size"].as<uint32_t>();
    found = true;
  }
  if (!found || !validFirmwareRelease(out, board)) {
    out = FirmwareRelease{};
    return fail(message, capacity, "No OTA image for this board");
  }
  if (message && capacity) message[0] = 0;
  return true;
}
} // namespace ui

// SPDX-License-Identifier: GPL-3.0-or-later
#include "services/LanguageFile.h"
#include <FS.h>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
namespace {
fs::FS disk;
bool mounted;
char selected[12], requested[12];
uint8_t builtin;
int allocations, live, failAt, saves, downloads, publishes, publishedCount;
const I18nPair *published;
using Result = ui::LanguageFile::Result;
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
void reset(const char *code = "cs") {
  check(live == 0 && !published, "Previous language service leaked memory or overlay");
  disk = fs::FS{};
  disk.enableMemory();
  disk.mkdir("/lang");
  mounted = true;
  snprintf(selected, sizeof selected, "%s", code);
  requested[0] = 0;
  builtin = LANG_EN;
  allocations = live = failAt = saves = downloads = publishes = publishedCount = 0;
}
void put(const char *body) {
  auto file = disk.open("/lang/cs.lang", "w");
  file.print(body);
}
ui::LanguageFile::Host host() {
  return {[](char *out, size_t size) { snprintf(out, size, "%s", selected); },
          []() -> uint8_t { return builtin; },
          [](const char *code) {
            ++saves;
            snprintf(selected, sizeof selected, "%s", code);
          },
          []() -> fs::FS * { return mounted ? &disk : nullptr; },
          [](char *out, size_t size, const char *relative) { snprintf(out, size, "%s", relative); },
          [](const char *code) {
            ++downloads;
            snprintf(requested, sizeof requested, "%s", code);
          },
          [](size_t size) -> void * {
            if (++allocations == failAt)
              return nullptr;
            auto *memory = malloc(size);
            if (memory)
              ++live;
            return memory;
          },
          [](void *memory) {
            check(!published, "Language storage freed before unpublishing borrowed table");
            --live;
            free(memory);
          },
          [](const I18nPair *pairs, int count) {
            ++publishes;
            published = pairs;
            publishedCount = count;
          }};
}
const char *value(const char *key) {
  for (int i = 0; i < publishedCount; ++i)
    if (!strcmp(published[i].key, key))
      return published[i].val;
  return nullptr;
}
} // namespace
void runLanguageFileRegression() {
  reset();
  put("# name: Test\r\nZed\tLast\r\n\n#7A7F87 Color#\t#7A7F87 Barva#\r\n"
      "Alpha\\nBeta\tLine\\nTab\\tSlash\\\\\nEmpty\t\n\tMissing key\nZed\tReplacement\n");
  {
    ui::LanguageFile file(host());
    check(file.load() == Result::Loaded && publishedCount == 3, "Language parsing/count failed");
    check(!strcmp(value("Zed"), "Replacement"), "Duplicate language key did not use last value");
    check(!strcmp(value("Alpha\nBeta"), "Line\nTab\tSlash\\"), "Language escapes changed");
    check(!strcmp(value("#7A7F87 Color#"), "#7A7F87 Barva#"), "Recolor key treated as comment");
    for (int i = 1; i < publishedCount; ++i)
      check(strcmp(published[i - 1].key, published[i].key) < 0, "Published language table not sorted/unique");
    check(live == 2 && publishes == 1, "Language owner did not retain backing storage");
    disk.remove("/lang/cs.lang");
    check(file.load() == Result::Loaded && allocations == 2 && publishes == 1 && downloads == 0,
          "Boot loader repeated completed I/O");
  }
  check(!live && !published && publishes == 2, "Language owner did not release/unpublish overlay");
  reset("");
  builtin = LANG_CS;
  mounted = false;
  {
    ui::LanguageFile file(host());
    check(file.load() == Result::WaitingForStorage && saves == 1 && !strcmp(selected, "cs"),
          "Legacy built-in language migration failed");
    check(file.load() == Result::WaitingForStorage && saves == 1 && downloads == 0,
          "Unready storage caused repeated migration/download");
    mounted = true;
    check(file.load() == Result::Missing && downloads == 1 && !strcmp(requested, "cs"),
          "Missing language did not request background repair");
    check(file.load() == Result::Missing && downloads == 1, "Missing language queued download repeatedly");
  }
  reset("");
  {
    ui::LanguageFile file(host());
    check(file.load() == Result::Disabled && downloads == 0 && allocations == 0,
          "English without overlay performed I/O");
  }
  reset("../cs");
  {
    ui::LanguageFile file(host());
    check(file.load() == Result::Invalid && downloads == 0,
          "Invalid stored language code reached storage/download");
  }
  reset();
  put("Key\tComplete translation\n");
  disk.limitReads(9);
  {
    ui::LanguageFile file(host());
    check(file.load() == Result::Invalid && !published && !live, "Short read published partial translation");
    disk.allowReads();
    check(file.load() == Result::Invalid && allocations == 1, "Failed boot load unexpectedly retried");
  }
  for (int failure = 1; failure <= 2; ++failure) {
    reset();
    put("Key\tTranslation\n");
    failAt = failure;
    {
      ui::LanguageFile file(host());
      check(file.load() == Result::OutOfMemory && !published && !live,
            "Language allocation failure leaked state");
    }
  }
  reset();
  put("# header only\n\n");
  {
    ui::LanguageFile file(host());
    check(file.load() == Result::Invalid && !live, "Empty language table published or leaked");
  }
  reset();
  {
    auto file = disk.open("/lang/cs.lang", "w");
    const uint8_t body[] = {'K', 'e', 'y', '\t', 'A', 0, 'B', '\n'};
    file.write(body, sizeof body);
  }
  {
    ui::LanguageFile file(host());
    check(file.load() == Result::Invalid && !live, "Embedded NUL published truncated language table");
  }
  reset();
  put("Key\tFinal line");
  {
    ui::LanguageFile file(host());
    check(file.load() == Result::Loaded && !strcmp(value("Key"), "Final line"),
          "Final language line without newline was lost");
  }
  check(!live && !published, "Language regression leaked session state");
}

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../i18n.h"
#include <stddef.h>
namespace fs {
class FS;
}
namespace ui {
// One boot session, on the UI thread. Owns the sorted overlay and backing bytes;
// the sink borrows them until it receives publish(nullptr, 0) at destruction.
class LanguageFile {
public:
  enum class Result { WaitingForStorage, Disabled, Loaded, Missing, Invalid, OutOfMemory };
  struct Host {
    void (*readCode)(char *, size_t);
    uint8_t (*builtinLanguage)();
    void (*saveCode)(const char *);
    fs::FS *(*filesystem)();
    void (*path)(char *, size_t, const char *);
    void (*requestDownload)(const char *);
    void *(*allocate)(size_t);
    void (*release)(void *);
    void (*publish)(const I18nPair *, int);
  };
  explicit LanguageFile(Host host) : _host(host) {}
  ~LanguageFile();
  LanguageFile(const LanguageFile &) = delete;
  LanguageFile &operator=(const LanguageFile &) = delete;
  // Retries only while storage is unavailable. Missing/invalid files and OOM
  // leave the built-in fallback in use for this boot, without repeated I/O.
  Result load();

private:
  Host _host;
  Result _result = Result::WaitingForStorage;
  char *_bytes = nullptr;
  I18nPair *_pairs = nullptr;
  int _count = 0;
  Result discard(Result);
};
} // namespace ui

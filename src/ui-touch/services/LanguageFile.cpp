// SPDX-License-Identifier: GPL-3.0-or-later
#include "LanguageFile.h"
#include <FS.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace ui {
namespace {
bool validCode(const char *code) {
  for (; *code; ++code)
    if (!((*code >= 'a' && *code <= 'z') || (*code >= 'A' && *code <= 'Z') ||
          (*code >= '0' && *code <= '9') || *code == '-' || *code == '_'))
      return false;
  return true;
}
void unescape(char *text) {
  char *out = text;
  for (char *in = text; *in; ++in) {
    if (*in == '\\' && in[1]) {
      ++in;
      *out++ = *in == 'n' ? '\n' : *in == 't' ? '\t' : *in;
    } else
      *out++ = *in;
  }
  *out = 0;
}
int comparePairs(const void *left, const void *right) {
  const auto &a = *static_cast<const I18nPair *>(left);
  const auto &b = *static_cast<const I18nPair *>(right);
  const int order = std::strcmp(a.key, b.key);
  if (order)
    return order;
  // Equal keys retain their file order. Both pointers lie in the same byte buffer.
  return a.key < b.key ? -1 : a.key > b.key ? 1 : 0;
}
} // namespace
LanguageFile::~LanguageFile() {
  if (_count)
    _host.publish(nullptr, 0);
  discard(_result);
}
LanguageFile::Result LanguageFile::discard(Result result) {
  if (_pairs)
    _host.release(_pairs);
  if (_bytes)
    _host.release(_bytes);
  _pairs = nullptr;
  _bytes = nullptr;
  _count = 0;
  return _result = result;
}
LanguageFile::Result LanguageFile::load() {
  if (_result != Result::WaitingForStorage)
    return _result;
  char code[12] = {};
  _host.readCode(code, sizeof code);
  code[sizeof code - 1] = 0;
  if (!code[0]) {
    const uint8_t language = _host.builtinLanguage();
    if (language > 0 && language < LANG_COUNT) {
      _host.saveCode(kUiLangCodes[language]);
      _host.readCode(code, sizeof code);
      code[sizeof code - 1] = 0;
    }
  }
  if (!code[0])
    return _result = Result::Disabled;
  if (!validCode(code))
    return _result = Result::Invalid;
  auto *disk = _host.filesystem();
  if (!disk)
    return _result;
  char relative[28], path[64] = {};
  std::snprintf(relative, sizeof relative, "/lang/%s.lang", code);
  _host.path(path, sizeof path, relative);
  path[sizeof path - 1] = 0;
  auto file = disk->open(path, "r");
  if (!file) {
    _result = Result::Missing;
    _host.requestDownload(code);
    return _result;
  }
  const size_t size = file.size();
  if (file.isDirectory() || size < 8 || size > 256 * 1024) {
    file.close();
    return _result = Result::Invalid;
  }
  _bytes = static_cast<char *>(_host.allocate(size + 1));
  if (!_bytes) {
    file.close();
    return _result = Result::OutOfMemory;
  }
  const size_t read = file.read(reinterpret_cast<uint8_t *>(_bytes), size);
  file.close();
  // Do not publish a truncated catalog, including a partially read final value.
  if (read != size || std::memchr(_bytes, 0, size))
    return discard(Result::Invalid);
  _bytes[size] = 0;
  size_t lines = 1;
  for (size_t i = 0; i < size; ++i)
    if (_bytes[i] == '\n')
      ++lines;
  _pairs = static_cast<I18nPair *>(_host.allocate(sizeof(I18nPair) * lines));
  if (!_pairs)
    return discard(Result::OutOfMemory);
  for (char *line = _bytes; line && *line;) {
    char *next = std::strchr(line, '\n');
    if (next) {
      if (next > line && next[-1] == '\r')
        next[-1] = 0;
      *next++ = 0;
    }
    // Recolor keys begin with '#'. Only lines without a tab are comments/headers.
    char *tab = std::strchr(line, '\t');
    if (tab) {
      *tab++ = 0;
      unescape(line);
      unescape(tab);
      if (*line && *tab)
        _pairs[_count++] = {line, tab};
    }
    line = next;
  }
  if (!_count)
    return discard(Result::Invalid);
  std::qsort(_pairs, _count, sizeof(I18nPair), comparePairs);
  // Last occurrence wins deterministically when an edited file repeats a key.
  int unique = 0;
  for (int i = 0; i < _count; ++i) {
    if (unique && !std::strcmp(_pairs[unique - 1].key, _pairs[i].key))
      _pairs[unique - 1] = _pairs[i];
    else
      _pairs[unique++] = _pairs[i];
  }
  _count = unique;
  _result = Result::Loaded;
  _host.publish(_pairs, _count);
  return _result;
}
} // namespace ui

// SPDX-License-Identifier: GPL-3.0-or-later
#include "GpsStatus.h"
#include <cstdarg>
#include <cstdio>
namespace ui {
namespace gps {
namespace {
class Text {
public:
  Text(char *text, size_t size) : _text(text), _size(text ? size : 0) {
    if (_size)
      *_text = 0;
  }
  void add(const char *format, ...) {
    if (_used >= _size)
      return;
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(_text + _used, _size - _used, format, args);
    va_end(args);
    if (written > 0) {
      const auto count = static_cast<size_t>(written);
      _used = count >= _size - _used ? _size - 1 : _used + count;
    }
  }

private:
  char *_text;
  size_t _size, _used = 0;
};
const char *safe(const char *value) { return value ? value : ""; }
} // namespace
void Status::acquisition(bool enabled, uint32_t now) {
  _searching = enabled;
  _since = now;
}
void Status::format(const Snapshot &snapshot, uint32_t now, bool compact, Labels labels, char *output,
                    size_t capacity) {
  Text text(output, capacity);
  if (!snapshot.enabled) {
    _searching = false;
    text.add("%s", safe(labels.off));
  } else if (!snapshot.fix) {
    if (!_searching)
      acquisition(true, now);
    const uint32_t seconds = (now - _since) / 1000u;
    text.add("%s", safe(labels.searching));
    if (seconds < 600)
      text.add(" %lum%02lus", static_cast<unsigned long>(seconds / 60),
               static_cast<unsigned long>(seconds % 60));
    else
      text.add(" %lum", static_cast<unsigned long>(seconds / 60));
    // Satellites exposed by LocationProvider count the fix, not satellites
    // in view. Until a fix arrives, do not infer receiver health from zero.
    if (!compact)
      text.add("\n%s", safe(labels.coldStart));
  } else {
    _searching = false;
    text.add("%s", safe(labels.fix));
    if (snapshot.satellites > 0)
      text.add(" \xc2\xb7 %d sats", snapshot.satellites);
    if (!compact && snapshot.altitude)
      text.add(" \xc2\xb7 %d m", snapshot.altitude);
    text.add("%s%.5f, %.5f", compact ? "  " : "\n", snapshot.latitude, snapshot.longitude);
  }
  if (!compact && snapshot.receiver && snapshot.power)
    text.add("\n%s: %s", snapshot.receiver, snapshot.power);
}
} // namespace gps
} // namespace ui

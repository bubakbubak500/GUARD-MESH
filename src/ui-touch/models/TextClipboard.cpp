// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextClipboard.h"
namespace ui {
bool TextClipboard::set(const char *text, bool recolor) {
  if (!text)
    return false;
  enum class State { Text, Parameter, Colored };
  State state = State::Text;
  size_t used = 0;
  // Forward compaction also allows set(this->text()) and slices of that text.
  for (; *text && used + 1 < Capacity; ++text) {
    if (recolor) {
      if (*text == '#') {
        if (state == State::Text) {
          state = State::Parameter;
          continue;
        }
        if (state == State::Colored) {
          state = State::Text;
          continue;
        }
        state = State::Text; // ## renders one literal hash.
      } else if (state == State::Parameter) {
        if (*text == ' ')
          state = State::Colored;
        continue;
      }
    }
    _text[used++] = *text;
  }
  if (used) {
    size_t start = used - 1;
    while (start && (static_cast<unsigned char>(_text[start]) & 0xc0) == 0x80)
      --start;
    const auto lead = static_cast<unsigned char>(_text[start]);
    const size_t bytes = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
    if (used - start < bytes)
      used = start;
  }
  _text[used] = 0;
  return true;
}
} // namespace ui

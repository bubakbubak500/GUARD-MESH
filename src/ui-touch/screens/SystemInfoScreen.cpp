// SPDX-License-Identifier: GPL-3.0-or-later
#include "SystemInfoScreen.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include <cstring>
namespace ui {
namespace screens {
void SystemInfoScreen::detach() {
  ++_generation;
  _body.set(nullptr);
  _live.set(nullptr);
  _rest.set(nullptr);
  _scheduled = false;
}
void SystemInfoScreen::build(lv_obj_t *body, bool memoryOnly) {
  detach();
  if (!body)
    return;
  _memory = memoryOnly;
  _body.set(body);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_left(body, 2, LV_PART_MAIN);
  auto label = [&]() {
    auto *object = lv_label_create(body);
    lv_label_set_long_mode(object, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(object, lv_pct(100));
    lv_obj_set_style_text_font(object, &theme::font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(object, lv_color_hex(theme::colors().COLOR_TEXT), LV_PART_MAIN);
    return object;
  };
  _live.set(label());
  if (!_memory)
    _rest.set(label());
  render(true);
}
void SystemInfoScreen::render(bool first) {
  if ((_reading && _readingGeneration == _generation) || !_body.get() || !_live.get())
    return;
  const auto generation = _generation;
  diagnostics::Snapshot snapshot;
  const bool wasReading = _reading;
  const auto previousReadingGeneration = _readingGeneration;
  _reading = true;
  _readingGeneration = generation;
  if (_host.read)
    _host.read(_host.context, !_memory, snapshot);
  _reading = wasReading;
  _readingGeneration = previousReadingGeneration;
  if (_generation != generation || !_body.get() || !_live.get())
    return;
  char text[2048];
  if (_memory)
    diagnostics::formatMemory(snapshot.hardware, text, sizeof text);
  else
    diagnostics::formatLive(snapshot, text, sizeof text);
  if (first || strcmp(lv_label_get_text(_live.get()), text))
    lv_label_set_text(_live.get(), text);
  if (_generation != generation || !_body.get() || !_rest.get())
    return;
  diagnostics::formatRest(snapshot, text, sizeof text);
  // Preserve slow-tier skip-on-equality: rewrapping it costs hundreds of ms on device.
  if (first || strcmp(lv_label_get_text(_rest.get()), text))
    lv_label_set_text(_rest.get(), text);
}
void SystemInfoScreen::refresh(uint32_t now) {
  if (_memory || !_body.get() || !_live.get())
    return;
  if (_scheduled && int32_t(now - _next) < 0)
    return;
  _scheduled = true;
  // Do not relayout diagnostic text during a scroll or throw.
  for (auto *input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input)) {
    if (lv_indev_get_scroll_dir(input) != LV_DIR_NONE) {
      _next = now + 120;
      return;
    }
  }
  _next = now + 1000;
  render(false);
}
} // namespace screens
} // namespace ui

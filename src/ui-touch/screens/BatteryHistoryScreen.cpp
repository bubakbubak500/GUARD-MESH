// SPDX-License-Identifier: GPL-3.0-or-later
#include "BatteryHistoryScreen.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/ChartTicks.h"
#include "../widgets/Styles.h"
#include <cstdio>
namespace ui {
namespace screens {
using namespace theme;
using namespace widgets;
namespace {
void formatEstimate(battery::Estimate estimate, char *text, size_t size) {
  switch (estimate.kind) {
  case battery::EstimateKind::Gathering:
    snprintf(text, size, "%s", TR("Battery life: gathering data\xe2\x80\xa6"));
    break;
  case battery::EstimateKind::Unavailable:
    snprintf(text, size, "Battery life: —");
    break;
  case battery::EstimateKind::Steady:
    snprintf(text, size, "Battery life: charging / steady");
    break;
  case battery::EstimateKind::Remaining:
    snprintf(text, size, "Est. life: %lud %luh %lum", static_cast<unsigned long>(estimate.seconds / 86400),
             static_cast<unsigned long>(estimate.seconds / 3600 % 24),
             static_cast<unsigned long>(estimate.seconds / 60 % 60));
    break;
  }
}
} // namespace
bool BatteryHistoryScreen::owns(lv_obj_t *object) const {
  for (; object && _root.get(); object = lv_obj_get_parent(object))
    if (object == _root.get())
      return true;
  return false;
}
void BatteryHistoryScreen::detachCallbacks(lv_obj_t *object) {
  if (!object)
    return;
  while (lv_obj_remove_event_cb_with_user_data(object, nullptr, this)) {
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
    detachCallbacks(lv_obj_get_child(object, i));
}
void BatteryHistoryScreen::close() {
  ++_generation;
  auto *old = _root.get();
  detachCallbacks(old);
  _root.set(nullptr);
  _confirm.dismiss();
  if (old) {
    if (_host.closeRoot)
      _host.closeRoot(&old);
    else
      lv_obj_del(old);
  }
}
void BatteryHistoryScreen::deleted(lv_event_t *event) {
  auto &self = *static_cast<BatteryHistoryScreen *>(lv_event_get_user_data(event));
  // ObjectRef ran first. Do not erase callbacks on the root during DELETE:
  // that would shift and skip subsequent navigation/lifetime observers.
  ++self._generation;
  self._confirm.dismiss();
}
void BatteryHistoryScreen::alert(const char *text) {
  if (_host.alert)
    _host.alert(_host.context, text, 1800);
}
void BatteryHistoryScreen::reopen(uint32_t generation) {
  if (generation != _generation || !_root.get())
    return;
  close();
  if (_generation == generation + 1 && !_root.get())
    open();
}
void BatteryHistoryScreen::clearConfirmed(BatteryHistory::Backend backend, uint32_t generation) {
  if (generation != _generation || !_root.get())
    return;
  const auto result = _history.clear(backend);
  if (generation != _generation || !_root.get())
    return;
  if (result != BatteryHistory::Result::Ok) {
    alert(TR("Delete failed"));
    return;
  }
  reopen(generation);
}
void BatteryHistoryScreen::event(lv_event_t *event) {
  auto &self = *static_cast<BatteryHistoryScreen *>(lv_event_get_user_data(event));
  auto *target = lv_event_get_target(event);
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !self.owns(target))
    return;
  // Backdrop receives bubbled events as well; only a direct tap dismisses it.
  if (lv_event_get_current_target(event) == self._root.get() && target != self._root.get())
    return;
  const auto action = reinterpret_cast<intptr_t>(lv_obj_get_user_data(target));
  if (action == 1) {
    const auto backend = self._backend;
    const auto generation = self._generation;
    self._confirm.showCaptured(
        TR("Clear battery history?"), TR("Clear"),
        [&self, backend, generation] { self.clearConfirmed(backend, generation); }, false);
  } else if (action == 2) {
    self._showCpu = !self._showCpu;
    self.reopen(self._generation);
  } else
    self.close();
}
void BatteryHistoryScreen::open() {
  if (_root.get())
    return;
  const auto generation = ++_generation;
  const lv_coord_t top = _host.contentTop ? _host.contentTop() : 0;
  if (generation != _generation || _root.get())
    return;
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  auto *root = lv_obj_create(lv_layer_top());
  _root.set(root);
  lv_obj_add_event_cb(root, deleted, LV_EVENT_DELETE, this);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, sw, sh - top);
  lv_obj_set_pos(root, 0, top);
  lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_50, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(root, event, LV_EVENT_CLICKED, this);

  const lv_coord_t cardw = sw - 24;
  lv_obj_t *card = lv_obj_create(root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, cardw, LV_SIZE_CONTENT);                  // grow to content...
  lv_obj_set_style_max_height(card, sh - top - 16, LV_PART_MAIN); // ...and scroll past the screen
  lv_obj_set_style_min_height(card, 120, LV_PART_MAIN);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 8);
  styleSurface(card, colors().COLOR_PANEL, 8);
  lv_obj_set_style_border_color(card, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
  lv_obj_set_scroll_dir(card, LV_DIR_VER);
  addCloseXBadge(card, event, this); // universal top-right X

  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, TR("Battery \xe2\x80\x94 last 24h"));
  lv_obj_set_style_text_font(title, &font14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_pos(title, 0, 0);

  // Scratch belongs to this open operation; chart series copy the samples.
  struct Buffer {
    battery::Sample *samples;
    void (*release)(void *);
    ~Buffer() {
      if (samples)
        release(samples);
    }
  } buffer{_host.allocate && _host.release ? static_cast<battery::Sample *>(_host.allocate(
                                                 sizeof(battery::Sample) * battery::HistoryCapacity))
                                           : nullptr,
           _host.release};
  auto *samples = buffer.samples;
  if (generation != _generation || !_root.get())
    return;
  const auto backend = _history.resolve();
  if (generation != _generation || !_root.get())
    return;
  size_t n = 0;
  const auto result = samples ? _history.load(backend, samples, battery::HistoryCapacity, n)
                              : BatteryHistory::Result::NoStorage;
  Snapshot snapshot;
  if (_host.snapshot)
    _host.snapshot(_host.context, snapshot);
  if (generation != _generation || !_root.get())
    return;
  _backend = backend;
  if (!samples || result != BatteryHistory::Result::Ok) {
    auto *error = lv_label_create(card);
    lv_label_set_text(error, samples ? TR("Cannot open file") : TR("Out of memory"));
    lv_obj_set_width(error, cardw - 20);
    lv_obj_set_style_text_font(error, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(error, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_pos(error, 0, 28);
    return;
  }

  if (n <= 0) {
    lv_obj_t *empty = lv_label_create(card);
    lv_label_set_text(empty, TR("No battery history yet.\n\nLogged every 5 minutes."));
    lv_obj_set_style_text_color(empty, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(empty, &font12(), LV_PART_MAIN);
    lv_obj_set_pos(empty, 0, 28);
    return;
  }

  // Battery range: bottom 3.4 V, top = calibrated 100% (Settings). Charging can
  // push the rail above the calibrated full (up to ~4.7 V) — clamp those samples
  // to y_top so the line rides along the chart's maximum instead of vanishing.
  const uint16_t full_mv = snapshot.fullMv;
  const lv_coord_t y_top = (lv_coord_t)(full_mv < 3500 ? 4200 : full_mv);
  const lv_coord_t y_bot = 3400;
  // Inset for the axis-label gutters (V left, MHz right) — RF-monitor style.
  const int chart_x = 30, chart_rpad = 30, chart_y = 18, chart_h = 122;
  lv_obj_t *chart = lv_chart_create(card);
  lv_obj_set_size(chart, cardw - 20 - chart_x - chart_rpad, chart_h);
  lv_obj_set_pos(chart, chart_x, chart_y);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(chart, n);
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, y_bot, y_top); // 3.4 V .. calibrated full (blue)
  lv_chart_set_div_line_count(chart, 4, 6);                         // helper grid: 4 horizontal, 6 vertical
  lv_obj_set_style_bg_color(chart, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(chart, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(chart, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(chart, 6, LV_PART_MAIN);
  lv_obj_set_style_line_color(chart, lv_color_hex(colors().COLOR_CHART_GRID),
                              LV_PART_MAIN);          // faint grid lines
  lv_obj_set_style_size(chart, 4, LV_PART_INDICATOR); // visible point dots
  // Axis ticks + labels (RF-monitor style): primary Y reformatted to volts by the
  // draw callback, secondary Y shows raw MHz. Reserve a label gutter on each side.
  lv_obj_set_style_pad_top(chart, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(chart, 6, LV_PART_MAIN);
  lv_obj_set_style_text_font(chart, &font12(), LV_PART_TICKS);
  lv_obj_set_style_text_color(chart, lv_color_hex(colors().COLOR_SUB), LV_PART_TICKS);
  lv_obj_set_style_line_color(chart, lv_color_hex(colors().COLOR_CHART_TICK), LV_PART_TICKS);
  lv_obj_set_style_pad_left(chart, 4, LV_PART_TICKS);
  lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 4, 0, 4, 1, true, 40);
  lv_obj_add_event_cb(chart, millivoltTicks, LV_EVENT_DRAW_PART_BEGIN, nullptr);
  lv_chart_series_t *vser =
      lv_chart_add_series(chart, lv_color_hex(0x4F9DF7), LV_CHART_AXIS_PRIMARY_Y); // blue battery
  // CPU MHz series is optional (toggled by the "CPU" chip next to the trash button).
  lv_chart_series_t *cser = nullptr;
  if (_showCpu) {
    lv_chart_set_range(chart, LV_CHART_AXIS_SECONDARY_Y, 60, 260); // CPU MHz (orange)
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_SECONDARY_Y, 4, 0, 4, 1, true, 40);
    cser = lv_chart_add_series(chart, lv_color_hex(0x4A5256),
                               LV_CHART_AXIS_SECONDARY_Y); // grey CPU (RF-monitor noise-floor tone)
  }
  for (size_t i = 0; i < n; ++i) {
    lv_coord_t v = (lv_coord_t)samples[i].millivolts;
    if (v > y_top)
      v = y_top; // charging spike -> ride the top line
    if (v < y_bot)
      v = y_bot;
    lv_chart_set_next_value(chart, vser, v);
    if (cser)
      lv_chart_set_next_value(chart, cser,
                              samples[i].cpuMHz > 0 ? (lv_coord_t)samples[i].cpuMHz : LV_CHART_POINT_NONE);
  }

  // Time labels on the X (the tick mechanism counts points, not hours).
  auto x_lbl = [&](const char *txt, lv_align_t al) {
    lv_obj_t *l = lv_label_create(card);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_align_to(l, chart, al, 0, 2);
  };
  x_lbl("-24h", LV_ALIGN_OUT_BOTTOM_LEFT);
  x_lbl("now", LV_ALIGN_OUT_BOTTOM_RIGHT);

  // ---- Bottom block (running y, top-down so nothing overlaps; the card scrolls
  // if it overflows the screen). Text capped to clear the CPU + trash buttons. ----
  int by = chart_y + chart_h + 18; // below the chart + its x-axis labels
  const int row_y = by;            // buttons ride to the right of this block
  const lv_coord_t btxt_w = cardw - 20 - 72;

  // estimate (accent headline)
  char est[44];
  formatEstimate(battery::estimate(samples, n, full_mv), est, sizeof est);
  lv_obj_t *el = lv_label_create(card);
  lv_label_set_text(el, est);
  lv_obj_set_style_text_font(el, &font14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(el, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_set_width(el, btxt_w);
  lv_label_set_long_mode(el, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(el, 0, by);
  by += 28; // clear the 26-px CPU/trash button row before the full-width stats line

  // stats
  char sub[72];
  if (_showCpu)
    snprintf(sub, sizeof sub, "now %u.%02u V (full %u.%01u)   CPU %u MHz",
             (unsigned)(samples[n - 1].millivolts / 1000),
             (unsigned)((samples[n - 1].millivolts % 1000) / 10), (unsigned)(y_top / 1000),
             (unsigned)((y_top % 1000) / 100), (unsigned)snapshot.cpuMHz);
  else
    snprintf(sub, sizeof sub, "now %u.%02u V (full %u.%01u)", (unsigned)(samples[n - 1].millivolts / 1000),
             (unsigned)((samples[n - 1].millivolts % 1000) / 10), (unsigned)(y_top / 1000),
             (unsigned)((y_top % 1000) / 100));
  lv_obj_t *sl = lv_label_create(card);
  lv_label_set_text(sl, sub);
  lv_obj_set_style_text_font(sl, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(sl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_width(sl, cardw - 20); // full width — its row has no buttons, so "MHz" never wraps
  lv_label_set_long_mode(sl, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(sl, 0, by);
  by += 18;

  // points count
  lv_obj_t *pl = lv_label_create(card);
  lv_label_set_text_fmt(pl, "%u pts", static_cast<unsigned>(n));
  lv_obj_set_style_text_font(pl, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(pl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_pos(pl, 0, by);
  by += 18;

  // Clear the battery history (with confirmation) — to the right of the text block.
  lv_obj_t *clr = lv_btn_create(card);
  lv_obj_set_user_data(clr, reinterpret_cast<void *>(intptr_t(1)));
  lv_obj_set_size(clr, 30, 26);
  lv_obj_align(clr, LV_ALIGN_TOP_RIGHT, 0, row_y);
  lv_obj_set_style_bg_color(clr, lv_color_hex(themeRole(0xB23A48, colors().COLOR_STATUS_DANGER)),
                            LV_PART_MAIN);
  lv_obj_add_event_cb(clr, event, LV_EVENT_CLICKED, this);
  lv_obj_t *clrl = lv_label_create(clr);
  lv_label_set_text(clrl, LV_SYMBOL_TRASH);
  lv_obj_center(clrl);
  if (isDay())
    lv_obj_set_style_text_color(clrl, lv_color_hex(colors().COLOR_ON_STATUS_DANGER), LV_PART_MAIN);
  useChainedFont(clrl);

  // Show/hide the CPU-MHz series (same size/style as the trash button, to its left).
  lv_obj_t *cpub = lv_btn_create(card);
  lv_obj_set_user_data(cpub, reinterpret_cast<void *>(intptr_t(2)));
  lv_obj_set_size(cpub, 30, 26);
  lv_obj_align(cpub, LV_ALIGN_TOP_RIGHT, -36, row_y);
  lv_obj_set_style_pad_all(cpub, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(cpub,
                            lv_color_hex(_showCpu ? themeRole(0x4A5256, colors().COLOR_CONTROL)
                                                  : themeRole(0x3A3D40, colors().COLOR_CONTROL_DISABLED)),
                            LV_PART_MAIN);
  lv_obj_add_event_cb(cpub, event, LV_EVENT_CLICKED, this);
  lv_obj_t *cpul = lv_label_create(cpub);
  lv_label_set_text(cpul, "CPU");
  lv_obj_set_style_text_font(cpul, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(
      cpul, lv_color_hex(_showCpu ? themeRole(0xFFFFFF, colors().COLOR_TEXT) : colors().COLOR_SUB),
      LV_PART_MAIN);
  lv_obj_center(cpul);

  // ---- Idle power-save stats (snapshot mirror of the Lock-settings diag; the
  // chart itself is a snapshot too, so this refreshes each time the window opens).
  // Same two lines as refreshSleepDiag(): state / wakes / % asleep, then last-wake. ----
  by += 8;
  lv_obj_t *slph = lv_label_create(card);
  lv_label_set_text(slph, TR("Idle power-save"));
  lv_obj_set_style_text_font(slph, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(slph, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_pos(slph, 0, by);
  by += 18;

  char slp1[64];
  snprintf(slp1, sizeof slp1, "%s \xc2\xb7 cycles %lu \xc2\xb7 asleep %u%%",
           snapshot.sleeping ? "sleeping" : "awake", (unsigned long)snapshot.wakes,
           (unsigned)snapshot.asleepPercent);
  lv_obj_t *slp1l = lv_label_create(card);
  lv_label_set_text(slp1l, slp1);
  lv_obj_set_style_text_font(slp1l, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(slp1l, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_width(slp1l, cardw - 20);
  lv_label_set_long_mode(slp1l, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(slp1l, 0, by);
  by += 18;

  lv_obj_t *slp2l = lv_label_create(card);
  lv_label_set_text_fmt(slp2l, "%s%s", TR("last wake: "), snapshot.lastWake);
  lv_obj_set_style_text_font(slp2l, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(slp2l, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_pos(slp2l, 0, by);
  by += 18;

  // A low "asleep %" on its own does not say whether the parks are too short or the
  // device was simply never eligible, which is the only thing worth knowing when
  // someone reports the saver doing nothing (#465). Name the condition that has held
  // the gate shut longest, and for how much of the time.
  const char *held = snapshot.blocker;
  if (held[0]) {
    lv_obj_t *slp3l = lv_label_create(card);
    lv_label_set_text(slp3l, held);
    lv_obj_set_style_text_font(slp3l, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(slp3l, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_width(slp3l, cardw - 20);
    lv_label_set_long_mode(slp3l, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(slp3l, 0, by);
    by += 18;
  }
}

} // namespace screens
} // namespace ui

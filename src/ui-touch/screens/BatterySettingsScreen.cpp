// SPDX-License-Identifier: GPL-3.0-or-later
#include "BatterySettingsScreen.h"
#include "../i18n.h"
#include "../platform/UiPlatform.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"
#include <cstdio>
namespace ui {
namespace screens {
using namespace theme;
using namespace widgets;
void BatterySettingsScreen::unbind(lv_obj_t *object) {
  if (object)
    while (lv_obj_remove_event_cb_with_user_data(object, nullptr, this)) {
    }
}
void BatterySettingsScreen::detach() {
  ++_generation;
  _settings.cancelCalibration(_pending);
  _pending = 0;
  unbind(_body.get());
  unbind(_history.get());
  unbind(_calibrate.get());
  unbind(_sleep.get());
  _body.set(nullptr);
  _history.set(nullptr);
  _calibrate.set(nullptr);
  _sleep.set(nullptr);
}
void BatterySettingsScreen::deleted(lv_event_t *event) {
  // ObjectRef precedes this observer; detach cannot mutate the root's active
  // DELETE callback list and skip later navigation/ownership observers.
  static_cast<BatterySettingsScreen *>(lv_event_get_user_data(event))->detach();
}
void BatterySettingsScreen::notify(const char *text, int duration) {
  if (_host.alert)
    _host.alert(_host.context, text, duration);
}
void BatterySettingsScreen::build(lv_obj_t *body, lv_coord_t width) {
  const bool reuse = body && body == _body.get();
  detach();
  if (!body)
    return;
  if (reuse)
    lv_obj_clean(body);
  _body.set(body);
  lv_obj_add_event_cb(body, deleted, LV_EVENT_DELETE, this);
  const auto generation = _generation;
  const auto sleep = _settings.sleepState();
  if (_generation != generation || !_body.get())
    return;
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, SC(8), LV_PART_MAIN);
  auto label = [&](const char *text) {
    auto *object = lv_label_create(body);
    lv_obj_set_width(object, width - 4);
    lv_label_set_long_mode(object, LV_LABEL_LONG_WRAP);
    lv_label_set_text(object, text);
    lv_obj_set_style_text_font(object, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(object, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    return object;
  };
  auto button = [&](const char *symbol, const char *text) {
    auto *object = lv_btn_create(body);
    lv_obj_set_size(object, width - 4, SC(34));
    styleButton(object);
    auto *caption = lv_label_create(object);
    useChainedFont(caption);
    lv_label_set_text_fmt(caption, "%s  %s", symbol, text);
    lv_obj_center(caption);
    return object;
  };
  label(TR("Battery"));
  _history.set(button(LV_SYMBOL_BATTERY_2, TR("Battery & power history")));
  lv_obj_add_event_cb(_history.get(), event, LV_EVENT_CLICKED, this);
  if (sleep.supported) {
    auto *row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, width - 4, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, SC(6), LV_PART_MAIN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    auto *caption = lv_label_create(row);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(caption, 1);
    lv_obj_set_style_text_font(caption, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(caption, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(caption, TR("Battery saver (experimental)"));
    _sleep.set(lv_switch_create(row));
    lv_obj_set_size(_sleep.get(), 44, 24);
    if (sleep.enabled)
      lv_obj_add_state(_sleep.get(), LV_STATE_CHECKED);
    lv_obj_add_event_cb(_sleep.get(), event, LV_EVENT_VALUE_CHANGED, this);
    label(sleep.blocker[0] ? sleep.blocker : TR("Throttles the CPU when idle to save power"));
  }
  _calibrate.set(button(LV_SYMBOL_BATTERY_FULL, TR("Calibrate battery (full = 100%)")));
  auto *calibrate = _calibrate.get();
  lv_obj_set_style_bg_color(calibrate, lv_color_hex(themeRole(0x2F6B57, colors().COLOR_STATUS_OK)),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(calibrate, lv_color_hex(themeRole(0x244F41, colors().COLOR_STATUS_OK_PRESSED)),
                            LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(calibrate, lv_color_hex(colors().COLOR_ON_STATUS_OK), LV_PART_MAIN);
  lv_obj_add_event_cb(calibrate, event, LV_EVENT_SHORT_CLICKED, this);
  lv_obj_add_event_cb(calibrate, event, LV_EVENT_LONG_PRESSED, this);
}
void BatterySettingsScreen::refresh() {
  if (!_body.get() || !_pending)
    return;
  const auto calibration = _settings.calibration();
  using State = BatterySettings::CalibrationState;
  if (calibration.id == _pending && calibration.state == State::Sampling)
    return;
  const bool current = calibration.id == _pending;
  _pending = 0;
  if (!current)
    return;
  if (calibration.state == State::TooLow)
    notify(TR("Battery read too low — charge fully first"), 2600);
  else if (calibration.state == State::SaveFailed)
    notify(TR("Save failed"), 2600);
  else if (calibration.state == State::Calibrated) {
    char text[192];
    snprintf(text, sizeof text, TR("Calibrated: 100%% = %u.%02u V  (long-press to reset)"),
             unsigned(calibration.millivolts / 1000), unsigned(calibration.millivolts % 1000 / 10));
    notify(text, 3000);
  }
}
void BatterySettingsScreen::event(lv_event_t *event) {
  auto &self = *static_cast<BatterySettingsScreen *>(lv_event_get_user_data(event));
  if (!self._body.get())
    return;
  const auto generation = self._generation;
  auto *target = lv_event_get_target(event);
  const auto code = lv_event_get_code(event);
  if (target == self._history.get() && code == LV_EVENT_CLICKED) {
    if (self._host.history)
      self._host.history(self._host.context);
  } else if (target == self._sleep.get() && code == LV_EVENT_VALUE_CHANGED) {
    const bool enabled = lv_obj_has_state(target, LV_STATE_CHECKED);
    const bool saved = self._settings.setSleep(enabled);
    if (self._generation == generation && self._body.get())
      self.notify(
          saved ? (enabled ? TR("Idle sleep enabled") : TR("Idle sleep disabled")) : TR("Save failed"), 1200);
  } else if (target == self._calibrate.get()) {
    if (code == LV_EVENT_SHORT_CLICKED)
      self._pending = self._settings.beginCalibration(platform::milliseconds());
    else if (code == LV_EVENT_LONG_PRESSED) {
      self._pending = 0;
      const bool saved = self._settings.resetCalibration();
      self.notify(saved ? TR("Battery calibration reset to default") : TR("Save failed"), 2200);
    }
  }
}
} // namespace screens
} // namespace ui

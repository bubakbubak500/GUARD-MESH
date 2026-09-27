// SPDX-License-Identifier: GPL-3.0-or-later
#include "FirmwareUpdatePanel.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
namespace ui {
namespace screens {
using Destination = FirmwareUpdateJobs::Destination;
using namespace theme;
using namespace widgets;
void FirmwareUpdatePanel::detach() {
  ++_generation;
  for (auto *ref : {&_install, &_previous, &_beta, &_sd}) {
    if (ref->get())
      lv_obj_remove_event_cb_with_user_data(ref->get(), event, this);
    ref->set(nullptr);
  }
  for (auto *ref : {&_body, &_summary, &_caption, &_otaStatus, &_sdStatus})
    ref->set(nullptr);
}
lv_obj_t *FirmwareUpdatePanel::label(lv_coord_t width, const char *text) {
  auto *value = lv_label_create(_body.get());
  lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(value, width);
  lv_obj_set_style_text_font(value, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(value, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_label_set_text(value, text);
  return value;
}
lv_obj_t *FirmwareUpdatePanel::button(lv_coord_t width, lv_coord_t height, const char *text) {
  auto *value = lv_btn_create(_body.get());
  lv_obj_set_size(value, width, height);
  styleButton(value);
  lv_obj_add_event_cb(value, event, LV_EVENT_CLICKED, this);
  auto *caption = lv_label_create(value);
  lv_label_set_text(caption, text);
  lv_obj_set_style_text_font(caption, &font14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(caption, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_center(caption);
  return value;
}
void FirmwareUpdatePanel::build(lv_obj_t *body, lv_coord_t width, Options options, Release release) {
  detach();
  _options = options;
  snprintf(_firmware, sizeof _firmware, "%s", options.firmware ? options.firmware : "");
  _options.firmware = _firmware;
  _release = release;
  if (!body || !_body.set(body))
    return;
  _summary.set(label(width, ""));
  if (options.channelControls && options.currentVersion >= 0) {
    if (options.ota) {
      _install.set(button(width, 38, TR(LV_SYMBOL_DOWNLOAD "  Install update")));
      _caption.set(lv_obj_get_child(_install.get(), 0));
      lv_obj_set_style_bg_color(_install.get(), lv_color_hex(colors().COLOR_STATUS_OK_PRESSED),
                                LV_PART_MAIN | LV_STATE_PRESSED);
      _previous.set(button(width, 34, TR("Install a previous version")));
      lv_obj_set_style_bg_color(_previous.get(), lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
      _otaStatus.set(label(width, ""));
    }
    auto *row = lv_obj_create(body);
    lv_obj_set_size(row, width, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(row, SC(8), LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    auto *caption = lv_label_create(row);
    lv_label_set_text(caption, TR("Get test builds (beta)"));
    lv_obj_set_style_text_font(caption, &font14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(caption, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    _beta.set(lv_switch_create(row));
    if (release.beta)
      lv_obj_add_state(_beta.get(), LV_STATE_CHECKED);
    lv_obj_add_event_cb(_beta.get(), event, LV_EVENT_VALUE_CHANGED, this);
    label(width, TR("Receive unreleased test firmware — newer features, but less tested. The update check "
                    "and Install update both follow the beta channel."));
    if (options.sd) {
      _sd.set(button(width, 38, TR(LV_SYMBOL_SD_CARD "  Save update bin to SD")));
      _sdStatus.set(
          label(width,
                options.launcher
                    ? TR("For Launcher installs: saves the latest firmware of the selected channel to the SD "
                         "card (BINS folder).")
                    : TR("Saves the latest firmware of the selected channel to the SD card (BINS folder).")));
    }
  }
  refresh(release);
  showProgress();
}
void FirmwareUpdatePanel::buttons() {
  if (!_body.get())
    return;
  const bool busy = _jobs.installActive();
  const bool current = _release.latest >= 0 && _release.latest <= _options.currentVersion;
  const bool enabled = !busy && !current;
  if (auto *button = _install.get()) {
    if (enabled) {
      lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_clear_state(button, LV_STATE_DISABLED);
    } else {
      lv_obj_clear_flag(button, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_state(button, LV_STATE_DISABLED);
    }
    lv_obj_set_style_bg_color(
        button, lv_color_hex(enabled ? colors().COLOR_STATUS_OK : colors().COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, enabled ? LV_OPA_COVER : LV_OPA_20, LV_PART_MAIN);
    if (_caption.get())
      lv_obj_set_style_text_color(_caption.get(),
                                  lv_color_hex(enabled ? colors().COLOR_ON_STATUS_OK : colors().COLOR_SUB),
                                  LV_PART_MAIN);
  }
  if (auto *button = _previous.get()) {
    if (_release.latest >= 2)
      lv_obj_clear_flag(button, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
  }
  for (auto *ref : {&_previous, &_sd}) {
    if (!ref->get())
      continue;
    if (busy)
      lv_obj_add_state(ref->get(), LV_STATE_DISABLED);
    else
      lv_obj_clear_state(ref->get(), LV_STATE_DISABLED);
  }
}
void FirmwareUpdatePanel::refresh(Release release) {
  _release = release;
  buttons();
  if (!_body.get() || !_summary.get())
    return;
  if (_beta.get()) {
    if (release.beta)
      lv_obj_add_state(_beta.get(), LV_STATE_CHECKED);
    else
      lv_obj_clear_state(_beta.get(), LV_STATE_CHECKED);
  }
  char text[180];
  lv_color_t color = lv_color_hex(colors().COLOR_SUB);
  const int current = _options.currentVersion;
  if (current < 0)
    snprintf(text, sizeof text, "Firmware %s\nDevelopment build — update check off", _firmware);
  else if (release.latest > current) {
    snprintf(text, sizeof text,
             LV_SYMBOL_DOWNLOAD
             "  Update available: beta_%d\nYou have beta_%d — update manually at flasher.wadamesh.com",
             release.latest, current);
    color = lightSurfaceTextColor(0xE2A23A);
  } else if (release.latest >= 0) {
    snprintf(text, sizeof text, LV_SYMBOL_OK "  Up to date (beta_%d)", current);
    color = lightSurfaceTextColor(0x6FCF6F);
  } else if (release.checked)
    snprintf(text, sizeof text, TR("Firmware beta_%d\nCouldn't reach the update server"), current);
  else
    snprintf(text, sizeof text, TR("Firmware beta_%d\nChecking for updates over Wi-Fi…"), current);
  lv_obj_set_style_text_color(_summary.get(), color, LV_PART_MAIN);
  if (strcmp(lv_label_get_text(_summary.get()), text))
    lv_label_set_text(_summary.get(), text);
}
void FirmwareUpdatePanel::notify(const char *text, int duration) {
  if (_host.alert)
    _host.alert(_host.context, text, duration);
}
void FirmwareUpdatePanel::status(bool sd, const char *text, bool warning) {
  auto *label = sd ? _sdStatus.get() : _otaStatus.get();
  if (!_body.get() || !label)
    return;
  lv_obj_set_style_text_color(
      label, warning ? lightSurfaceTextColor(0xE2A23A) : lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  if (strcmp(lv_label_get_text(label), text))
    lv_label_set_text(label, text);
}
void FirmwareUpdatePanel::begin(Destination destination, int version, bool beta) {
  if (!_jobs.requestInstall(destination, beta, version))
    return;
  const auto generation = _generation;
  _scheduled = false;
  if (!_host.ensureExecutor || !_host.ensureExecutor(_host.context))
    _jobs.failQueuedInstall("Not enough memory");
  if (generation != _generation)
    return;
  char text[72];
  if (destination == Destination::Sd)
    snprintf(text, sizeof text, "%s", TR("Saving to SD… 0%"));
  else
    snprintf(text, sizeof text, "Installing beta_%d...\nDo not power off.", version);
  status(destination == Destination::Sd, text, destination == Destination::Ota);
  buttons();
}
void FirmwareUpdatePanel::install(int version, bool beta) {
  if (_jobs.installActive())
    return;
  const auto generation = _generation;
  // A version-picker confirmation can outlive About. Revalidate hardware at
  // execution time rather than relying on a retired page's options.
  if (!_host.otaAvailable || !_host.otaAvailable(_host.context)) {
    notify(TR("Update via the Launcher / flasher.wadamesh.com"), 3000);
    return;
  }
  if (generation != _generation)
    return;
  if (!_host.networkConnected || !_host.networkConnected(_host.context)) {
    if (generation == _generation)
      status(false, TR("Connect to Wi-Fi first, then try again."), true);
    notify(TR("Wi-Fi not connected"), 2000);
    return;
  }
  if (generation != _generation || version < 1)
    return;
  begin(Destination::Ota, version, beta);
}
void FirmwareUpdatePanel::saveToSd() {
  if (_jobs.installActive() || !_options.sd)
    return;
  if (_release.latest < 0) {
    status(true, TR("No version known yet. Wi-Fi and a completed update check are needed first."));
    return;
  }
  begin(Destination::Sd, _release.latest, _release.beta);
}
void FirmwareUpdatePanel::event(lv_event_t *event) {
  auto &self = *static_cast<FirmwareUpdatePanel *>(lv_event_get_user_data(event));
  if (!self._body.get())
    return;
  auto *target = lv_event_get_target(event);
  if (target == self._beta.get() && lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
    const bool beta = lv_obj_has_state(target, LV_STATE_CHECKED);
    if (self._host.selectChannel)
      self._host.selectChannel(self._host.context, beta);
    return;
  }
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || lv_obj_has_state(target, LV_STATE_DISABLED))
    return;
  if (target == self._install.get())
    self.install(self._release.latest, self._release.beta);
  else if (target == self._sd.get())
    self.saveToSd();
  else if (target == self._previous.get() && self._release.latest >= 2 && !self._jobs.installActive()) {
    if (self._host.previousVersions)
      self._host.previousVersions(self._host.context, self._release.latest, self._release.beta);
  }
}
void FirmwareUpdatePanel::showProgress() {
  char text[72];
  if (_jobs.installState(Destination::Ota) == 1) {
    snprintf(text, sizeof text, "Updating %d%%\nDo not power off.", _jobs.progress());
    status(false, text, true);
  } else if (_jobs.installState(Destination::Sd) == 1) {
    snprintf(text, sizeof text, TR("Saving to SD… %d%%"), _jobs.progress());
    status(true, text);
  }
}
void FirmwareUpdatePanel::poll(uint32_t now) {
  if (_scheduled && int32_t(now - _nextPoll) < 0)
    return;
  _scheduled = true;
  _nextPoll = now + 400;
  showProgress();
  FirmwareUpdateJobs::InstallResult result{};
  bool sd = false;
  if (!_jobs.takeInstall(Destination::Ota, result)) {
    sd = true;
    if (!_jobs.takeInstall(Destination::Sd, result))
      return;
  }
  // Consume and finish local UI work before callbacks that may replace the page
  // or start another job. Never access the old tree after such a callback.
  char text[192];
  if (sd) {
    if (result.ok)
      snprintf(text, sizeof text,
               _options.launcher ? TR("Saved: %s\nFlash it from the Launcher.")
                                 : TR("Saved: %s\nReady for offline flashing."),
               result.message);
    else
      snprintf(text, sizeof text, TR("SD download failed: %s"), result.message);
  } else if (result.ok)
    snprintf(text, sizeof text, "%s", TR("Update complete.\nRebooting..."));
  else
    snprintf(text, sizeof text, "Update failed: %s\nUse flasher.wadamesh.com instead.",
             result.message[0] ? result.message : "unknown");
  status(sd, text);
  buttons();
  if (result.ok && sd)
    notify(TR("Update bin saved to SD"), 3000);
  else if (result.ok) {
    lv_refr_now(nullptr);
    if (_host.reboot)
      _host.reboot(_host.context);
  }
}
} // namespace screens
} // namespace ui

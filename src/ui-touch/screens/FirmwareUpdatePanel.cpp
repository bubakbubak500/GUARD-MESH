// SPDX-License-Identifier: GPL-3.0-or-later
#include "FirmwareUpdatePanel.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"
#include <cstdio>
#include <cstring>
namespace ui { namespace screens {
using namespace theme;
void FirmwareUpdatePanel::removeCallbacks(lv_obj_t *root, void *context) {
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) removeCallbacks(lv_obj_get_child(root, i), context);
  while (lv_obj_remove_event_cb_with_user_data(root, nullptr, context)) {}
}
void FirmwareUpdatePanel::detach() {
  if (_body.get()) removeCallbacks(_body.get(), this);
  _body.set(nullptr); _status.set(nullptr); _check.set(nullptr); _install.set(nullptr); _wifi.set(nullptr);
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
lv_obj_t *FirmwareUpdatePanel::button(lv_coord_t width, const char *text, lv_event_cb_t callback) {
  auto *value = lv_btn_create(_body.get());
  lv_obj_set_size(value, width, 38);
  widgets::styleButton(value);
  lv_obj_add_event_cb(value, callback, LV_EVENT_CLICKED, this);
  auto *caption = lv_label_create(value);
  lv_obj_set_style_text_font(caption, &font14(), LV_PART_MAIN);
  lv_label_set_text(caption, text);
  lv_obj_center(caption);
  return value;
}
void FirmwareUpdatePanel::status(const char *message) {
  snprintf(_message, sizeof _message, "%s", message ? message : "");
  render();
}
void FirmwareUpdatePanel::build(lv_obj_t *body, lv_coord_t width, Options options) {
  detach();
  if (!body || !_body.set(body)) return;
  _ota = options.ota;
  snprintf(_firmware, sizeof _firmware, "%s", options.firmware ? options.firmware : "");
  _available = _latest.size && newerFirmwareRelease(_latest.tag, _firmware);
  char version[96];
  snprintf(version, sizeof version, TR("Firmware %s"), _firmware);
  label(width, version);
  label(width, "GUARD-MESH / GitHub");
  auto *row = lv_obj_create(body);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, width, 32);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  auto *caption = lv_label_create(row);
  lv_label_set_text(caption, "Wi-Fi");
  lv_obj_set_style_text_font(caption, &font14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(caption, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_align(caption, LV_ALIGN_LEFT_MID, 0, 0);
  auto *wifi = lv_switch_create(row);
  _wifi.set(wifi);
  lv_obj_align(wifi, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(wifi, [](lv_event_t *event) {
    auto &self = *static_cast<FirmwareUpdatePanel *>(lv_event_get_user_data(event));
    if (self._jobs.installActive()) { self.render(); return; }
    const bool enabled = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    if (self._host.setWifi) self._host.setWifi(self._host.context, enabled);
    self.render();
  }, LV_EVENT_VALUE_CHANGED, this);
  button(width, TR("Wi-Fi settings"), [](lv_event_t *event) {
    auto &self = *static_cast<FirmwareUpdatePanel *>(lv_event_get_user_data(event));
    if (!self._jobs.installActive() && self._host.openWifi) self._host.openWifi(self._host.context);
  });
  _status.set(label(width, ""));
  _check.set(button(width, TR("Check for updates"), [](lv_event_t *event) {
    static_cast<FirmwareUpdatePanel *>(lv_event_get_user_data(event))->checkLatest();
  }));
  _install.set(button(width, TR("Install update"), [](lv_event_t *event) {
    auto &self = *static_cast<FirmwareUpdatePanel *>(lv_event_get_user_data(event));
    if (!self._available || !self._ota || self._jobs.installActive() || self._jobs.checkActive() || !self.connected()) return;
    char message[160];
    snprintf(message, sizeof message, TR("Install %s?\nThe device will restart."), self._latest.tag);
    if (self._host.confirmInstall) self._host.confirmInstall(self._host.context, message);
  }));
  render();
}
void FirmwareUpdatePanel::render() {
  if (!_body.get()) return;
  const bool busy = _jobs.checkActive() || _jobs.installActive() || _rebootPending;
  auto enabled = [](lv_obj_t *object, bool value) {
    if (!object) return;
    if (value) lv_obj_clear_state(object, LV_STATE_DISABLED);
    else lv_obj_add_state(object, LV_STATE_DISABLED);
  };
  enabled(_check.get(), !busy && connected());
  enabled(_install.get(), !busy && connected() && _ota && _available);
  enabled(_wifi.get(), !_jobs.installActive() && !_rebootPending);
  if (_wifi.get()) {
    if (_host.wifiEnabled && _host.wifiEnabled(_host.context)) lv_obj_add_state(_wifi.get(), LV_STATE_CHECKED);
    else lv_obj_clear_state(_wifi.get(), LV_STATE_CHECKED);
  }
  char message[256];
  if (_rebootPending) snprintf(message, sizeof message, "%s", TR("Update complete. Restarting…"));
  else if (_jobs.installActive()) snprintf(message, sizeof message, TR("Installing… %d%%"), _jobs.progress());
  else if (_jobs.checkActive()) snprintf(message, sizeof message, "%s", TR("Checking GitHub…"));
  else if (_message[0]) snprintf(message, sizeof message, "%s", _message);
  else snprintf(message, sizeof message, "%s", connected() ? TR("Ready to check GitHub") : TR("Connect to Wi-Fi first"));
  if (!_ota) {
    size_t used = strlen(message);
    snprintf(message + used, sizeof message - used, "\n%s", TR("No compatible OTA partition. Use USB."));
  }
  if (_status.get() && strcmp(lv_label_get_text(_status.get()), message)) lv_label_set_text(_status.get(), message);
}
void FirmwareUpdatePanel::checkLatest() {
  if (_jobs.checkActive() || _jobs.installActive() || _rebootPending) return;
  if (!connected()) { status(TR("Connect to Wi-Fi first")); return; }
  _available = false; _latest = FirmwareRelease{};
  if (!_jobs.requestCheck(false, ++_generation)) return;
  if (!_host.ensureWorker || !_host.ensureWorker(_host.context)) _jobs.failQueuedCheck();
  render();
}
void FirmwareUpdatePanel::installLatest() {
  if (!_ota || !_available || !connected() || _jobs.checkActive() || _jobs.installActive() || _rebootPending) return;
  if (!_jobs.requestInstall(_latest)) return;
  if (!_host.ensureWorker || !_host.ensureWorker(_host.context)) _jobs.failQueuedInstall("Update worker unavailable");
  render();
}
void FirmwareUpdatePanel::poll(uint32_t now) {
  FirmwareUpdateJobs::CheckResult check;
  if (_jobs.takeCheck(check) && check.request.generation == _generation) {
    _available = check.ok && newerFirmwareRelease(check.release.tag, _firmware);
    _latest = check.ok ? check.release : FirmwareRelease{};
    if (check.ok) snprintf(_message, sizeof _message, _available ? TR("Available: %s") : TR("Firmware is up to date: %s"), check.release.tag);
    else snprintf(_message, sizeof _message, "%s", TR(check.message[0] ? check.message : "GitHub connection failed"));
  }
  FirmwareUpdateJobs::InstallResult result;
  if (_jobs.takeInstall(FirmwareUpdateJobs::Destination::Ota, result)) {
    // Only our pinned image can request a reboot, including after leaving Update.
    if (result.ok && result.request.release.size && !strcmp(result.request.release.tag, _latest.tag)) {
      _rebootPending = true; _rebootAt = now + 1200;
    } else {
      snprintf(_message, sizeof _message, "%s", TR(result.message[0] ? result.message : "Firmware validation failed"));
      if (_host.alert) _host.alert(_host.context, _message, 4000);
    }
  }
  _jobs.takeInstall(FirmwareUpdateJobs::Destination::Sd, result); // No SD installation UI.
  render();
  if (_rebootPending && int32_t(now - _rebootAt) >= 0) {
    _rebootPending = false;
    if (_host.reboot) _host.reboot(_host.context);
  }
}
} } // namespace ui::screens

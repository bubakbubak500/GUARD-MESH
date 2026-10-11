// SPDX-License-Identifier: GPL-3.0-or-later
#include "FirmwareUpdatePanel.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/GuardianIcon.h"
#include "../widgets/Styles.h"
#include <cstdio>
#include <cstring>
namespace ui { namespace screens {
using namespace theme;
namespace {
const uint32_t blue = 0x0087ff, cyan = 0x00c4ec, orange = 0xffa324;
const uint32_t white = 0xf3f7ff, muted = 0xa8c5d3, green = 0x31dd85;
void visible(lv_obj_t *object, bool value) {
  if (!object) return;
  const bool hidden = lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN);
  if (value && hidden) lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
  else if (!value && !hidden) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
}
bool text(lv_obj_t *object, const char *value) {
  if (!object || !strcmp(lv_label_get_text(object), value)) return false;
  lv_label_set_text(object, value);
  return true;
}
lv_obj_t *row(lv_obj_t *parent, lv_coord_t width, lv_coord_t height) {
  auto *value = lv_obj_create(parent);
  lv_obj_remove_style_all(value);
  lv_obj_set_size(value, width, height);
  lv_obj_clear_flag(value, LV_OBJ_FLAG_SCROLLABLE);
  return value;
}
void textColor(lv_obj_t *object, uint32_t value) {
  if (!object) return;
  const lv_color_t color = lv_color_hex(value);
  if (lv_obj_get_style_text_color(object, LV_PART_MAIN).full != color.full)
    lv_obj_set_style_text_color(object, color, LV_PART_MAIN);
}
}
void FirmwareUpdatePanel::removeCallbacks(lv_obj_t *root, void *context) {
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) removeCallbacks(lv_obj_get_child(root, i), context);
  while (lv_obj_remove_event_cb_with_user_data(root, nullptr, context)) {}
}
void FirmwareUpdatePanel::detach() {
  if (_body.get()) removeCallbacks(_body.get(), this);
  _body.set(nullptr); _status.set(nullptr); _check.set(nullptr); _install.set(nullptr); _wifi.set(nullptr);
  _summary.set(nullptr); _heading.set(nullptr); _current.set(nullptr); _version.set(nullptr);
  _versionArrow.set(nullptr); _image.set(nullptr); _hash.set(nullptr);
  _summaryDown.set(nullptr); _summaryInfo.set(nullptr); _summaryWarning.set(nullptr);
  _network.set(nullptr); _networkStatus.set(nullptr); _wifiSettings.set(nullptr); _actions.set(nullptr);
  _progressCard.set(nullptr); _progressTitle.set(nullptr); _progressValue.set(nullptr);
  _progressBar.set(nullptr); _progressNote.set(nullptr);
  _progressDown.set(nullptr); _progressShield.set(nullptr);
  for (auto &dot : _stageDots) dot.set(nullptr);
  for (auto &caption : _stageLabels) caption.set(nullptr);
}
lv_obj_t *FirmwareUpdatePanel::card(lv_obj_t *parent, lv_coord_t width) {
  auto *value = lv_obj_create(parent);
  widgets::styleCard(value);
  lv_obj_set_size(value, width, LV_SIZE_CONTENT);
  lv_obj_clear_flag(value, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(value, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(value, 5, LV_PART_MAIN);
  lv_obj_set_style_pad_row(value, 3, LV_PART_MAIN);
  lv_obj_set_style_radius(value, 7, LV_PART_MAIN);
  lv_obj_set_style_bg_color(value, lv_color_hex(0x071720), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(value, lv_color_hex(0x020b11), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(value, LV_GRAD_DIR_VER, LV_PART_MAIN);
  lv_obj_set_style_border_width(value, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(value, lv_color_hex(cyan), LV_PART_MAIN);
  lv_obj_set_style_border_opa(value, LV_OPA_COVER, LV_PART_MAIN);
  return value;
}
lv_obj_t *FirmwareUpdatePanel::label(lv_obj_t *parent, lv_coord_t width, const char *caption, bool strong) {
  auto *value = lv_label_create(parent);
  lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(value, width);
  lv_obj_set_style_text_font(value, strong ? &font14() : &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(value, lv_color_hex(strong ? white : muted), LV_PART_MAIN);
  lv_label_set_text(value, caption);
  return value;
}
lv_obj_t *FirmwareUpdatePanel::button(lv_obj_t *parent, lv_coord_t width, const char *caption, lv_event_cb_t callback, bool primary) {
  auto *value = lv_btn_create(parent);
  lv_obj_set_size(value, width, 34);
  widgets::styleButton(value);
  lv_obj_set_style_radius(value, 7, LV_PART_MAIN);
  lv_obj_set_style_pad_all(value, 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(value, lv_color_hex(primary ? orange : 0x06131b), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(value, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(value, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(value, lv_color_hex(primary ? orange : cyan), LV_PART_MAIN);
  lv_obj_set_style_border_opa(value, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(value, lv_color_hex(primary ? 0x071018 : white), LV_PART_MAIN);
  lv_obj_set_style_bg_color(value, lv_color_hex(primary ? 0xdb8522 : blue), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_opa(value, LV_OPA_40, LV_PART_MAIN | LV_STATE_DISABLED);
  lv_obj_add_event_cb(value, callback, LV_EVENT_CLICKED, this);
  auto *title = lv_label_create(value);
  lv_obj_set_width(title, width - (primary ? 32 : 10));
  lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(title, width < 160 ? &font12() : &font14(), LV_PART_MAIN);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_text(title, caption);
  if (primary) {
    auto *icon = widgets::guardianIcon(value, widgets::GuardianIcon::Down, 18, 0x071018, lv_color_hex(0x071018));
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_align(title, LV_ALIGN_RIGHT_MID, 0, 0);
  } else lv_obj_center(title);
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
  const char *target = options.target;
  if (!target || !target[0]) {
#if defined(LILYGO_TDECK) || defined(GUARD_SIMULATOR)
    target = "T-Deck / T-Deck Plus";
#elif defined(HELTEC_LORA_V4_R8)
    target = "Heltec V4-R8 TFT";
#elif defined(HELTEC_LORA_V4_TFT)
    target = "Heltec V4 TFT";
#else
    target = TR("This device");
#endif
  }
  snprintf(_target, sizeof _target, "%s", target);
  _available = _latest.size && newerFirmwareRelease(_latest.tag, _firmware);
  lv_obj_set_style_pad_row(body, 4, LV_PART_MAIN);
  lv_obj_set_style_bg_color(body, lv_color_hex(0x02090f), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(body, LV_OPA_COVER, LV_PART_MAIN);
  const lv_coord_t inner = width - 14;
  auto *summary = card(body, width); _summary.set(summary);
  auto *intro = row(summary, inner, 30);
  auto summaryIcon = [&](widgets::GuardianIcon kind, uint32_t accent) {
    auto *icon = widgets::guardianIcon(intro, kind, 28, accent, lv_color_hex(white));
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
    return icon;
  };
  _summaryDown.set(summaryIcon(widgets::GuardianIcon::Down, cyan));
  _summaryInfo.set(summaryIcon(widgets::GuardianIcon::Info, cyan));
  _summaryWarning.set(summaryIcon(widgets::GuardianIcon::Warning, orange));
  auto *heading = label(intro, inner - 36, _target, true); _heading.set(heading);
  lv_obj_set_style_text_font(heading, &font16(), LV_PART_MAIN);
  lv_obj_align(heading, LV_ALIGN_LEFT_MID, 36, 0);
  auto *versions = row(summary, inner, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(versions, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(versions, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(versions, 6, LV_PART_MAIN);
  _versionArrow.set(widgets::guardianIcon(versions, widgets::GuardianIcon::Down, 18, cyan));
  auto *versionText = row(versions, inner - 24, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(versionText, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(versionText, 2, LV_PART_MAIN);
  char version[128];
  snprintf(version, sizeof version, TR("Installed: %s"), _firmware);
  _current.set(label(versionText, inner - 24, version));
  _version.set(label(versionText, inner - 24, "", true));
  _image.set(label(summary, inner, ""));
  _hash.set(label(summary, inner, ""));

  auto *progress = card(body, width); _progressCard.set(progress);
  lv_obj_set_style_pad_all(progress, 9, LV_PART_MAIN);
  lv_obj_set_style_pad_row(progress, 6, LV_PART_MAIN);
  lv_obj_set_style_min_height(progress, 190, LV_PART_MAIN);
  const lv_coord_t progressInner = width - 20;
  label(progress, progressInner, _target);
  auto *progressRow = row(progress, progressInner, 54);
  auto *progressDown = widgets::guardianIcon(progressRow, widgets::GuardianIcon::Down, 34, cyan);
  _progressDown.set(progressDown); lv_obj_align(progressDown, LV_ALIGN_LEFT_MID, 0, 0);
  auto *shield = widgets::guardianIcon(progressRow, widgets::GuardianIcon::Shield, 34, green, lv_color_hex(white));
  _progressShield.set(shield); lv_obj_align(shield, LV_ALIGN_LEFT_MID, 0, 0);
  auto *progressTitle = label(progressRow, progressInner - 128, "", true); _progressTitle.set(progressTitle);
  lv_obj_align(progressTitle, LV_ALIGN_LEFT_MID, 40, 0);
  auto *percent = label(progressRow, 84, "", true); _progressValue.set(percent);
  lv_obj_set_style_text_font(percent, &lv_font_montserrat_28, LV_PART_MAIN);
  lv_obj_set_style_text_align(percent, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_align(percent, LV_ALIGN_RIGHT_MID, 0, 0);
  auto *bar = lv_bar_create(progress); _progressBar.set(bar);
  lv_obj_set_size(bar, progressInner, 18);
  lv_bar_set_range(bar, 0, 100);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x233f53), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_hex(cyan), LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  auto *stages = row(progress, progressInner, 28);
  lv_obj_set_flex_flow(stages, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(stages, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  const char *stageNames[] = {TR("Download"), TR("Validation"), TR("Restart")};
  const lv_coord_t stageWidth = (progressInner - 24) / 3;
  for (unsigned i = 0; i < 3; ++i) {
    auto *stage = row(stages, stageWidth, 28);
    auto *dot = lv_obj_create(stage); lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8); lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 0); _stageDots[i].set(dot);
    auto *caption = label(stage, stageWidth - 12, stageNames[i]); _stageLabels[i].set(caption);
    lv_obj_align(caption, LV_ALIGN_LEFT_MID, 12, 0);
    if (i != 2) label(stages, 12, LV_SYMBOL_RIGHT);
  }
  auto *divider = row(progress, progressInner, 1);
  lv_obj_set_style_bg_color(divider, lv_color_hex(0x3b6174), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
  _progressNote.set(label(progress, progressInner, ""));

  auto *network = row(summary, inner, 32); _network.set(network);
  lv_obj_set_style_border_width(network, 1, LV_PART_MAIN);
  lv_obj_set_style_border_side(network, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
  lv_obj_set_style_border_color(network, lv_color_hex(0x3b6174), LV_PART_MAIN);
  const lv_coord_t networkInner = inner - 2;
  const lv_coord_t settingsWidth = 116;
  auto *caption = label(network, networkInner - settingsWidth - 52, ""); _networkStatus.set(caption);
  lv_obj_align(caption, LV_ALIGN_LEFT_MID, 0, 1);
  auto *wifi = lv_switch_create(network);
  _wifi.set(wifi);
  lv_obj_set_size(wifi, 40, 22);
  lv_obj_align(wifi, LV_ALIGN_RIGHT_MID, -settingsWidth - 6, 1);
  lv_obj_add_event_cb(wifi, [](lv_event_t *event) {
    auto &self = *static_cast<FirmwareUpdatePanel *>(lv_event_get_user_data(event));
    if (self._jobs.installActive()) { self.render(); return; }
    const bool enabled = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    if (self._host.setWifi) self._host.setWifi(self._host.context, enabled);
    self.render();
  }, LV_EVENT_VALUE_CHANGED, this);
  auto *wifiSettings = button(network, settingsWidth, TR("Wi-Fi settings"), [](lv_event_t *event) {
    auto &self = *static_cast<FirmwareUpdatePanel *>(lv_event_get_user_data(event));
    if (!self._jobs.installActive() && self._host.openWifi) self._host.openWifi(self._host.context);
  });
  lv_obj_set_height(wifiSettings, 30);
  lv_obj_align(wifiSettings, LV_ALIGN_RIGHT_MID, 0, 0); _wifiSettings.set(wifiSettings);
  _status.set(label(body, width, ""));
  auto *actions = lv_obj_create(body); _actions.set(actions);
  lv_obj_remove_style_all(actions);
  lv_obj_set_size(actions, width, LV_SIZE_CONTENT);
  lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(actions, 6, LV_PART_MAIN);
  const lv_coord_t actionWidth = (width - 6) / 2;
  _check.set(button(actions, actionWidth, TR("Check for updates"), [](lv_event_t *event) {
    static_cast<FirmwareUpdatePanel *>(lv_event_get_user_data(event))->checkLatest();
  }));
  _install.set(button(actions, actionWidth, TR("Install update"), [](lv_event_t *event) {
    auto &self = *static_cast<FirmwareUpdatePanel *>(lv_event_get_user_data(event));
    if (!self._available || !self._ota || self._jobs.installActive() || self._jobs.checkActive() || !self.connected()) return;
    char message[160];
    snprintf(message, sizeof message, TR("Install %s?\nThe device will restart."), self._latest.tag);
    if (self._host.confirmInstall) self._host.confirmInstall(self._host.context, message);
  }, true));
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
  enabled(_wifiSettings.get(), !_jobs.installActive() && !_rebootPending);
  if (_wifi.get()) {
    if (_host.wifiEnabled && _host.wifiEnabled(_host.context)) lv_obj_add_state(_wifi.get(), LV_STATE_CHECKED);
    else lv_obj_clear_state(_wifi.get(), LV_STATE_CHECKED);
  }
  const bool installing = _jobs.installActive() || _rebootPending;
  visible(_summary.get(), !installing);
  visible(_progressCard.get(), installing);
  visible(_network.get(), !installing);
  visible(_actions.get(), !installing);
  visible(_status.get(), !installing);
  const bool haveImage = _latest.size != 0;
  visible(_version.get(), haveImage);
  visible(_versionArrow.get(), haveImage);
  visible(_image.get(), haveImage);
  visible(_hash.get(), haveImage && _latest.sha256[0]);
  const bool warning = !_ota || !connected() || (!_jobs.checkActive() && _message[0] &&
                       strcmp(_message, TR("Install after confirmation.")));
  visible(_summaryDown.get(), !warning && _available);
  visible(_summaryInfo.get(), !warning && !_available);
  visible(_summaryWarning.get(), warning);
  char message[256];
  text(_heading.get(), _target);
  if (haveImage) {
    snprintf(message, sizeof message, _available ? TR("Available: %s") : TR("Firmware is up to date: %s"), _latest.tag);
    if (text(_version.get(), message)) {
      lv_point_t measured;
      lv_txt_get_size(&measured, message, &font14(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
      lv_obj_set_style_text_font(_version.get(), measured.x <= lv_obj_get_width(_version.get()) ? &font14() : &font12(), LV_PART_MAIN);
    }
    // Use integer arithmetic so this view does not pull in float formatting.
    const uint32_t hundredths = uint32_t(uint64_t(_latest.size) * 100 / 1048576);
    snprintf(message, sizeof message, TR("OTA image: %lu.%02lu MiB"),
             static_cast<unsigned long>(hundredths / 100), static_cast<unsigned long>(hundredths % 100));
    text(_image.get(), message);
    char digest[32];
    snprintf(digest, sizeof digest, "%.8s…%.8s", _latest.sha256, _latest.sha256 + 56);
    snprintf(message, sizeof message, TR("SHA-256 expected: %s"), digest);
    text(_hash.get(), message);
  }
  const char *connection = connected() ? TR("Connected") : TR("Offline");
  snprintf(message, sizeof message, "%s  %s", LV_SYMBOL_WIFI, connection);
  text(_networkStatus.get(), message);
  textColor(_networkStatus.get(), connected() ? green : muted);
  visible(_progressDown.get(), !_rebootPending);
  visible(_progressShield.get(), _rebootPending);
  text(_progressTitle.get(), _rebootPending ? TR("Firmware verified") : TR("Downloading firmware"));
  const int downloaded = _jobs.progress();
  const int percent = _rebootPending ? 100 : downloaded < 100 ? downloaded : 99;
  snprintf(message, sizeof message, "%d %%", percent);
  text(_progressValue.get(), message);
  if (_progressBar.get() && lv_bar_get_value(_progressBar.get()) != percent)
    lv_bar_set_value(_progressBar.get(), percent, LV_ANIM_OFF);
  const lv_color_t indicator = lv_color_hex(_rebootPending ? green : cyan);
  if (_progressBar.get() && lv_obj_get_style_bg_color(_progressBar.get(), LV_PART_INDICATOR).full != indicator.full)
    lv_obj_set_style_bg_color(_progressBar.get(), indicator, LV_PART_INDICATOR);
  for (unsigned i = 0; i < 3; ++i) {
    // The transport exposes download progress and the final validated result,
    // so intermediate validation is never invented from a percentage alone.
    const uint32_t stageColor = _rebootPending ? (i < 2 ? green : cyan) : (i == 0 ? cyan : 0x45687c);
    auto *dot = _stageDots[i].get();
    if (dot && lv_obj_get_style_bg_color(dot, LV_PART_MAIN).full != lv_color_hex(stageColor).full)
      lv_obj_set_style_bg_color(dot, lv_color_hex(stageColor), LV_PART_MAIN);
    textColor(_stageLabels[i].get(), (_rebootPending || i == 0) ? white : muted);
  }
  text(_progressNote.get(), _rebootPending ? TR("SHA-256 and image verified. Restarting…") :
       TR("Keep Wi-Fi connected. SHA-256 and image validation before restart."));
  if (_rebootPending) snprintf(message, sizeof message, "%s", TR("Firmware verified"));
  else if (_jobs.installActive()) snprintf(message, sizeof message, "%s", TR("Downloading firmware"));
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
    if (check.ok) snprintf(_message, sizeof _message, "%s", _available ? TR("Install after confirmation.") : "");
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

// SPDX-License-Identifier: GPL-3.0-or-later
#include "FirmwareUpdatePanel.h"
#include "../services/FirmwareUpdateSource.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include <cstdio>
namespace ui {
namespace screens {
using namespace theme;
void FirmwareUpdatePanel::detach() { _body.set(nullptr); }
lv_obj_t *FirmwareUpdatePanel::label(lv_coord_t width, const char *text) {
  auto *value = lv_label_create(_body.get());
  lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(value, width);
  lv_obj_set_style_text_font(value, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(value, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_label_set_text(value, text);
  return value;
}
void FirmwareUpdatePanel::build(lv_obj_t *body, lv_coord_t width, Options options) {
  detach();
  if (!body || !_body.set(body)) return;
  char version[96];
  snprintf(version, sizeof version, TR("Firmware %s"), options.firmware ? options.firmware : "");
  label(width, version);
  label(width, TR("Firmware downloads on GitHub"));
  label(width, firmwareUpdate::releasesUrl);
  label(width, TR("Download the firmware for your exact board from GUARD-MESH releases."));
  label(width, TR("Automatic installation is not available yet."));
  label(width, options.ota
                   ? TR("For Wi-Fi OTA, use app-ota.bin. Open Terminal and run 'ota start', connect to "
                        "MeshCore-OTA, then open the displayed /update address and upload as Firmware.")
                   : TR("For this partition layout, use USB or your board's installer."));
}
void FirmwareUpdatePanel::install(int version, bool beta) {
  (void)version;
  (void)beta;
  if (_host.alert)
    _host.alert(_host.context, TR("Automatic installation is unavailable. Use GUARD-MESH GitHub releases."), 4000);
}
void FirmwareUpdatePanel::poll(uint32_t now) {
  (void)now;
  FirmwareUpdateJobs::CheckResult check;
  _jobs.takeCheck(check);
  FirmwareUpdateJobs::InstallResult result;
  if (!_jobs.takeInstall(FirmwareUpdateJobs::Destination::Ota, result))
    _jobs.takeInstall(FirmwareUpdateJobs::Destination::Sd, result);
  // Never reboot or announce installation success from a legacy completion.
}
} // namespace screens
} // namespace ui

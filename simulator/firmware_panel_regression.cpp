// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.h"
#include "screens/FirmwareUpdatePanel.h"
#include "services/FirmwareUpdateSource.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace {
using Jobs = ui::FirmwareUpdateJobs;
using Panel = ui::screens::FirmwareUpdatePanel;
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
lv_obj_t *label(lv_obj_t *root, const char *text) {
  if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text)) return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = label(lv_obj_get_child(root, i), text)) return found;
  return nullptr;
}
bool control(lv_obj_t *root) {
  if (lv_obj_check_type(root, &lv_btn_class) || lv_obj_check_type(root, &lv_switch_class)) return true;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (control(lv_obj_get_child(root, i))) return true;
  return false;
}
lv_obj_t *body() {
  auto *root = lv_obj_create(lv_layer_top());
  lv_obj_set_size(root, 240, 240);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  return root;
}
}
void runFirmwarePanelRegression() {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  Jobs jobs;
  int alerts = 0;
  Panel panel(jobs, {&alerts, [](void *p, const char *, int) { ++*static_cast<int *>(p); }});
  auto *first = body();
  panel.build(first, 220, {"guardian-2026.10.10.1", true});
  check(label(first, ui::firmwareUpdate::releasesUrl) &&
        label(first, TR("Automatic installation is not available yet.")), "GitHub/manual status missing");
  check(!control(first), "Legacy update controls remain enabled");
  check(label(first, TR("For Wi-Fi OTA, use app-ota.bin. Open Terminal and run 'ota start', connect to "
                        "MeshCore-OTA, then open the displayed /update address and upload as Firmware.")),
        "Manual OTA instructions missing");
  // A stale previous-version callback cannot schedule any network or flash job.
  panel.install(999, true);
  check(alerts == 1 && !jobs.installActive() && !jobs.checkActive(), "Direct legacy install was accepted");
  auto *second = body();
  panel.build(second, 220, {"development", false});
  check(label(second, TR("For this partition layout, use USB or your board's installer.")),
        "Missing-slot instructions offer an OTA upload");
  lv_obj_del(first);
  panel.detach();
  lv_obj_del(second);
  // Completed old jobs must be consumed even with no About tree, without reboot.
  for (auto destination : {Jobs::Destination::Ota, Jobs::Destination::Sd}) {
    check(jobs.requestInstall(destination, true, 27), "Could not queue legacy regression job");
    check(jobs.runInstall({nullptr, nullptr,
                          [](void *, const Jobs::InstallRequest &, Jobs::Progress, Jobs::InstallResult &result) {
                            result.ok = true;
                          }}), "Could not finish legacy regression job");
    panel.poll(0);
    check(!jobs.installActive() && alerts == 1, "Detached legacy result was retained or announced");
  }
  check(jobs.requestCheck(false, 3), "Could not queue legacy check");
  jobs.failQueuedCheck();
  panel.poll(UINT32_MAX);
  check(!jobs.checkActive(), "Legacy check completion was retained");
  panel.build(nullptr, 220, {nullptr, true});
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Manual firmware panel leaked a root");
  puts("Firmware panel: GitHub source, manual OTA, missing-slot fallback and blocked legacy actions passed.");
}

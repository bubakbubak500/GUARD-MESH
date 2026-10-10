// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.h"
#include "screens/FirmwareUpdatePanel.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace {
using Jobs = ui::FirmwareUpdateJobs;
using Panel = ui::screens::FirmwareUpdatePanel;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
lv_obj_t *label(lv_obj_t *root, const char *text) {
  if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text)) return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = label(lv_obj_get_child(root, i), text)) return found;
  return nullptr;
}
lv_obj_t *body() {
  auto *root = lv_obj_create(lv_layer_top());
  lv_obj_set_size(root, 240, 240); lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN); return root;
}
struct Context {
  bool connected = false, wifi = false, worker = true;
  unsigned alerts = 0, confirmations = 0, reboots = 0, settings = 0;
  Panel::Host host() {
    return {this,
      [](void *p, const char *, int) { ++static_cast<Context *>(p)->alerts; },
      [](void *p) { return static_cast<Context *>(p)->connected; },
      [](void *p) { return static_cast<Context *>(p)->wifi; },
      [](void *p, bool enabled) { static_cast<Context *>(p)->wifi = enabled; return true; },
      [](void *p) { ++static_cast<Context *>(p)->settings; },
      [](void *p) { return static_cast<Context *>(p)->worker; },
      [](void *p, const char *text) { check(strstr(text, "guardian-"), "Confirmation lost release"); ++static_cast<Context *>(p)->confirmations; },
      [](void *p) { ++static_cast<Context *>(p)->reboots; }};
  }
};
void completeCheck(Jobs &jobs, const char *tag) {
  check(jobs.runCheck({const_cast<char *>(tag), nullptr, nullptr, [](void *p, Jobs::CheckResult &result) {
    result.ok = true; result.release.size = 123456;
    snprintf(result.release.tag, sizeof result.release.tag, "%s", static_cast<char *>(p));
  }}), "Check not queued");
}
}
void runFirmwarePanelRegression() {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  Jobs jobs; Context context; Panel panel(jobs, context.host());
  auto *first = body(); panel.build(first, 220, {"guardian-2026.10.10.1", true});
  auto *checkButton = lv_obj_get_parent(label(first, TR("Check for updates")));
  auto *installButton = lv_obj_get_parent(label(first, TR("Install update")));
  check(lv_obj_has_state(checkButton, LV_STATE_DISABLED) && lv_obj_has_state(installButton, LV_STATE_DISABLED), "Disconnected OTA actions enabled");
  panel.installLatest(); check(!jobs.installActive(), "Unchecked install accepted");
  context.connected = true; panel.poll(0); panel.checkLatest();
  check(jobs.checkActive(), "Connected check not queued");
  completeCheck(jobs, "guardian-2026.10.10.2"); panel.poll(1);
  check(panel.updateAvailable() && !lv_obj_has_state(installButton, LV_STATE_DISABLED), "New release unavailable");
  lv_event_send(installButton, LV_EVENT_CLICKED, nullptr);
  check(context.confirmations == 1 && !jobs.installActive(), "Confirmation started install before acceptance");
  panel.installLatest(); check(jobs.installActive(), "Accepted install not queued");
  check(jobs.runInstall({nullptr, nullptr, [](void *, const Jobs::InstallRequest &, Jobs::Progress, Jobs::InstallResult &out) {
    snprintf(out.message, sizeof out.message, "Firmware SHA-256 mismatch");
  }}), "Install not executed");
  panel.poll(2); panel.poll(10000);
  check(context.alerts == 1 && !context.reboots, "Failed update rebooted");
  panel.checkLatest(); completeCheck(jobs, "guardian-2026.10.10.1"); panel.poll(10001);
  check(!panel.updateAvailable() && lv_obj_has_state(installButton, LV_STATE_DISABLED), "Same release offered an update");
  panel.checkLatest(); completeCheck(jobs, "guardian-2026.10.10"); panel.poll(10002);
  check(!panel.updateAvailable(), "Older latest offered a downgrade");
  auto *second = body(); panel.build(second, 220, {"guardian-2026.10.10.1", false});
  lv_obj_del(first);
  panel.checkLatest(); completeCheck(jobs, "guardian-2026.10.10.2"); panel.poll(10003);
  panel.installLatest(); check(!jobs.installActive(), "Missing-slot install accepted");
  check(label(second, TR("No compatible OTA partition. Use USB.")) == nullptr, "Expected combined status label");
  panel.detach(); lv_obj_del(second);
  auto *third = body(); panel.build(third, 220, {"guardian-2026.10.10.1", true});
  panel.installLatest(); check(jobs.installActive(), "Pinned release lost during navigation");
  panel.detach(); lv_obj_del(third);
  check(jobs.runInstall({nullptr, nullptr, [](void *, const Jobs::InstallRequest &, Jobs::Progress, Jobs::InstallResult &out) { out.ok = true; }}), "Detached update not executed");
  panel.poll(UINT32_MAX - 500); panel.poll(698);
  check(!context.reboots, "Restart delay failed across wrap");
  panel.poll(699); check(context.reboots == 1, "Successful detached OTA did not reboot");
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "OTA panel leaked a root");
  puts("Firmware panel: connection, release check, confirmation, failure, version, partition and detached completion passed.");
}

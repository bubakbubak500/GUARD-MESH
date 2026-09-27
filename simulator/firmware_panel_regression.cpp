// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.h"
#include "screens/FirmwareUpdatePanel.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace {
using Jobs = ui::FirmwareUpdateJobs;
using Panel = ui::screens::FirmwareUpdatePanel;
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
lv_obj_t *label(lv_obj_t *root, const char *text) {
  if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = label(lv_obj_get_child(root, i), text))
      return found;
  return nullptr;
}
lv_obj_t *type(lv_obj_t *root, const lv_obj_class_t *klass) {
  if (lv_obj_check_type(root, klass))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = type(lv_obj_get_child(root, i), klass))
      return found;
  return nullptr;
}
lv_obj_t *button(lv_obj_t *root, const char *text) {
  auto *found = label(root, text);
  check(found != nullptr, "Update panel button missing");
  return lv_obj_get_parent(found);
}
lv_obj_t *body() {
  auto *root = lv_obj_create(lv_layer_top());
  lv_obj_set_size(root, 240, 240);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  return root;
}
const Panel::Options options{"unit-test", 10, true, true, true, true};
struct Context {
  Panel *panel = nullptr;
  lv_obj_t *replacement = nullptr;
  bool online = true, slot = true, executor = true, success = true, beta = false;
  int starts = 0, alerts = 0, reboots = 0, picks = 0, changes = 0;
  int pickedVersion = -1;
  bool pickedBeta = false;
  Jobs::InstallRequest request{};
  Panel::Host host() {
    return {this,
            [](void *p) {
              auto &c = *static_cast<Context *>(p);
              ++c.starts;
              if (c.replacement) {
                auto *root = c.replacement;
                c.replacement = nullptr;
                c.panel->build(root, 220, options, {14, true, false});
              }
              return c.executor;
            },
            [](void *p) { return static_cast<Context *>(p)->online; },
            [](void *p) { return static_cast<Context *>(p)->slot; },
            [](void *p, const char *, int) { ++static_cast<Context *>(p)->alerts; },
            [](void *p) { ++static_cast<Context *>(p)->reboots; },
            [](void *p, bool beta) {
              auto &c = *static_cast<Context *>(p);
              ++c.changes;
              c.beta = beta;
              c.panel->refresh({-1, false, beta});
            },
            [](void *p, int version, bool beta) {
              auto &c = *static_cast<Context *>(p);
              ++c.picks;
              c.pickedVersion = version;
              c.pickedBeta = beta;
            }};
  }
  void finish(Jobs &jobs) {
    check(jobs.runInstall({this, nullptr,
                           [](void *p, const Jobs::InstallRequest &request, Jobs::Progress progress,
                              Jobs::InstallResult &result) {
                             auto &c = *static_cast<Context *>(p);
                             c.request = request;
                             progress.report(progress.context, 48);
                             result.ok = c.success;
                             strcpy(result.message, c.success ? "/BINS/test.bin" : "download interrupted");
                           }}),
          "Update panel did not queue an immutable job");
  }
};
} // namespace
void runFirmwarePanelRegression() {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  Jobs jobs;
  Context context;
  Panel panel(jobs, context.host());
  context.panel = &panel;
  auto *first = body();
  panel.build(first, 220, options, {10, true, false});
  auto *install = button(first, TR(LV_SYMBOL_DOWNLOAD "  Install update"));
  check(lv_obj_has_state(install, LV_STATE_DISABLED), "Known-current update remained enabled");
  lv_event_send(install, LV_EVENT_CLICKED, nullptr);
  check(context.starts == 0, "Disabled update accepted a synthetic click");
  panel.refresh({12, true, true});
  check(!lv_obj_has_state(install, LV_STATE_DISABLED), "New release did not enable update");
  lv_event_send(button(first, TR("Install a previous version")), LV_EVENT_CLICKED, nullptr);
  check(context.picks == 1 && context.pickedVersion == 12 && context.pickedBeta,
        "Previous picker lost release/channel snapshot");
  context.online = false;
  lv_event_send(install, LV_EVENT_CLICKED, nullptr);
  check(!jobs.installActive() && label(first, TR("Connect to Wi-Fi first, then try again.")),
        "Offline OTA was not rejected");
  context.online = true;
  context.slot = false;
  panel.install(11, true);
  check(!jobs.installActive() && context.starts == 0, "Missing OTA slot accepted an install");
  context.slot = true;
  lv_event_send(install, LV_EVENT_CLICKED, nullptr);
  check(jobs.installActive() && lv_obj_has_state(install, LV_STATE_DISABLED),
        "Queued update was not disabled");
  panel.refresh({15, true, false});
  check(lv_obj_has_state(install, LV_STATE_DISABLED), "Release refresh enabled a running install");
  context.success = false;
  context.finish(jobs);
  check(context.request.version == 12 && context.request.beta &&
            context.request.destination == Jobs::Destination::Ota,
        "Changing release/channel rewrote an in-flight install");
  panel.poll(1);
  check(!jobs.installActive() && !lv_obj_has_state(install, LV_STATE_DISABLED),
        "Failed update could not be retried");

  auto *second = body();
  panel.build(second, 220, options, {13, true, false});
  auto *oldSwitch = type(first, &lv_switch_class);
  lv_obj_add_state(oldSwitch, LV_STATE_CHECKED);
  lv_event_send(oldSwitch, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_event_send(install, LV_EVENT_CLICKED, nullptr);
  check(context.starts == 1 && context.changes == 0, "Retired About tree controlled its replacement");
  lv_obj_del(first);
  auto *currentSwitch = type(second, &lv_switch_class);
  lv_obj_add_state(currentSwitch, LV_STATE_CHECKED);
  lv_event_send(currentSwitch, LV_EVENT_VALUE_CHANGED, nullptr);
  check(context.changes == 1 && context.beta, "Beta switch did not use its host");
  auto *sd = button(second, TR(LV_SYMBOL_SD_CARD "  Save update bin to SD"));
  lv_event_send(sd, LV_EVENT_CLICKED, nullptr);
  check(!jobs.installActive(), "SD download accepted an unknown version");
  panel.refresh({17, true, true});
  lv_event_send(sd, LV_EVENT_CLICKED, nullptr);
  check(jobs.installActive(), "SD button did not start a download");
  panel.detach();
  lv_event_send(sd, LV_EVENT_CLICKED, nullptr);
  lv_obj_del(second);
  context.success = true;
  context.finish(jobs);
  const int alerts = context.alerts;
  panel.poll(500);
  check(!jobs.installActive() && context.alerts == alerts + 1 && context.reboots == 0 &&
            context.request.destination == Jobs::Destination::Sd && context.request.version == 17 &&
            context.request.beta,
        "Detached SD completion was lost or used a changed channel");
  panel.poll(1000);
  check(context.alerts == alerts + 1, "Download completion was consumed twice");

  // Completion remains alive across a page DELETE, and the 400 ms schedule wraps.
  auto *third = body();
  panel.build(third, 220, options, {18, true, false});
  panel.install(18, false);
  panel.poll(UINT32_MAX - 200);
  lv_obj_del(third);
  context.finish(jobs);
  panel.poll(198);
  check(context.reboots == 0 && jobs.installActive(), "Update completion timer failed before wrap deadline");
  panel.poll(199);
  check(context.reboots == 1 && !jobs.installActive(), "Detached successful OTA did not reboot exactly once");
  panel.poll(600);
  check(context.reboots == 1, "OTA result rebooted twice");

  // Failed executor startup publishes an error; it cannot leave the panel stuck.
  auto *fourth = body();
  panel.build(fourth, 220, options, {19, true, false});
  context.executor = false;
  panel.install(19, false);
  panel.poll(1000);
  check(!jobs.installActive() &&
            !lv_obj_has_state(button(fourth, TR(LV_SYMBOL_DOWNLOAD "  Install update")), LV_STATE_DISABLED),
        "Executor OOM left the update panel busy");
  context.executor = true;
  context.replacement = body();
  auto *replacement = context.replacement;
  panel.install(19, false);
  check(!label(replacement, "Installing beta_19...\nDo not power off."),
        "Retired start overwrote a reentrant replacement");
  context.success = false;
  context.finish(jobs);
  panel.poll(1400);
  lv_obj_del(fourth);
  lv_obj_del(replacement);
  panel.detach();

  auto *retained = body();
  {
    Panel local(jobs, context.host());
    local.build(retained, 220, options, {20, true, false});
  }
  const int starts = context.starts;
  lv_event_send(button(retained, TR(LV_SYMBOL_DOWNLOAD "  Install update")), LV_EVENT_CLICKED, nullptr);
  check(context.starts == starts, "Update widget retained a destroyed controller");
  lv_obj_del(retained);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Update panel leaked a root");
  puts("Firmware panel: immutable installs, channel changes, stale trees, DELETE, OOM, timer wrap and "
       "detached completion passed.");
}

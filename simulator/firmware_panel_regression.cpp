// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.h"
#include "screens/FirmwareUpdatePanel.h"
#include "theme/Fonts.h"
#include "theme/Theme.h"
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
    snprintf(result.release.sha256, sizeof result.release.sha256, "%s", "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
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
  char expected[128];
  snprintf(expected, sizeof expected, TR("OTA image: %lu.%02lu MiB"), 0ul, 11ul);
  check(label(first, expected), "OTA size did not use resolved release metadata");
  snprintf(expected, sizeof expected, TR("SHA-256 expected: %s"), "01234567…89abcdef");
  check(label(first, expected) && !label(first, TR("Firmware verified")), "Expected hash was claimed verified before download");
  lv_event_send(installButton, LV_EVENT_CLICKED, nullptr);
  check(context.confirmations == 1 && !jobs.installActive(), "Confirmation started install before acceptance");
  panel.installLatest(); check(jobs.installActive(), "Accepted install not queued");
  struct InstallView { Panel *panel; lv_obj_t *root; } view{&panel, first};
  check(jobs.runInstall({&view, nullptr, [](void *p, const Jobs::InstallRequest &, Jobs::Progress progress, Jobs::InstallResult &out) {
    auto &view = *static_cast<InstallView *>(p);
    progress.report(progress.context, 64);
    view.panel->poll(2);
    check(label(view.root, "64 %") && label(view.root, TR("Downloading firmware")) &&
          !label(view.root, TR("Firmware verified")), "Download progress claimed image validation or completion");
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

void runFirmwarePanelVisualRegression(void (*capture)(const char *)) {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  auto *root = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(root);
  lv_obj_set_size(root, 320, 240);
  lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  auto *title = lv_label_create(root);
  char caption[80]; snprintf(caption, sizeof caption, "%s  %s", LV_SYMBOL_LEFT, TR("Update"));
  lv_label_set_text(title, caption);
  lv_obj_set_style_text_font(title, &ui::theme::font14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(ui::theme::colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_pos(title, 8, 4);
  auto *content = lv_obj_create(root); lv_obj_remove_style_all(content);
  lv_obj_set_size(content, 320, 218); lv_obj_set_pos(content, 0, 22);
  lv_obj_set_style_pad_all(content, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_row(content, 6, LV_PART_MAIN);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  Jobs jobs; Context context; context.connected = context.wifi = true;
  Panel panel(jobs, context.host());
  panel.build(content, 304, {"guardian-2026.10.10.3", true, "T-Deck / T-Deck Plus"});
  panel.checkLatest(); completeCheck(jobs, "guardian-2026.10.11"); panel.poll(0);
  auto shot = [&](const char *name) { lv_obj_update_layout(root); if (capture) capture(name); };
  shot("ota-available.png");
  panel.installLatest();
  struct View { Panel *panel; void (*capture)(const char *); } view{&panel, capture};
  check(jobs.runInstall({&view, nullptr, [](void *p, const Jobs::InstallRequest &, Jobs::Progress progress, Jobs::InstallResult &out) {
    auto &view = *static_cast<View *>(p);
    progress.report(progress.context, 64); view.panel->poll(1);
    if (view.capture) view.capture("ota-download.png");
    out.ok = true;
  }}), "Visual OTA install not queued");
  panel.poll(2);
  check(label(content, TR("Firmware verified")) && label(content, "100 %"), "Successful image validation not displayed");
  shot("ota-verified.png");
  panel.detach(); lv_obj_del(root);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Visual OTA panel leaked a root");
}

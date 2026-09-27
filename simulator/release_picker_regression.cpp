// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.h"
#include "screens/ReleasePicker.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace {
namespace picker = ui::screens::releasePicker;
int installs, notifications, installedVersion;
bool installedBeta, reopen, replaceOnClose, allowOpenOnInstall;
std::vector<lv_obj_t *> retired;
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
lv_obj_t *root() { return lv_obj_get_child(lv_layer_top(), -1); }
lv_obj_t *label(lv_obj_t *object, const char *text) {
  if (lv_obj_check_type(object, &lv_label_class) && !strcmp(lv_label_get_text(object), text))
    return object;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
    if (auto *found = label(lv_obj_get_child(object, i), text))
      return found;
  return nullptr;
}
lv_obj_t *button(const char *text) {
  auto *found = label(root(), text);
  check(found != nullptr, "Release picker button missing");
  return lv_obj_get_parent(found);
}
void click(lv_obj_t *object) { lv_event_send(object, LV_EVENT_CLICKED, nullptr); }
} // namespace
void runReleasePickerRegression() {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  installs = notifications = 0;
  reopen = replaceOnClose = allowOpenOnInstall = false;
  picker::configure({[](lv_obj_t **object) {
                       retired.push_back(*object);
                       lv_obj_add_flag(*object, LV_OBJ_FLAG_HIDDEN);
                       *object = nullptr;
                       if (replaceOnClose) {
                         replaceOnClose = false;
                         picker::open(50, false);
                         click(button("beta_49"));
                       }
                     },
                     [](int version, bool beta) {
                       ++installs;
                       installedVersion = version;
                       installedBeta = beta;
                       check(allowOpenOnInstall || !picker::isOpen(),
                             "Release action ran before confirmation closed");
                       if (reopen)
                         picker::open(40, false);
                     },
                     [](const char *) { ++notifications; }, []() -> lv_coord_t { return 24; }, nullptr});
  picker::open(1, false);
  check(!picker::isOpen() && notifications == 1, "Empty release list opened");
  picker::open(5, true);
  check(label(root(), "beta_4") && label(root(), "beta_2") && !label(root(), "beta_1"),
        "Previous-version bounds");
  auto *oldRoot = root(), *oldRow = button("beta_4");
  picker::open(8, false);
  click(oldRow);
  click(oldRoot);
  check(installs == 0 && label(root(), "beta_7"), "Old picker operated replacement");
  lv_obj_del(oldRoot);
  retired.erase(retired.begin());
  check(picker::isOpen(), "Old root DELETE closed replacement");
  click(button("beta_6"));
  auto *oldConfirm = button(TR("Install"));
  picker::open(9, true);
  click(oldConfirm);
  check(installs == 0 && label(root(), "beta_8"), "Stale confirmation installed new selection");
  click(button("beta_7"));
  click(button(TR("Install")));
  check(installs == 1 && installedVersion == 7 && installedBeta && !picker::isOpen(),
        "Captured release request lost channel/version");
  picker::open(3, false);
  click(button("beta_2"));
  click(button(TR("Cancel")));
  check(installs == 1 && !picker::isOpen(), "Canceled downgrade installed");
  picker::open(3, false);
  lv_obj_del(root());
  check(!picker::isOpen(), "External picker DELETE left stale root");
  picker::open(3, false);
  click(button("beta_1"));
  lv_obj_del(root());
  check(!picker::isOpen() && installs == 1, "External confirmation DELETE installed");
  picker::open(4, false);
  click(button("beta_3"));
  reopen = true;
  click(button(TR("Install")));
  check(installs == 2 && !installedBeta && installedVersion == 3 && picker::isOpen() &&
            label(root(), "beta_39"),
        "Install callback replacement picker was closed");
  reopen = false;
  picker::open(6, true);
  click(button("beta_5"));
  replaceOnClose = allowOpenOnInstall = true;
  click(button(TR("Install")));
  check(installs == 3 && installedVersion == 5 && installedBeta && picker::isOpen(),
        "Confirmation resolved a replacement's request instead of its captured value");
  allowOpenOnInstall = false;
  click(button(TR("Install")));
  check(installs == 4 && installedVersion == 49 && !installedBeta && !picker::isOpen(),
        "Reentrant confirmation lost its independent action");
  picker::close();
  for (auto *object : retired)
    lv_obj_del(object);
  retired.clear();
  picker::configure({});
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Release picker leaked a tree");
  puts("Release picker: captured channel/version, stale menus/confirmations, cancel, DELETE and reentrant "
       "open passed.");
}

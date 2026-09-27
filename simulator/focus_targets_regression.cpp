// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/FocusTargets.h"
#include <stdexcept>
namespace {
ui::FocusTargets *owner = nullptr;
lv_obj_t *lastDeleted = nullptr;
int deletions = 0, focusChanges = 0;
void check(bool value, const char *reason) {
  if (!value)
    throw std::runtime_error(reason);
}
void deleted(lv_obj_t *object) {
  lastDeleted = object;
  ++deletions;
  for (int i = 0; i < owner->count(); ++i)
    check(owner->at(i) != object, "Deleted target remained in ordered registry");
}
void focused(lv_group_t *) {
  ++focusChanges;
  for (int i = 0; i < owner->count(); ++i)
    check(owner->at(i) != lastDeleted, "Focus callback observed destroyed target");
}
} // namespace
void runFocusTargetsRegression() {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  auto *page = lv_obj_create(lv_layer_top());
  lv_obj_t *survivor = nullptr;
  {
    ui::FocusTargets targets(deleted);
    owner = &targets;
    check(targets.initialize(), "Navigation group allocation failed");
    auto *group = targets.group();
    check(targets.initialize() && targets.group() == group, "Navigation initialize replaced live group");
    lv_group_set_focus_cb(group, focused);
    for (int i = 0; i < 200; ++i)
      check(targets.add(lv_btn_create(page)), "Navigation lost a target beyond old fixed capacity");
    check(targets.count() == 200 && lv_group_get_obj_count(group) == 200,
          "Navigation group and registry differ");
    auto *first = targets.at(0);
    auto *second = targets.at(1);
    check(targets.add(first) && targets.count() == 200 && targets.at(0) == first,
          "Duplicate target changed order");
    lv_group_focus_obj(first);
    const int beforeFocus = focusChanges;
    lv_obj_del(first);
    check(deletions == 1 && targets.count() == 199 && targets.at(0) == second && focusChanges > beforeFocus,
          "Focused deletion did not retire target before automatic refocus");
    auto *middle = targets.at(70);
    lv_obj_del(middle);
    check(deletions == 2 && targets.count() == 198 && lv_group_get_obj_count(group) == 198,
          "Middle deletion desynchronized group");
    check(!targets.at(-1) && !targets.at(198), "Navigation accepted invalid index");
    targets.clear();
    lastDeleted = nullptr; // A later allocation may legitimately reuse the old address.
    check(targets.count() == 0 && lv_group_get_obj_count(group) == 0, "Navigation clear retained targets");
    const auto count = deletions;
    lv_obj_clean(page);
    check(deletions == count, "Cleared navigation retained DELETE observers");
    survivor = lv_btn_create(page);
    check(targets.add(survivor), "Navigation could not reuse cleared registry");
  }
  check(lv_obj_is_valid(survivor) && !lv_obj_get_group(survivor),
        "Navigation destruction deleted borrowed widget");
  const auto count = deletions;
  lv_obj_del(page);
  check(deletions == count, "Widget retained destroyed navigation owner");
  owner = nullptr;
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Navigation test leaked roots");
}

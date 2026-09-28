// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/HomeScreen.h"
#include "i18n.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
using Home = ui::screens::HomeScreen;
Home::Preview live[3];
int selected = -1;
int actions[7] = {};

void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
lv_obj_t *findLabel(lv_obj_t *root, const char *text) {
  if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text)) return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = findLabel(lv_obj_get_child(root, i), text)) return found;
  return nullptr;
}
Home::Host host() {
  return {
    [](Home::Action action) { ++actions[static_cast<int>(action)]; },
    [](const Home::Preview &shown) {
      return shown.index >= 0 && shown.index < 3 &&
             shown.channel == live[shown.index].channel &&
             !strcmp(shown.name, live[shown.index].name);
    },
    [](int index, bool channel) {
      check(index >= 0 && index < 3 && channel == live[index].channel, "Wrong Home conversation selected");
      selected = index;
    },
    [](const lv_font_t *, char *out, size_t cap, const char *in) { snprintf(out, cap, "%s", in); }
  };
}
} // namespace

void runHomeScreenRegression() {
  const auto originalRoots = lv_obj_get_child_cnt(lv_layer_top());
  auto *parent = lv_obj_create(lv_layer_top());
  lv_obj_set_size(parent, 320, 184);
  for (int i = 0; i < 3; ++i) {
    live[i] = Home::Preview{};
    live[i].index = i;
    live[i].channel = i == 1;
    live[i].unread = 1;
    snprintf(live[i].name, sizeof live[i].name, "Person %d", i);
    snprintf(live[i].text, sizeof live[i].text, "Latest message %d", i);
  }
  selected = -1;
  memset(actions, 0, sizeof actions);
  {
    Home home(host());
    check(home.create(parent, 320, 184) && home.active(), "Home did not create");
    home.refresh(3, live, 3);
    lv_obj_update_layout(parent);
    auto *firstLabel = findLabel(parent, "Person 0");
    auto *secondLabel = findLabel(parent, "Person 1");
    auto *thirdLabel = findLabel(parent, "Person 2");
    check(firstLabel && secondLabel && thirdLabel && findLabel(parent, "Latest message 2"),
          "Home does not fit three names and snippets");
    auto *first = lv_obj_get_parent(firstLabel);
    auto *second = lv_obj_get_parent(secondLabel);
    auto *third = lv_obj_get_parent(thirdLabel);
    check(!lv_obj_has_flag(first, LV_OBJ_FLAG_HIDDEN) &&
          !lv_obj_has_flag(second, LV_OBJ_FLAG_HIDDEN) &&
          !lv_obj_has_flag(third, LV_OBJ_FLAG_HIDDEN), "Home hid a preview row");
    check(lv_obj_get_y(third) + lv_obj_get_height(third) <=
          lv_obj_get_height(lv_obj_get_parent(third)), "Third Home preview exceeds card");
    check(findLabel(parent, TR("Guardian BLE off")) && findLabel(parent, "TX: —  RX: —"),
          "Guardian must start unavailable");
    guardian::Snapshot pc;
    pc.enabled = pc.radio = pc.ready = true;
    pc.session.connect();
    const uint8_t packet[] = {0x47,0x4d,1,7,5,0,0,0,2,0,0,0,1,0,0,0,7,0,0,0};
    pc.session.accept(packet, sizeof packet, 100);
    home.refreshGuardian(pc, 101);
    check(findLabel(parent, "Guardian  TX:+ RX:+"), "Home lost concurrent TX/RX");
    home.refreshGuardian(pc, 15100);
    check(findLabel(parent, TR("Guardian stale")) && findLabel(parent, "TX: —  RX: —"),
          "Stale Guardian data remained visible on Home");
    pc.session.disconnect();
    home.refreshGuardian(pc, 15101);
    check(findLabel(parent, TR("Guardian offline")), "Disconnect did not clear Home");
    auto *focus = lv_group_create();
    lv_group_add_obj(focus, first);
    lv_group_focus_obj(first);
    home.refresh(3, live, 3);
    check(lv_obj_get_parent(findLabel(parent, "Person 0")) == first &&
          lv_group_get_focused(focus) == first, "Unchanged refresh rebuilt or unfocused a row");
    lv_event_send(first, LV_EVENT_CLICKED, nullptr);
    check(selected == 0, "Home preview did not select its conversation");
    selected = -1;
    snprintf(live[0].name, sizeof live[0].name, "Reused slot");
    lv_event_send(first, LV_EVENT_CLICKED, nullptr);
    check(selected == -1, "Home selected a stale conversation identity");
    home.refresh(1, live, 1);
    lv_event_send(second, LV_EVENT_CLICKED, nullptr);
    check(selected == -1 && lv_obj_has_flag(second, LV_OBJ_FLAG_HIDDEN),
          "Missing Home row remained clickable");
    for (int i = 0; i < 7; ++i) {
      auto *target = home.actionTarget(static_cast<Home::Action>(i));
      check(target != nullptr, "Home action target missing");
      lv_event_send(target, LV_EVENT_CLICKED, nullptr);
      check(actions[i] == 1, "Home action was not dispatched");
    }
    lv_group_del(focus);
    lv_obj_clean(parent);
    check(!home.active() && !home.actionTarget(Home::Action::Inbox),
          "Home retained an externally deleted tree");
    auto *replacement = lv_obj_create(lv_layer_top());
    lv_obj_set_size(replacement, 320, 184);
    check(home.create(replacement, 320, 184), "Home could not rebind after external clean");
    home.refresh(1, live, 1);
    check(findLabel(replacement, "Reused slot"), "Rebound Home did not render previews");
    lv_obj_del(parent);
    parent = replacement;
    check(home.active(), "Deleting old parent invalidated rebound Home");
  }
  check(lv_obj_get_child_cnt(parent) == 0, "Home destructor left its child tree behind");
  lv_obj_del(parent);
  check(lv_obj_get_child_cnt(lv_layer_top()) == originalRoots, "Home regression leaked a root");
}

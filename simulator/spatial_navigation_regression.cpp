// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/SpatialNavigation.h"
#include <initializer_list>
#include <stdexcept>
namespace {
using ui::focus::Direction;
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
struct Scene {
  ui::FocusTargets targets;
  lv_obj_t *root;
  Scene() {
    check(targets.initialize(), "Spatial group allocation failed");
    root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 400, 400);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  }
  ~Scene() { lv_obj_del(root); }
  lv_obj_t *button(int x, int y, int width, int height, lv_obj_t *parent = nullptr) {
    auto *object = lv_btn_create(parent ? parent : root);
    lv_obj_remove_style_all(object);
    lv_obj_set_size(object, width, height);
    lv_obj_set_pos(object, x, y);
    check(targets.add(object), "Spatial target registration failed");
    return object;
  }
  lv_obj_t *choose(lv_obj_t *current, Direction direction, const ui::focus::SpatialPolicy &policy = {}) {
    lv_obj_update_layout(root);
    return ui::focus::spatialTarget(targets, current, direction, policy);
  }
};
int previewCount = 0, commitCount = 0;
void sliderEvent(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
    ++previewCount;
    if (lv_event_get_user_data(event))
      lv_obj_del(lv_event_get_target(event));
  } else if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
    ++commitCount;
  }
}
} // namespace

void runSpatialNavigationRegression() {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  // Rotate the same layout to exercise every direction. A narrow intervening
  // switch must win over a farther full-width row despite the alignment penalty.
  for (auto direction : {Direction::Up, Direction::Down, Direction::Left, Direction::Right}) {
    Scene scene;
    auto make = [&](int x, int y, int width, int height) {
      switch (direction) {
      case Direction::Up:
        return scene.button(x, 300 - y - height, width, height);
      case Direction::Left:
        return scene.button(300 - y - height, x, height, width);
      case Direction::Right:
        return scene.button(y, x, height, width);
      default:
        return scene.button(x, y, width, height);
      }
    };
    auto *current = make(0, 0, 40, 20);
    auto *near = make(150, 50, 20, 20);
    auto *far = make(0, 100, 200, 20);
    check(scene.choose(current, direction) == near, "Spatial navigation skipped an intervening row/column");
    lv_obj_add_flag(near, LV_OBJ_FLAG_HIDDEN);
    check(scene.choose(current, direction) == far, "Hidden spatial target remained eligible");
    lv_obj_clear_flag(near, LV_OBJ_FLAG_HIDDEN);
    ui::focus::SpatialPolicy policy;
    policy.excluded = near;
    check(scene.choose(current, direction, policy) == far, "Excluded edge target remained eligible");
    policy.excluded = nullptr;
    policy.context = near;
    policy.accept = [](lv_obj_t *candidate, void *excluded) { return candidate != excluded; };
    check(scene.choose(current, direction, policy) == far, "Screen policy was ignored");
    lv_obj_del(near);
    check(scene.choose(current, direction) == far, "Deleted target remained in spatial navigation");
  }
  {
    Scene scene;
    auto *current = scene.button(100, 0, 40, 20);
    scene.button(0, 50, 20, 30);
    auto *aligned = scene.button(100, 60, 40, 20);
    check(scene.choose(current, Direction::Down) == aligned, "Overlapping row band lost aligned candidate");
    auto *gear = scene.button(110, 30, 10, 10);
    lv_obj_add_flag(gear, LV_OBJ_FLAG_USER_2);
    check(scene.choose(current, Direction::Down) == aligned, "Vertical navigation entered secondary gear");
    lv_obj_set_pos(gear, 160, 0);
    check(scene.choose(current, Direction::Right) == gear, "Horizontal navigation could not enter gear");
    check(scene.choose(current, Direction::Up) == nullptr, "Navigation wrapped at a spatial edge");
  }
  {
    Scene scene;
    auto *region = lv_obj_create(scene.root);
    lv_obj_remove_style_all(region);
    lv_obj_set_size(region, 200, 200);
    lv_obj_clear_flag(region, LV_OBJ_FLAG_SCROLLABLE);
    auto *current = scene.button(0, 100, 50, 20, region);
    auto *prior = scene.button(0, 0, 50, 20, region);
    auto *header = scene.button(0, 70, 50, 20);
    ui::focus::SpatialPolicy policy;
    policy.preferredRegion = region;
    check(scene.choose(current, Direction::Up, policy) == prior,
          "Scrolling region lost focus to nearer header");
    lv_obj_del(prior);
    check(scene.choose(current, Direction::Up, policy) == header, "Region edge could not reach fixed header");
  }
  {
    Scene scene;
    auto *slider = lv_slider_create(scene.root);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, 50, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, sliderEvent, LV_EVENT_ALL, nullptr);
    previewCount = commitCount = 0;
    check(!ui::focus::adjustSlider(slider, Direction::Down), "Slider captured vertical navigation");
    check(ui::focus::adjustSlider(slider, Direction::Right), "Slider did not capture horizontal navigation");
    check(lv_slider_get_value(slider) == 55 && previewCount == 1 && commitCount == 1,
          "Slider step or preview/commit contract changed");
    lv_slider_set_value(slider, 99, LV_ANIM_OFF);
    ui::focus::adjustSlider(slider, Direction::Right);
    check(lv_slider_get_value(slider) == 100, "Slider exceeded upper bound");
    lv_slider_set_range(slider, 0, 1);
    ui::focus::adjustSlider(slider, Direction::Left);
    check(lv_slider_get_value(slider) == 0, "Slider minimum step or lower bound changed");
    lv_obj_remove_event_cb(slider, sliderEvent);
    lv_obj_add_event_cb(slider, sliderEvent, LV_EVENT_ALL, slider);
    const int priorCommits = commitCount;
    check(ui::focus::adjustSlider(slider, Direction::Right), "Deleted slider did not consume key");
    check(commitCount == priorCommits, "Commit dispatched after preview destroyed slider");
  }
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Spatial regression leaked UI roots");
}

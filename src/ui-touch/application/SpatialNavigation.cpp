// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpatialNavigation.h"
#include "../widgets/ObjectRef.h"
#include <limits.h>

namespace ui::focus {
namespace {
bool vertical(Direction direction) { return direction == Direction::Up || direction == Direction::Down; }
bool inside(lv_obj_t *object, lv_obj_t *root) {
  for (; object; object = lv_obj_get_parent(object))
    if (object == root)
      return true;
  return false;
}
bool metrics(const lv_area_t &a, const lv_area_t &b, Direction direction, long &primary, long &cross) {
  const long dx = (long(b.x1) + b.x2) / 2 - (long(a.x1) + a.x2) / 2;
  const long dy = (long(b.y1) + b.y2) / 2 - (long(a.y1) + a.y2) / 2;
  long hgap = b.x1 - a.x2;
  if (a.x1 - b.x2 > hgap)
    hgap = a.x1 - b.x2;
  if (hgap < 0)
    hgap = 0;
  long vgap = b.y1 - a.y2;
  if (a.y1 - b.y2 > vgap)
    vgap = a.y1 - b.y2;
  if (vgap < 0)
    vgap = 0;
  primary = vertical(direction) ? vgap : hgap;
  cross = vertical(direction) ? hgap : vgap;
  switch (direction) {
  case Direction::Up:
    return dy < 0;
  case Direction::Down:
    return dy > 0;
  case Direction::Left:
    return dx < 0;
  case Direction::Right:
    return dx > 0;
  }
  return false;
}
bool candidate(lv_obj_t *object, lv_obj_t *current, Direction direction, const SpatialPolicy &policy) {
  if (!object || object == current || object == policy.excluded ||
      lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN))
    return false;
  if (vertical(direction) && lv_obj_has_flag(object, policy.horizontalOnly))
    return false;
  return !policy.accept || policy.accept(object, policy.context);
}
} // namespace

lv_obj_t *spatialTarget(const FocusTargets &targets, lv_obj_t *current, Direction direction,
                        const SpatialPolicy &policy) {
  if (!current || lv_obj_get_group(current) != targets.group())
    return nullptr;
  lv_area_t origin;
  lv_obj_get_coords(current, &origin);
  lv_obj_t *region = nullptr;
  if (policy.preferredRegion) {
    for (int i = 0; i < targets.count(); ++i) {
      auto *object = targets.at(i);
      if (!candidate(object, current, direction, policy) || !inside(object, policy.preferredRegion))
        continue;
      lv_area_t bounds;
      lv_obj_get_coords(object, &bounds);
      long primary, cross;
      if (metrics(origin, bounds, direction, primary, cross)) {
        region = policy.preferredRegion;
        break;
      }
    }
  }
  // The nearest row/column sets a band. This prevents jumping past a narrow
  // switch to a farther, better-aligned control. Overlapping controls remain eligible.
  long bandLow = 0, bandHigh = 0, nearest = LONG_MAX, nearestCross = LONG_MAX;
  bool haveBand = false;
  for (int i = 0; i < targets.count(); ++i) {
    auto *object = targets.at(i);
    if (!candidate(object, current, direction, policy) || (region && !inside(object, region)))
      continue;
    lv_area_t bounds;
    lv_obj_get_coords(object, &bounds);
    long primary, cross;
    if (!metrics(origin, bounds, direction, primary, cross) || primary > nearest ||
        (primary == nearest && cross >= nearestCross))
      continue;
    nearest = primary;
    nearestCross = cross;
    bandLow = vertical(direction) ? bounds.y1 : bounds.x1;
    bandHigh = vertical(direction) ? bounds.y2 : bounds.x2;
    haveBand = true;
  }
  if (!haveBand)
    return nullptr;
  lv_obj_t *best = nullptr;
  long bestScore = LONG_MAX;
  for (int i = 0; i < targets.count(); ++i) {
    auto *object = targets.at(i);
    if (!candidate(object, current, direction, policy) || (region && !inside(object, region)))
      continue;
    lv_area_t bounds;
    lv_obj_get_coords(object, &bounds);
    long primary, cross;
    if (!metrics(origin, bounds, direction, primary, cross))
      continue;
    bool past = false;
    switch (direction) {
    case Direction::Up:
      past = bounds.y2 < bandLow;
      break;
    case Direction::Down:
      past = bounds.y1 > bandHigh;
      break;
    case Direction::Left:
      past = bounds.x2 < bandLow;
      break;
    case Direction::Right:
      past = bounds.x1 > bandHigh;
      break;
    }
    if (past)
      continue;
    const long score = primary + 8 * cross;
    if (score < bestScore) {
      bestScore = score;
      best = object;
    }
  }
  return best;
}

bool adjustSlider(lv_obj_t *current, Direction direction) {
  if (!current || vertical(direction) || !lv_obj_check_type(current, &lv_slider_class))
    return false;
  ui::widgets::ObjectRef slider;
  if (!slider.set(current))
    return true;
  const int32_t minimum = lv_slider_get_min_value(current), maximum = lv_slider_get_max_value(current);
  int32_t step = (maximum - minimum) / 20;
  if (step < 1)
    step = 1;
  int32_t value = lv_slider_get_value(current) + (direction == Direction::Right ? step : -step);
  if (value < minimum)
    value = minimum;
  else if (value > maximum)
    value = maximum;
  lv_slider_set_value(current, value, LV_ANIM_OFF);
  lv_event_send(current, LV_EVENT_VALUE_CHANGED, nullptr);
  if (slider.get())
    lv_event_send(slider.get(), LV_EVENT_RELEASED, nullptr);
  return true;
}
} // namespace ui::focus

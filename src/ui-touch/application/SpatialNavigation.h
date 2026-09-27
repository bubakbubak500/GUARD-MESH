// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FocusTargets.h"

namespace ui::focus {
enum class Direction { Up, Down, Left, Right };
struct SpatialPolicy {
  lv_obj_flag_t horizontalOnly = LV_OBJ_FLAG_USER_2;
  lv_obj_t *excluded = nullptr;
  // Search this subtree first; leave it only when no eligible target lies in direction.
  lv_obj_t *preferredRegion = nullptr;
  // Synchronous predicate only: must not mutate widgets or the target collection.
  bool (*accept)(lv_obj_t *candidate, void *context) = nullptr;
  void *context = nullptr;
};
// Borrows live targets and policy for this call. Does not change focus or scroll.
lv_obj_t *spatialTarget(const FocusTargets &, lv_obj_t *current, Direction, const SpatialPolicy & = {});
// A horizontal press edits a slider and emits preview/commit events. A preview
// callback may destroy the slider; in that case no commit is sent to a stale object.
bool adjustSlider(lv_obj_t *current, Direction);
} // namespace ui::focus

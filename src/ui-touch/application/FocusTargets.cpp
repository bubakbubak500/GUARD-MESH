// SPDX-License-Identifier: GPL-3.0-or-later
#include "FocusTargets.h"
#include "../platform/UiPlatform.h"
#include <cstring>
namespace ui {
FocusTargets::~FocusTargets() {
  clear();
  if (_group)
    lv_group_del(_group);
  platform::release(_objects);
}
bool FocusTargets::initialize() {
  if (!_group)
    _group = lv_group_create();
  return _group != nullptr;
}
bool FocusTargets::add(lv_obj_t *object) {
  if (!object || !_group)
    return false;
  for (size_t i = 0; i < _count; ++i)
    if (_objects[i] == object)
      return true;
  if (_count == _capacity) {
    const size_t capacity = _capacity ? _capacity * 2 : 32;
    auto **objects = static_cast<lv_obj_t **>(platform::allocate(capacity * sizeof(lv_obj_t *), false));
    if (!objects)
      return false;
    if (_count)
      std::memcpy(objects, _objects, _count * sizeof(lv_obj_t *));
    platform::release(_objects);
    _objects = objects;
    _capacity = capacity;
  }
  _objects[_count++] = object;
  if (!lv_obj_add_event_cb(object, deletedEvent, LV_EVENT_DELETE, this)) {
    --_count;
    return false;
  }
  // Add only after installing the observer: group focus callbacks may delete it.
  lv_group_add_obj(_group, object);
  for (size_t i = 0; i < _count; ++i)
    if (_objects[i] == object)
      return true;
  return false;
}
void FocusTargets::clear() {
  for (size_t i = 0; i < _count; ++i)
    lv_obj_remove_event_cb_with_user_data(_objects[i], deletedEvent, this);
  _count = 0;
  if (_group)
    lv_group_remove_all_objs(_group);
}
lv_obj_t *FocusTargets::at(int index) const {
  if (index < 0 || static_cast<size_t>(index) >= _count)
    return nullptr;
  auto *object = _objects[index];
  return lv_obj_get_group(object) == _group ? object : nullptr;
}
void FocusTargets::remove(lv_obj_t *object) {
  for (size_t i = 0; i < _count; ++i) {
    if (_objects[i] != object)
      continue;
    --_count;
    if (i < _count)
      std::memmove(_objects + i, _objects + i + 1, (_count - i) * sizeof(lv_obj_t *));
    if (_deleted)
      _deleted(object);
    return;
  }
}
void FocusTargets::deletedEvent(lv_event_t *event) {
  auto *owner = static_cast<FocusTargets *>(lv_event_get_user_data(event));
  owner->remove(lv_event_get_target(event));
}
} // namespace ui

// SPDX-License-Identifier: GPL-3.0-or-later
#include "KeyboardBinding.h"
#include "../i18n.h"
#include "../platform/UiPlatform.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "ObjectRef.h"
#include "Styles.h"
#include "TextSelection.h"
#include <cstring>
#include <memory>
namespace ui {
namespace widgets {
using namespace ui::theme;
namespace {
// Apply only the changed UTF-8 span. Restricted LVGL fields must be edited one
// native operation at a time: set_text loops internally after VALUE_CHANGED and
// cannot stop when a callback deletes the field or replaces the binding.
bool replaceText(lv_obj_t *field, const char *text, textSelection::StillCurrent current, void *context) {
  if (!field || (current && !current(context)))
    return false;
  if (!text)
    text = "";
  const char *old = lv_textarea_get_text(field);
  if (!strcmp(old, text))
    return true;
  // Normalize the complete requested value before finding the changed span.
  // Otherwise a shared suffix could consume the length budget and preserve text
  // that a whole-value replacement should truncate.
  std::unique_ptr<char, decltype(&platform::release)> desired(
      static_cast<char *>(platform::allocate(strlen(text) + 1, true)), platform::release);
  if (!desired)
    return false;
  const char *accepted = lv_textarea_get_accepted_chars(field);
  const uint32_t limit = lv_textarea_get_max_length(field);
  const bool oneLine = lv_textarea_get_one_line(field);
  uint32_t input = 0, characters = 0;
  size_t output = 0;
  while (text[input] && (!limit || characters < limit)) {
    const auto begin = input;
    const uint32_t codepoint = _lv_txt_encoded_next(text, &input);
    if (input <= begin || input - begin > 4)
      return false;
    if (oneLine && (codepoint == '\n' || codepoint == '\r'))
      continue;
    if (accepted) {
      uint32_t index = 0;
      bool allowed = false;
      while (accepted[index]) {
        if (_lv_txt_encoded_next(accepted, &index) == codepoint) {
          allowed = true;
          break;
        }
      }
      if (!allowed)
        continue;
    }
    memcpy(desired.get() + output, text + begin, input - begin);
    output += input - begin;
    ++characters;
  }
  desired.get()[output] = 0;
  text = desired.get();
  size_t first = 0, oldEnd = strlen(old), newEnd = strlen(text);
  while (first < oldEnd && first < newEnd && old[first] == text[first])
    ++first;
  if (first == oldEnd && first == newEnd)
    return true;
  auto continuation = [](char c) { return (static_cast<unsigned char>(c) & 0xc0) == 0x80; };
  while (first && continuation(old[first]))
    --first;
  const auto oldLength = oldEnd;
  while (oldEnd > first && newEnd > first && old[oldEnd - 1] == text[newEnd - 1]) {
    --oldEnd;
    --newEnd;
  }
  while (oldEnd < oldLength && continuation(old[oldEnd])) {
    ++oldEnd;
    ++newEnd;
  }
  const uint32_t begin = textSelection::byteToCp(old, first), end = textSelection::byteToCp(old, oldEnd);
  memmove(desired.get(), text + first, newEnd - first);
  desired.get()[newEnd - first] = 0;
  return textSelection::replace(field, begin, end, desired.get(), current, context);
}
struct EditGuard {
  const uint32_t &generation;
  uint32_t expected;
  ObjectRef &field;
  ObjectRef &keyboard;
  ObjectRef &mirror;
  static bool current(void *context) {
    const auto &guard = *static_cast<EditGuard *>(context);
    return guard.generation == guard.expected && guard.field.get() && guard.keyboard.get() &&
           guard.mirror.get();
  }
};
} // namespace
KeyboardBinding::~KeyboardBinding() {
  clear();
  lv_obj_t *old = _root;
  _root = _mirror = nullptr;
  if (old) {
    unwatch(old);
    lv_obj_del(old);
  }
}
void KeyboardBinding::watch(lv_obj_t *object) { lv_obj_add_event_cb(object, deleted, LV_EVENT_DELETE, this); }
void KeyboardBinding::unwatch(lv_obj_t *object) {
  if (object)
    while (lv_obj_remove_event_cb_with_user_data(object, deleted, this)) {
    }
}
void KeyboardBinding::ensureCreated(lv_coord_t top, lv_coord_t height) {
  if (_root && _mirror)
    return;
  if (_root) {
    lv_obj_t *old = _root;
    _root = nullptr;
    lv_obj_del(old);
  }
  _root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(_root);
  lv_obj_set_size(_root, 240, height);
  lv_obj_align(_root, LV_ALIGN_TOP_MID, 0, top);
  styleSurface(_root, colors().COLOR_PANEL, 8);
  lv_obj_set_style_pad_hor(_root, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(_root, 6, LV_PART_MAIN);
  lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(_root, LV_OBJ_FLAG_HIDDEN);
  watch(_root);
  lv_obj_t *hint = lv_label_create(_root);
  lv_label_set_text(hint, TR("Editing"));
  lv_obj_set_style_text_font(hint, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(hint, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 0, 0);
  _mirror = lv_textarea_create(_root);
  lv_obj_set_size(_mirror, SC(224), SC(30));
  lv_obj_align(_mirror, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_textarea_set_one_line(_mirror, true);
  lv_obj_set_style_bg_color(_mirror, lv_color_hex(colors().COLOR_FIELD), LV_PART_MAIN);
  lv_obj_set_style_text_color(_mirror, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_border_color(_mirror, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(_mirror, 1, LV_PART_MAIN);
  lv_obj_set_style_text_font(_mirror, &font14(), LV_PART_MAIN);
  watch(_mirror);
  lv_obj_add_event_cb(_mirror, changed, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(_mirror, changed, LV_EVENT_READY, this);
}
void KeyboardBinding::clear() {
  ++_generation;
  if (_mirror)
    lv_textarea_set_accepted_chars(_mirror, nullptr);
  if (_keyboard)
    lv_keyboard_set_textarea(_keyboard, nullptr);
  unwatch(_target);
  unwatch(_keyboard);
  _target = _keyboard = nullptr;
  _mirrored = false;
}
bool KeyboardBinding::bind(lv_obj_t *keyboard, lv_obj_t *target, bool mirrored) {
  if (!keyboard || !target || (mirrored && !_mirror))
    return false;
  ObjectRef requestedKeyboard, requestedTarget, requestedMirror;
  if (!requestedKeyboard.set(keyboard) || !requestedTarget.set(target) || !requestedMirror.set(_mirror))
    return false;
  const uint32_t generation = _generation;
  if (_target && _target != target)
    sync();
  if (_generation != generation || !requestedTarget.get() || !requestedKeyboard.get() ||
      (mirrored && !requestedMirror.get()))
    return false;
  clear();
  EditGuard guard{_generation, _generation, requestedTarget, requestedKeyboard, requestedMirror};
  if (mirrored) {
    lv_textarea_set_max_length(_mirror, lv_textarea_get_max_length(target));
    lv_textarea_set_password_mode(_mirror, lv_textarea_get_password_mode(target));
    lv_textarea_set_one_line(_mirror, true);
    if (!EditGuard::current(&guard) ||
        !replaceText(_mirror, lv_textarea_get_text(target), EditGuard::current, &guard) ||
        !EditGuard::current(&guard))
      return false;
    lv_textarea_set_cursor_pos(_mirror, lv_textarea_get_cursor_pos(target));
  }
  _target = target;
  _keyboard = keyboard;
  _mirrored = mirrored;
  watch(_target);
  watch(_keyboard);
  if (mirrored)
    lv_textarea_set_accepted_chars(_mirror, lv_textarea_get_accepted_chars(target));
  lv_keyboard_set_textarea(keyboard, mirrored ? _mirror : target);
  if (_generation != guard.expected || !requestedTarget.get() || !requestedKeyboard.get())
    return false;
  if (!mirrored)
    hide();
  return true;
}
void KeyboardBinding::sync() {
  if (!_mirrored || !_target || !_mirror || _syncing)
    return;
  ObjectRef destination, keyboard, mirror;
  if (!destination.set(_target) || !keyboard.set(_keyboard) || !mirror.set(_mirror))
    return;
  EditGuard guard{_generation, _generation, destination, keyboard, mirror};
  const auto cursor = lv_textarea_get_cursor_pos(_mirror);
  _syncing = true;
  if (replaceText(_target, lv_textarea_get_text(_mirror), EditGuard::current, &guard) &&
      EditGuard::current(&guard))
    lv_textarea_set_cursor_pos(destination.get(), cursor);
  _syncing = false;
}
void KeyboardBinding::setText(lv_obj_t *target, const char *text) {
  if (!target)
    return;
  ObjectRef destination;
  if (!destination.set(target))
    return;
  struct Guard {
    const uint32_t &generation;
    uint32_t expected;
    ObjectRef &destination;
    static bool current(void *context) {
      const auto &guard = *static_cast<Guard *>(context);
      return guard.generation == guard.expected && guard.destination.get();
    }
  } guard{_generation, _generation, destination};
  const bool syncing = _syncing;
  _syncing = true;
  if (replaceText(target, text, Guard::current, &guard) && Guard::current(&guard)) {
    lv_textarea_set_cursor_pos(destination.get(), LV_TEXTAREA_CURSOR_LAST);
    if (_mirrored && _target == destination.get() && _mirror) {
      ObjectRef keyboard, mirror;
      if (keyboard.set(_keyboard) && mirror.set(_mirror)) {
        EditGuard edit{_generation, _generation, destination, keyboard, mirror};
        if (replaceText(_mirror, lv_textarea_get_text(destination.get()), EditGuard::current, &edit) &&
            EditGuard::current(&edit))
          lv_textarea_set_cursor_pos(mirror.get(), LV_TEXTAREA_CURSOR_LAST);
      }
    }
  }
  _syncing = syncing;
}
void KeyboardBinding::changed(lv_event_t *event) {
  auto &self = *static_cast<KeyboardBinding *>(lv_event_get_user_data(event));
  if (lv_event_get_target(event) != self._mirror || !self._mirrored)
    return;
  if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
    self.sync();
    return;
  }
  auto *target = self._target;
  const uint32_t generation = self._generation;
  if (self._ready && self._ready())
    return;
  if (self._generation != generation)
    return;
  self.sync();
  if (target && self._target == target && self._generation == generation)
    lv_event_send(target, LV_EVENT_READY, nullptr);
}
void KeyboardBinding::deleted(lv_event_t *event) {
  auto &self = *static_cast<KeyboardBinding *>(lv_event_get_user_data(event));
  auto *object = lv_event_get_target(event);
  if (object == self._keyboard) {
    self._keyboard = nullptr;
    self.clear();
  } else if (object == self._target) {
    self._target = nullptr;
    self.clear();
    self.hide();
  } else if (object == self._root || object == self._mirror) {
    self.clear();
    if (object == self._root)
      self._root = nullptr;
    self._mirror = nullptr;
  }
}
void KeyboardBinding::resize(lv_coord_t width) {
  if (_root)
    lv_obj_set_width(_root, width);
  if (_mirror)
    lv_obj_set_width(_mirror, width - 16);
}
void KeyboardBinding::show(lv_coord_t top) {
  if (!_root)
    return;
  lv_obj_align(_root, LV_ALIGN_TOP_MID, 0, top);
  lv_obj_clear_flag(_root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(_root);
}
void KeyboardBinding::hide() {
  if (_root)
    lv_obj_add_flag(_root, LV_OBJ_FLAG_HIDDEN);
}
} // namespace widgets
} // namespace ui

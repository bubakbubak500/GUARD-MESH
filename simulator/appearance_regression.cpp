// SPDX-License-Identifier: GPL-3.0-or-later
#include "helpers/esp32/TouchPrefsStore.h"
#include "i18n.h"
#include "screens/AccentColorPicker.h"
#include "screens/LockSettingsScreen.h"
#include "theme/Theme.h"
#include "theme/TouchTheme.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
namespace {
using Picker = ui::screens::AccentColorPicker;
using LockScreen = ui::screens::LockSettingsScreen;
void check(bool ok, const char *text) {
  if (!ok)
    throw std::runtime_error(text);
}
lv_obj_t *findText(lv_obj_t *root, const char *text) {
  if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = findText(lv_obj_get_child(root, i), text))
      return found;
  return nullptr;
}
lv_obj_t *find(lv_obj_t *root, const char *text) {
  lv_obj_update_layout(root);
  return findText(root, text);
}
lv_obj_t *typed(lv_obj_t *root, const lv_obj_class_t *type) {
  if (lv_obj_check_type(root, type))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = typed(lv_obj_get_child(root, i), type))
      return found;
  return nullptr;
}
lv_obj_t *button(lv_obj_t *root, const char *text) {
  lv_obj_update_layout(root);
  auto *caption = find(root, text);
  if (!caption)
    throw std::runtime_error(std::string("Appearance button missing: ") + text);
  return lv_obj_get_parent(caption);
}
void click(lv_obj_t *object) { lv_event_send(object, LV_EVENT_CLICKED, nullptr); }
lv_obj_t *body() {
  auto *root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, 210, 240);
  return root;
}
lv_obj_t *latest() { return lv_obj_get_child(lv_layer_top(), -1); }
struct Context {
  Picker *picker = nullptr;
  bool fail = false, onRead = false, onSave = false, onSync = false, onClose = false, onApply = false,
       onAttach = false;
  unsigned saves = 0, applies = 0, restarts = 0, alerts = 0;
  uint32_t applied = 0;
  void reopen(bool &flag) {
    if (flag) {
      flag = false;
      picker->open();
    }
  }
};
Context *closing = nullptr;
Picker::Host host(Context &c) {
  return {&c,
          [](void *p) -> uint32_t {
            auto &c = *static_cast<Context *>(p);
            c.reopen(c.onRead);
            return touchPrefsGetAccentColor();
          },
          [](void *p, uint32_t value) {
            auto &c = *static_cast<Context *>(p);
            ++c.saves;
            const bool ok = !c.fail && touchPrefsSetAccentColor(value);
            c.reopen(c.onSave);
            return ok;
          },
          [](void *p, uint32_t value) {
            auto &c = *static_cast<Context *>(p);
            ++c.applies;
            c.applied = value;
            c.reopen(c.onApply);
          },
          [](void *p) { ++static_cast<Context *>(p)->restarts; },
          [](void *p, lv_obj_t *) {
            auto &c = *static_cast<Context *>(p);
            c.reopen(c.onAttach);
          },
          [](void *p) {
            auto &c = *static_cast<Context *>(p);
            c.reopen(c.onSync);
          },
          [](void *p, const char *, int) { ++static_cast<Context *>(p)->alerts; },
          [](lv_obj_t **root) {
            auto *old = *root;
            *root = nullptr;
            lv_obj_del_async(old);
            if (closing)
              closing->reopen(closing->onClose);
          },
          []() -> lv_coord_t { return 24; }};
}
void pickerRegression(void (*pump)(unsigned)) {
  Context c;
  Picker picker(host(c));
  c.picker = &picker;
  closing = &c;
  touchPrefsSetAccentColor(0x123456);
  picker.open();
  auto *first = latest();
  auto *field = typed(first, &lv_textarea_class);
  check(field, "Accent field missing");
  check(!strcmp(lv_textarea_get_text(field), "123456"), "Saved accent not loaded");
  lv_textarea_set_text(field, "abCDef");
  c.fail = true;
  click(button(first, TR("Save & restart")));
  check(c.saves == 1 && c.restarts == 0 && c.applies == 0 && picker.isOpen() &&
            touchPrefsGetAccentColor() == 0x123456,
        "Failed accent save applied/restarted");
  c.fail = false;
  lv_textarea_set_text(field, "123");
  click(button(first, TR("Save & restart")));
  check(c.saves == 1 && picker.isOpen(), "Partial hex saved stale previous choice");
  click(button(first, TR("Reset")));
  check(!strcmp(lv_textarea_get_text(field), "15B6A6"), "Accent reset lost default");
  lv_textarea_set_text(field, "00Aa12");
  click(button(first, TR("Save & restart")));
  check(!picker.isOpen() && c.restarts == 1 && c.applied == 0x00AA12 &&
            touchPrefsGetAccentColor() == 0x00AA12,
        "Accent save/apply/restart mismatch");
  const auto saves = c.saves;
  click(button(first, TR("Save & restart")));
  check(c.saves == saves, "Retired accent button retained callback");
  pump(2);
  picker.open();
  auto *second = latest();
  c.onSync = true;
  click(button(second, TR("Save & restart")));
  check(c.saves == saves && picker.isOpen(), "Reentrant sync saved old draft");
  pump(2);
  auto *third = latest();
  c.onSave = true;
  click(button(third, TR("Save & restart")));
  check(c.restarts == 1 && picker.isOpen(), "Reentrant save closed/restarted replacement");
  pump(2);
  auto *fourth = latest();
  c.onClose = true;
  click(button(fourth, TR("Save & restart")));
  check(c.restarts == 1 && picker.isOpen(), "Reentrant close restarted replacement");
  pump(2);
  auto *fifth = latest();
  c.onApply = true;
  click(button(fifth, TR("Save & restart")));
  check(c.restarts == 1 && picker.isOpen(), "Reentrant apply restarted replacement");
  pump(2);
  c.onRead = true;
  picker.open();
  pump(2);
  check(picker.isOpen(), "Read reentry lost picker");
  c.onAttach = true;
  picker.open();
  pump(2);
  check(picker.isOpen(), "Attach reentry lost picker");
  auto *root = latest();
  auto *grid = lv_obj_get_child(root, 2);
  check(lv_obj_get_child_cnt(grid) == ui::colorChoice::AccentCount, "Accent swatches missing");
  auto *swatch = lv_obj_get_child(grid, 2);
  click(swatch);
  check(!strcmp(lv_textarea_get_text(typed(root, &lv_textarea_class)), "3B82F6"),
        "Swatch did not update field");
  lv_obj_add_state(swatch, LV_STATE_FOCUSED | LV_STATE_PRESSED);
  check(lv_obj_get_style_bg_color(swatch, LV_PART_MAIN).full == lv_color_hex(0x3B82F6).full,
        "Focused swatch lost represented color");
  int deleted = 0;
  for (int i = 0; i < 3; ++i)
    lv_obj_add_event_cb(
        root, [](lv_event_t *e) { ++*static_cast<int *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE,
        &deleted);
  lv_obj_del(root);
  check(deleted == 3 && !picker.isOpen(), "Accent DELETE skipped observers");
  lv_obj_t *retained;
  {
    Picker temporary(host(c));
    c.picker = &temporary;
    temporary.open();
    retained = latest();
    c.onClose = true;
  }
  const auto before = c.saves;
  click(button(retained, TR("Save & restart")));
  check(c.saves == before, "Destroyed picker retained callbacks");
  c.picker = &picker;
  closing = nullptr;
  pump(2);
}
void lockRegression(void (*pump)(unsigned)) {
  struct LockContext {
    LockScreen *screen = nullptr;
    lv_obj_t *replacement = nullptr;
    unsigned changes = 0, opens = 0;
    bool replace = false;
  } c;
  ui::LockSettings settings({&c,
                             [](void *p, bool) {
                               auto &c = *static_cast<LockContext *>(p);
                               ++c.changes;
                               if (c.replace) {
                                 c.replace = false;
                                 c.screen->build(c.replacement, 202);
                               }
                             },
                             {true, true, true}});
  LockScreen screen(settings, {&c, [](void *p) { ++static_cast<LockContext *>(p)->opens; }, nullptr});
  c.screen = &screen;
  touchPrefsSetLockWallpaper("sd:/wall.jpg");
  auto *first = body();
  screen.build(first, 202);
  check(find(first, "SD: wall.jpg"), "Lock wallpaper label wrong");
  click(button(first, "SD: wall.jpg"));
  check(c.opens == 1, "Lock wallpaper action missing");
  auto *colors = lv_obj_get_child(first, 3);
  check(lv_obj_get_child_cnt(colors) == ui::colorChoice::LockCount, "Lock colors missing");
  click(lv_obj_get_child(colors, 3));
  check(touchPrefsGetLockTextColor() == ui::colorChoice::lock(3), "Lock color did not persist");
  check(!settings.setColor(999), "Invalid lock color accepted");
  auto *toggle = typed(first, &lv_switch_class);
  auto *second = c.replacement = body();
  c.replace = true;
  lv_obj_add_state(toggle, LV_STATE_CHECKED);
  lv_event_send(toggle, LV_EVENT_VALUE_CHANGED, nullptr);
  check(c.changes == 1 && touchPrefsGetLockOnScreenOff(), "Lock callback not applied");
  lv_event_send(toggle, LV_EVENT_VALUE_CHANGED, nullptr);
  check(c.changes == 1, "Old lock callback remained active");
  lv_obj_del(first);
  screen.wallpaperChanged("/lock/new.jpg");
  check(find(second, "new.jpg"), "Wallpaper change did not reach current form");
  int deleted = 0;
  for (int i = 0; i < 3; ++i)
    lv_obj_add_event_cb(
        second, [](lv_event_t *e) { ++*static_cast<int *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE,
        &deleted);
  lv_obj_del(second);
  screen.wallpaperChanged("/lock/gone.jpg");
  check(deleted == 3, "Lock DELETE skipped observers");
  auto *retained = body();
  {
    LockScreen temporary(settings, {nullptr, nullptr, nullptr});
    temporary.build(retained, 202);
  }
  toggle = typed(retained, &lv_switch_class);
  lv_event_send(toggle, LV_EVENT_VALUE_CHANGED, nullptr);
  check(c.changes == 1, "Destroyed lock owner retained callbacks");
  lv_obj_del(retained);
  ui::LockSettings limited({nullptr, nullptr, {false, false, false}});
  check(!limited.setLocked(false) && !limited.setColor(0), "Unsupported lock action accepted");
  LockScreen empty(limited, {nullptr, nullptr, nullptr});
  auto *last = body();
  empty.build(last, 202);
  check(!lv_obj_get_child_cnt(last), "No-unlock board exposes lock controls");
  lv_obj_del(last);
  char text[32];
  ui::LockSettings::wallpaperName(nullptr, text, sizeof text);
  check(!strcmp(text, "(default)"), "Default wallpaper name invalid");
  ui::LockSettings::wallpaperName("sd:/many/path/a.jpg", text, sizeof text);
  check(!strcmp(text, "SD: a.jpg"), "SD wallpaper basename invalid");
  ui::LockSettings::wallpaperName("abc", text, 1);
  check(!text[0], "Tiny name buffer not terminated");
  pump(2);
}
void themeRegression(void (*pump)(unsigned)) {
  auto *display = lv_disp_get_default();
  auto *original = lv_disp_get_theme(display);
  ui::theme::TouchTheme theme;
  theme.install(display);
  auto *installed = lv_disp_get_theme(display);
  theme.install(display);
  check(lv_disp_get_theme(display) == installed && installed->parent == original,
        "Repeated theme install formed parent cycle");
  auto *toggle = lv_switch_create(lv_layer_top());
  lv_obj_add_state(toggle, LV_STATE_CHECKED);
  // The stock switch theme animates the indicator color independently of knob anim_time.
  pump(LV_THEME_DEFAULT_TRANSITION_TIME + 100);
  check(lv_obj_get_style_bg_color(toggle, LV_PART_INDICATOR).full ==
            lv_color_hex(ui::theme::colors().COLOR_ACCENT).full,
        "Shared theme did not recolor switch");
  lv_obj_del(toggle);
  lv_disp_set_theme(display, original);
}
} // namespace
void runAppearanceRegression(void (*pump)(unsigned)) {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  const auto accent = touchPrefsGetAccentColor(), color = touchPrefsGetLockTextColor();
  const bool locked = touchPrefsGetLockOnScreenOff();
  char wallpaper[TOUCH_LOCK_WALLPAPER_MAXLEN];
  touchPrefsGetLockWallpaper(wallpaper, sizeof wallpaper);
  pickerRegression(pump);
  lockRegression(pump);
  themeRegression(pump);
  touchPrefsSetAccentColor(accent);
  touchPrefsSetLockTextColor(color);
  touchPrefsSetLockOnScreenOff(locked);
  touchPrefsSetLockWallpaper(wallpaper);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Appearance modules leaked roots");
  puts("Appearance: accent validation/save failure, reentrant picker lifetime, lock "
       "preferences/capabilities, swatch colors and idempotent theme passed.");
}

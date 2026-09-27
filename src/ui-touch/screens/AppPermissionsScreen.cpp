// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppPermissionsScreen.h"
#include "../device_caps.h"
#include "../application/LuaIntegration.h"
#include "../LuaAppHost.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../i18n.h"
#include <cstdio>
#if CAP_LUA_SDK_EXT
extern int luaHostAppPerms(const char*, bool*);
extern int luaHostSendPerm(const char*);
namespace ui { namespace screens {
using namespace theme;
AppPermissionsScreen::~AppPermissionsScreen() { if (_page) { detach(_page); lv_obj_clean(_page); } }
void AppPermissionsScreen::detach(lv_obj_t* object) {
  lv_obj_remove_event_cb_with_user_data(object, deleted, this);
  lv_obj_remove_event_cb(object, toggle);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i) detach(lv_obj_get_child(object, i));
}
bool AppPermissionsScreen::owns(lv_obj_t* object) const {
  while (object) { if (object == _page) return true; object = lv_obj_get_parent(object); }
  return false;
}
void AppPermissionsScreen::deleted(lv_event_t* event) {
  auto* self = static_cast<AppPermissionsScreen*>(lv_event_get_user_data(event));
  if (self && self->_page == lv_event_get_target(event)) self->_page = nullptr;
}
void AppPermissionsScreen::toggle(lv_event_t* event) {
  auto* permission = static_cast<Permission*>(lv_event_get_user_data(event));
  if (!permission || !permission->owner->owns(lv_event_get_target(event))) return;
  auto* self = permission->owner;
  if (permission->index >= 12 || !self->_ids[permission->index][0]) return;
  const char* id = self->_ids[permission->index];
  bool asked = false;
  int mask = luaHostAppPerms(id, &asked);
  if (lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED)) mask |= permission->bit;
  else mask &= ~permission->bit;
  ui::lua::writePermissions(id, mask);
}
void AppPermissionsScreen::build(lv_obj_t* page, lv_coord_t lblw) {
  if (_page) detach(_page);
  if (_page == page && page) lv_obj_clean(page);
  _page = page;
  if (!page) return;
  lv_obj_add_event_cb(page, deleted, LV_EVENT_DELETE, this);
  int y = 6;
  lv_obj_t* hint = lv_label_create(page);
  lv_label_set_text(hint, TR("What each installed app is allowed to do. Anything an app sends goes out "
                             "under your node name and cannot be told apart from a message you "
                             "typed. Private covers direct messages and room posts, and is kept "
                             "separate from channels on purpose. Turn one off to take it back."));
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(hint, lblw);
  lv_obj_set_style_text_font(hint, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(hint, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_pos(hint, 6, y);
  lv_obj_update_layout(hint);
  y += lv_obj_get_height(hint) + 10;

  // Collect ids: everything installed, plus anything already in perms.kv (an app
  // can be removed while its decision lives on -- that decision must stay visible
  // and revocable, or a reinstall silently inherits an old yes).
  auto& ids = _ids;
  const int n = ui::lua::permissionIds(ids, 12);

  if (n == 0) {
    lv_obj_t* none = lv_label_create(page);
    lv_label_set_text(none, TR("No apps installed."));
    lv_obj_set_style_text_font(none, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(none, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_pos(none, 6, y);
    return;
  }

  for (int i = 0; i < n; ++i) {
    const int perm = luaHostSendPerm(ids[i]);
    char row[40];
    snprintf(row, sizeof row, "%s", ids[i]);
    const int h = _label(page, y, 6, row, colors().COLOR_TEXT, nullptr, 56);
    bool asked = false;
    const int mask = luaHostAppPerms(ids[i], &asked);
    const int rowh = (h > 0 ? h : 18);
    // Four switches: speaking in your name vs reading what arrives, each split
    // again between channels and private conversations. They are different risks --
    // posting to a channel you are already in is not the same act as writing to one
    // person as you, and channel traffic is not your DMs -- so an app that wants
    // more than one has to be given each.
    struct { const char* label; int bit; } perms[] = {
      { TR("Post to channels as me"), LUA_PERM_SEND },
      { TR("Read channel messages"), LUA_PERM_READ },
      { TR("Send private messages as me"), LUA_PERM_DM_SEND },
      { TR("Read private messages"), LUA_PERM_DM_READ },
      { TR("Send discovery probes"), LUA_PERM_PROBE },
    };
    int sy = y + rowh + 2;
    for (int k = 0; k < (int)(sizeof perms / sizeof perms[0]); ++k) {
      lv_obj_t* pl = lv_label_create(page);
      lv_label_set_text(pl, perms[k].label);
      lv_obj_set_style_text_font(pl, &font12(), LV_PART_MAIN);
      lv_obj_set_style_text_color(pl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
      lv_obj_set_pos(pl, 16, sy + 4);
      lv_obj_t* sw = lv_switch_create(page);
      lv_obj_set_size(sw, 44, 24);
      lv_obj_set_pos(sw, lblw - 44 + 6, sy);
      if (mask & perms[k].bit) lv_obj_add_state(sw, LV_STATE_CHECKED);
      // user_data packs the app index and the bit, so one callback serves both.
      _permissions[i][k] = {this, static_cast<uint8_t>(i), static_cast<uint8_t>(perms[k].bit)};
      lv_obj_add_event_cb(sw, toggle, LV_EVENT_VALUE_CHANGED, &_permissions[i][k]);
      sy += 30;
    }
    if (!asked) {
      lv_obj_t* st = lv_label_create(page);
      lv_label_set_text(st, TR("not asked yet"));
      lv_obj_set_style_text_font(st, &font12(), LV_PART_MAIN);
      lv_obj_set_style_text_color(st, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
      lv_obj_set_pos(st, 16, sy);
      sy += 18;
    }
    y = sy + 8;
    (void)perm;
  }
}


} }
#endif

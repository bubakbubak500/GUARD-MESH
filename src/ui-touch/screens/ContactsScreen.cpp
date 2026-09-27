// SPDX-License-Identifier: GPL-3.0-or-later
#include "ContactsScreen.h"
#include "../platform/UiPlatform.h"
#include "../theme/Theme.h"
#include "../theme/Fonts.h"
#include "../widgets/Styles.h"
#include "../device_caps.h"
#include <cstdio>
#include <cstring>

namespace ui { namespace screens {
using namespace theme;
using namespace widgets;

ContactsScreen::~ContactsScreen() {
  if (_list) {
    lv_obj_remove_event_cb_with_user_data(_list, deleteEvent, this);
    lv_obj_remove_event_cb_with_user_data(_list, viewportEvent, this);
    clearList();
  }
  platform::release(_contexts);
  platform::release(_entries);
  platform::release(_cells);
}

int ContactsScreen::materializedRowCount() const {
  int n = 0;
  for (int i = 0; i < _cell_count; ++i) if (_cells[i].row >= 0) ++n;
  return n;
}
int ContactsScreen::materializedWidgetCount() const {
  int n = 0;
  for (int i = 0; i < _cell_count; ++i) {
    const Cell& c = _cells[i];
    if (c.row < 0) continue;
    n += (c.icon != nullptr) + (c.name != nullptr) + (c.age != nullptr) +
         (c.location != nullptr) + (c.star != nullptr);
    if (_selection) n += (c.box != nullptr) + (c.check != nullptr);
  }
  return n;
}

void ContactsScreen::resetRows() { clearList(); }
void ContactsScreen::clearList() {
  if (_list) lv_obj_clean(_list);
  _parking = _footer = nullptr;
  _rows = _cell_count = 0;
  if (_cells) for (int i = 0; i < MaxRows; ++i) _cells[i] = Cell{};
  if (_contexts) for (int i = 0; i < MaxRows; ++i)
    _contexts[i].row = _contexts[i].age_lbl = _contexts[i].name_lbl = nullptr;
}
void ContactsScreen::deleteEvent(lv_event_t* event) {
  auto* self = static_cast<ContactsScreen*>(lv_event_get_user_data(event));
  if (self && self->_list == lv_event_get_target(event)) {
    self->_list = self->_parking = self->_footer = nullptr;
    self->_rows = self->_cell_count = 0;
    if (self->_cells) for (int i = 0; i < MaxRows; ++i) self->_cells[i] = Cell{};
  }
}
void ContactsScreen::widgetDeleteEvent(lv_event_t* event) {
  auto** slot = static_cast<lv_obj_t**>(lv_event_get_user_data(event));
  if (slot && *slot == lv_event_get_target(event)) *slot = nullptr;
}
void ContactsScreen::track(lv_obj_t* object, lv_obj_t** slot) {
  *slot = object;
  if (object) lv_obj_add_event_cb(object, widgetDeleteEvent, LV_EVENT_DELETE, slot);
}
void ContactsScreen::viewportEvent(lv_event_t* event) {
  auto* self = static_cast<ContactsScreen*>(lv_event_get_user_data(event));
  const lv_event_code_t code = lv_event_get_code(event);
  if (self && self->_list == lv_event_get_target(event) &&
      (code == LV_EVENT_SCROLL || code == LV_EVENT_SCROLL_END || code == LV_EVENT_SIZE_CHANGED))
    self->syncVisible();
}
void ContactsScreen::rowFocusEvent(lv_event_t* event) {
  auto* ctx = static_cast<RowContext*>(lv_event_get_user_data(event));
  if (ctx && ctx->owner) ctx->owner->syncVisible();
}

bool ContactsScreen::createCell(Cell& c) {
  if (!_parking) return false;
  c = Cell{};
  const auto fail = [&]() {
    if (c.box) lv_obj_del(c.box);
    if (c.icon) lv_obj_del(c.icon);
    if (c.name) lv_obj_del(c.name);
    if (c.age) lv_obj_del(c.age);
    if (c.location) lv_obj_del(c.location);
    if (c.star) lv_obj_del(c.star);
    c = Cell{};
    return false;
  };
  track(lv_obj_create(_parking), &c.box);
  if (!c.box) return false;
  lv_obj_remove_style_all(c.box);
  lv_obj_set_size(c.box, 20, 20);
  lv_obj_set_style_radius(c.box, 4, LV_PART_MAIN);
  lv_obj_set_style_border_width(c.box, 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(c.box, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_clear_flag(c.box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  track(lv_label_create(c.box), &c.check);
  if (!c.check) return fail();
  lv_label_set_text(c.check, LV_SYMBOL_OK);
  lv_obj_set_style_text_font(c.check, &font12(), LV_PART_MAIN);
#if defined(HAS_TDECK_PRO)
  lv_obj_set_style_text_color(c.check, lv_color_white(), LV_PART_MAIN);
#else
  lv_obj_set_style_text_color(c.check, lv_color_hex(0x062019), LV_PART_MAIN);
#endif
  lv_obj_center(c.check);
  track(lv_label_create(_parking), &c.icon);
  if (!c.icon) return fail();
  lv_obj_set_style_text_font(c.icon, &font16(), LV_PART_MAIN);
  track(lv_label_create(_parking), &c.name);
  if (!c.name) return fail();
  useChainedFont(c.name);
  lv_obj_set_style_text_font(c.name, &font14(), LV_PART_MAIN);
  track(lv_label_create(_parking), &c.age);
  if (!c.age) return fail();
  lv_obj_set_style_text_font(c.age, &font12(), LV_PART_MAIN);
  lv_label_set_long_mode(c.age, LV_LABEL_LONG_CLIP);
  track(lv_label_create(_parking), &c.location);
  if (!c.location) return fail();
  lv_obj_set_style_text_font(c.location, &font12(), LV_PART_MAIN);
  lv_label_set_long_mode(c.location, LV_LABEL_LONG_CLIP);
  track(lv_label_create(_parking), &c.star);
  if (!c.star) return fail();
  lv_label_set_text(c.star, TOUCH_SYM_STAR_BIG);
  lv_obj_set_style_text_font(c.star, &star_font_14, LV_PART_MAIN);
  return true;
}
void ContactsScreen::parkCell(Cell& c) {
  if (c.row >= 0 && c.row < MaxRows && _contexts) {
    _contexts[c.row].age_lbl = nullptr;
    _contexts[c.row].name_lbl = nullptr;
  }
  c.row = -1;
  if (!_parking) return;
  if (c.box && lv_obj_get_parent(c.box) != _parking) lv_obj_set_parent(c.box, _parking);
  if (c.icon && lv_obj_get_parent(c.icon) != _parking) lv_obj_set_parent(c.icon, _parking);
  if (c.name && lv_obj_get_parent(c.name) != _parking) lv_obj_set_parent(c.name, _parking);
  if (c.age && lv_obj_get_parent(c.age) != _parking) lv_obj_set_parent(c.age, _parking);
  if (c.location && lv_obj_get_parent(c.location) != _parking) lv_obj_set_parent(c.location, _parking);
  if (c.star && lv_obj_get_parent(c.star) != _parking) lv_obj_set_parent(c.star, _parking);
}
void ContactsScreen::bindCell(Cell& c, int row) {
  lv_obj_t* anchor = _contexts[row].row;
  if (!anchor) return;
  // The host repaints child 0 after a selection tap.
  if (_selection) lv_obj_set_parent(c.box, anchor);
  lv_obj_set_parent(c.icon, anchor);
  lv_obj_set_parent(c.name, anchor);
  lv_obj_set_parent(c.age, anchor);
  lv_obj_set_parent(c.location, anchor);
  lv_obj_set_parent(c.star, anchor);
  c.row = row;
  _contexts[row].name_lbl = c.name;
  _contexts[row].age_lbl = c.age;
  updateCell(c, true);
}
void ContactsScreen::updateCell(Cell& c, bool lookupLatest) {
  if (c.row < 0 || c.row >= _rows || !c.name || !c.age) return;
  ContactEntry& e = _entries[c.row];
  if (lookupLatest && _lookup) {
    ContactEntry latest{};
    if (_lookup(e.key6, latest)) {
      e.last_heard = latest.last_heard;
      std::memcpy(e.name, latest.name, sizeof(e.name));
      e.name[sizeof(e.name) - 1] = '\0';
    }
  }
  const bool wide = _row_width >= 400;
  const bool mid = !wide && _row_width >= 280;
  const int star_x = _row_width - 18;
  const int loc_w = wide ? 80 : (mid ? 70 : 46);
  const int heard_w = wide ? 64 : (mid ? 48 : 32);
  const int gap = wide ? 10 : (mid ? 8 : 2);
  const int loc_x = star_x - (wide ? 8 : (mid ? 6 : 4)) - loc_w;
  const int heard_x = loc_x - gap - heard_w;
  const int icon_x = _selection ? 34 : 8;
  const int name_x = icon_x + (mid ? 24 : 20);
  const int line_h = lv_font_get_line_height(&font14());
  const int row2_y = 5 + line_h + 2;
  const int name_w = heard_x - name_x - 6 > 50 ? heard_x - name_x - 6 : 50;
  _name_width = name_w;
  _two_line = mid;
  if (_selection) {
    lv_obj_align(c.box, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_set_style_border_color(c.box, lv_color_hex(e.is_fav ? 0x33383E : colors().COLOR_SUB), LV_PART_MAIN);
    const bool checked = _host.selected(e.key6);
    lv_obj_set_style_bg_opa(c.box, checked ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    if (checked) lv_obj_clear_flag(c.check, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(c.check, LV_OBJ_FLAG_HIDDEN);
  }
  lv_label_set_text(c.icon, e.is_blocked ? TOUCH_SYM_PERSON
                       : (e.is_repeater ? TOUCH_SYM_ANTENNA
                       : (e.is_room ? LV_SYMBOL_LOOP : TOUCH_SYM_PERSON)));
  lv_obj_set_style_text_color(c.icon, lightSurfaceTextColor(e.is_blocked ? 0xD7574E : colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_align(c.icon, LV_ALIGN_LEFT_MID, icon_x, 0);
  char name[40];
  _host.sanitize(&font14(), name, sizeof(name), e.name);
  if (mid) {
    lv_label_set_long_mode(c.name, LV_LABEL_LONG_DOT);
    lv_obj_set_size(c.name, star_x - 6 - name_x, line_h);
  } else {
    lv_point_t size;
    lv_txt_get_size(&size, name, &font14(), 0, 0, name_w, LV_TEXT_FLAG_NONE);
    lv_label_set_long_mode(c.name, size.y > 2 * line_h ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT);
    lv_obj_set_width(c.name, name_w);
  }
  if (std::strcmp(lv_label_get_text(c.name), name)) lv_label_set_text(c.name, name);
  lv_obj_set_style_text_color(c.name, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  if (mid) lv_obj_align(c.name, LV_ALIGN_TOP_LEFT, name_x, 5);
  else lv_obj_align(c.name, LV_ALIGN_LEFT_MID, name_x, 0);
  char age[12];
  const uint32_t age_secs = _now > e.last_heard && e.last_heard ? _now - e.last_heard : 0;
  _host.formatAge(age, sizeof(age), age_secs);
  if (std::strcmp(lv_label_get_text(c.age), age)) lv_label_set_text(c.age, age);
  lv_obj_set_style_text_color(c.age, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_width(c.age, heard_w);
  if (mid) lv_obj_align(c.age, LV_ALIGN_TOP_LEFT, name_x, row2_y);
  else lv_obj_align(c.age, LV_ALIGN_LEFT_MID, heard_x, 0);
  char location[16];
  if (e.has_gps) {
    char distance[12];
    _host.formatDistance(distance, sizeof(distance), _latitude, _longitude, e.gps_lat, e.gps_lon);
    std::snprintf(location, sizeof(location), "%s", distance[0] ? distance : "GPS");
  } else std::snprintf(location, sizeof(location), "-");
  if (std::strcmp(lv_label_get_text(c.location), location)) lv_label_set_text(c.location, location);
  lv_obj_set_style_text_color(c.location, lightSurfaceTextColor(e.has_gps ? colors().COLOR_SUB : 0x4A4E54), LV_PART_MAIN);
  lv_obj_set_width(c.location, loc_w);
  if (mid) lv_obj_align(c.location, LV_ALIGN_TOP_LEFT, name_x + heard_w + gap, row2_y);
  else lv_obj_align(c.location, LV_ALIGN_LEFT_MID, loc_x, 0);
  lv_obj_set_style_text_color(c.star, lightSurfaceTextColor(0xC9A24A), LV_PART_MAIN);
  lv_obj_align(c.star, LV_ALIGN_LEFT_MID, star_x, 0);
  if (e.is_fav) lv_obj_clear_flag(c.star, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(c.star, LV_OBJ_FLAG_HIDDEN);
}
void ContactsScreen::syncVisible() {
  if (_binding || !_list || !_parking || !_contexts || !_cells) return;
  _binding = true;
  lv_obj_update_layout(_list);
  lv_area_t viewport;
  lv_obj_get_content_coords(_list, &viewport);
  const int overscan = _row_height;
  bool wanted[MaxRows] = {};
  for (int i = 0; i < _rows; ++i) {
    lv_obj_t* row = _contexts[i].row;
    if (!row) continue;
    lv_area_t area;
    lv_obj_get_coords(row, &area);
    wanted[i] = (area.y2 >= viewport.y1 - overscan && area.y1 <= viewport.y2 + overscan) ||
                lv_obj_has_state(row, LV_STATE_FOCUSED);
  }
  if (_rows && viewport.y2 <= viewport.y1) wanted[0] = true;
  for (int i = 0; i < _cell_count; ++i)
    if (_cells[i].row >= 0 && !wanted[_cells[i].row]) parkCell(_cells[i]);
  for (int row = 0; row < _rows; ++row) {
    if (!wanted[row]) continue;
    bool bound = false;
    for (int i = 0; i < _cell_count; ++i) if (_cells[i].row == row) { bound = true; break; }
    if (bound) continue;
    Cell* free_cell = nullptr;
    for (int i = 0; i < _cell_count; ++i) if (_cells[i].row < 0) { free_cell = &_cells[i]; break; }
    if (!free_cell && _cell_count < MaxRows) {
      free_cell = &_cells[_cell_count++];
      if (!createCell(*free_cell)) { --_cell_count; break; }
    }
    if (free_cell) bindCell(*free_cell, row);
  }
  _binding = false;
}
void ContactsScreen::render(lv_obj_t* list, const ContactEntry* entries, int count,
                            uint32_t now, double latitude, double longitude, bool selection) {
  if (!list || (count > 0 && !entries)) return;
  if (!_contexts) {
    _contexts = static_cast<RowContext*>(platform::allocate(sizeof(RowContext) * MaxRows, true));
    if (!_contexts) _contexts = static_cast<RowContext*>(platform::allocate(sizeof(RowContext) * MaxRows, false));
    if (_contexts) std::memset(_contexts, 0, sizeof(RowContext) * MaxRows);
  }
  if (!_entries) _entries = static_cast<ContactEntry*>(platform::allocate(sizeof(ContactEntry) * MaxRows, true));
  if (!_entries) _entries = static_cast<ContactEntry*>(platform::allocate(sizeof(ContactEntry) * MaxRows, false));
  if (!_cells) {
    _cells = static_cast<Cell*>(platform::allocate(sizeof(Cell) * MaxRows, true));
    if (!_cells) _cells = static_cast<Cell*>(platform::allocate(sizeof(Cell) * MaxRows, false));
    if (_cells) for (int i = 0; i < MaxRows; ++i) _cells[i] = Cell{};
  }
  if (!_contexts || !_entries || !_cells) {
    lv_obj_clean(list);
    lv_list_add_text(list, "Contacts unavailable (low memory)");
    return;
  }
  if (_list != list) {
    if (_list) {
      lv_obj_remove_event_cb_with_user_data(_list, deleteEvent, this);
      lv_obj_remove_event_cb_with_user_data(_list, viewportEvent, this);
      clearList();
    }
    _list = list;
    _rows = _cell_count = 0;
    lv_obj_add_event_cb(list, deleteEvent, LV_EVENT_DELETE, this);
    lv_obj_add_event_cb(list, viewportEvent, LV_EVENT_ALL, this);
  }
  if (!_parking) {
    clearList(); // External lv_obj_clean() may have removed all children.
    track(lv_obj_create(list), &_parking);
    lv_obj_remove_style_all(_parking);
    lv_obj_add_flag(_parking, LV_OBJ_FLAG_HIDDEN);
  }
  _binding = true;
  if (_footer) { lv_obj_del(_footer); _footer = nullptr; }
  for (int i = 0; i < _cell_count; ++i) parkCell(_cells[i]);
  const int rows = count > MaxRows ? MaxRows : (count < 0 ? 0 : count);
  const int width = lv_disp_get_hor_res(nullptr);
  const int height = width >= 280 && width < 400 ? 46 : 34;
  _row_width = width;
  _row_height = height;
  _selection = selection;
  _now = now;
  _latitude = latitude;
  _longitude = longitude;
  for (int i = _rows - 1; i >= rows; --i) {
    if (_contexts[i].row) lv_obj_del(_contexts[i].row);
    _contexts[i].row = nullptr;
  }
  for (int i = 0; i < rows; ++i) {
    _entries[i] = entries[i];
    RowContext& ctx = _contexts[i];
    ctx.owner = this;
    ctx.mesh_idx = static_cast<uint32_t>(entries[i].mesh_idx);
    ctx.is_repeater = entries[i].is_repeater;
    ctx.is_fav = entries[i].is_fav;
    std::memcpy(ctx.key6, entries[i].key6, sizeof(ctx.key6));
    ctx.age_lbl = ctx.name_lbl = nullptr;
    if (!ctx.row) {
      lv_obj_t* row = lv_obj_create(list);
      lv_obj_remove_style_all(row);
      lv_obj_set_style_bg_color(row, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_bg_color(row, lv_color_hex(colors().COLOR_CONTROL_PRESSED), LV_PART_MAIN | LV_STATE_PRESSED);
      lv_obj_set_style_border_color(row, lv_color_hex(colors().COLOR_CONTROL_PRESSED), LV_PART_MAIN);
      lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
      lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
      lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
      lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(row, _host.rowTap, LV_EVENT_CLICKED, &ctx);
      lv_obj_add_event_cb(row, rowFocusEvent, LV_EVENT_FOCUSED, &ctx);
      track(row, &ctx.row);
    }
    lv_obj_set_size(ctx.row, width, height);
  }
  _rows = rows;
  if (count > MaxRows) {
    char more[56];
    std::snprintf(more, sizeof(more), "+%d more — search to narrow the list", count - MaxRows);
    track(lv_list_add_text(list, more), &_footer);
    lv_obj_set_style_text_color(_footer, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_align(_footer, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_footer, 8, LV_PART_MAIN);
  } else if (!count) {
    track(lv_list_add_text(list, "No matching contacts"), &_footer);
    lv_obj_set_style_text_color(_footer, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_pad_all(_footer, 20, LV_PART_MAIN);
  }
  _binding = false;
  syncVisible();
}
bool ContactsScreen::refresh(uint32_t now, Lookup lookup) {
  if (!_list || !_parking || !lookup) return false;
  _now = now;
  _lookup = lookup;
  syncVisible();
  for (int i = 0; i < _cell_count; ++i) {
    Cell& c = _cells[i];
    if (c.row < 0) continue;
    if (!c.name || !c.age || !_contexts[c.row].row) return false;
    updateCell(c, true);
  }
  return true;
}
} }

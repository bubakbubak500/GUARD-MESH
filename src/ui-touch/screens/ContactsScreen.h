// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "lvgl.h"
#include "../models/ContactModel.h"
namespace ui { namespace screens {
// Renders immutable contact snapshots. Mesh lookups and user actions are supplied
// by the coordinator; the screen owns row callback data and its cached labels.
class ContactsScreen {
public:
  static constexpr int MaxRows = 128;
struct RowContext {
  ContactsScreen* owner;
  uint32_t mesh_idx;
  bool     is_repeater;
  bool     is_fav;       // favorites can't be multi-select-deleted (unfavorite first)
  uint8_t  key6[6];      // stable identity for the multi-select set (pub_key prefix)
  lv_obj_t* age_lbl;     // the row's Heard label — updated in place on the 60s age tick (#82)
  lv_obj_t* name_lbl;    // the row's Name label — updated in place on an advert name-fill (#463)
  lv_obj_t* row;
};
  struct Host {
    bool (*selected)(const uint8_t*);
    void (*sanitize)(const lv_font_t*, char*, size_t, const char*);
    void (*formatAge)(char*, size_t, uint32_t);
    void (*formatDistance)(char*, size_t, double, double, int32_t, int32_t);
    lv_event_cb_t rowTap;
  };
  using Lookup = bool (*)(const uint8_t*, ContactEntry&);
  explicit ContactsScreen(Host host) : _host(host) {}
  ~ContactsScreen();
  ContactsScreen(const ContactsScreen&) = delete;
  ContactsScreen& operator=(const ContactsScreen&) = delete;
  void render(lv_obj_t* list, const ContactEntry* entries, int count, uint32_t now,
              double latitude, double longitude, bool selection);
  bool refresh(uint32_t now, Lookup lookup);
  void resetRows();
  void updateViewport() { syncVisible(); }
  int rowCount() const { return _rows; }
  int materializedRowCount() const;
  int materializedWidgetCount() const;
  int allocatedCellCount() const { return _cell_count; }
private:
  struct Cell {
    lv_obj_t *box = nullptr, *check = nullptr, *icon = nullptr, *name = nullptr;
    lv_obj_t *age = nullptr, *location = nullptr, *star = nullptr;
    int row = -1;
  };
  static void deleteEvent(lv_event_t* event);
  static void viewportEvent(lv_event_t* event);
  static void rowFocusEvent(lv_event_t* event);
  static void widgetDeleteEvent(lv_event_t* event);
  void clearList();
  void syncVisible();
  bool createCell(Cell& cell);
  void parkCell(Cell& cell);
  void bindCell(Cell& cell, int row);
  void updateCell(Cell& cell, bool lookupLatest);
  void track(lv_obj_t* object, lv_obj_t** slot);
  Host _host;
  RowContext* _contexts = nullptr;
  ContactEntry* _entries = nullptr;
  Cell* _cells = nullptr;
  lv_obj_t* _list = nullptr;
  lv_obj_t* _parking = nullptr;
  lv_obj_t* _footer = nullptr;
  int _rows = 0, _name_width = 0;
  int _cell_count = 0, _row_height = 34, _row_width = 0;
  bool _two_line = false;
  bool _selection = false, _binding = false;
  uint32_t _now = 0;
  double _latitude = 0, _longitude = 0;
  Lookup _lookup = nullptr;
};
} }

// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/ContactsScreen.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
using Screen = ui::screens::ContactsScreen;
bool selected[256] = {};
char currentName[256][40] = {};
int lookupCalls = 0;
int tapped = -1;

void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
bool isSelected(const uint8_t* key) { return selected[key[0]]; }
void sanitize(const lv_font_t*, char* out, size_t cap, const char* in) {
  std::snprintf(out, cap, "%s", in);
}
void age(char* out, size_t cap, uint32_t seconds) {
  std::snprintf(out, cap, "%lu", static_cast<unsigned long>(seconds));
}
void distance(char* out, size_t cap, double, double, int32_t, int32_t) {
  std::snprintf(out, cap, "1km");
}
void rowTap(lv_event_t* event) {
  auto* ctx = static_cast<Screen::RowContext*>(lv_event_get_user_data(event));
  require(ctx != nullptr, "Contact row lost its stable callback context");
  tapped = ctx->key6[0];
  if (ctx->is_fav) return;
  selected[tapped] = !selected[tapped];
  // Match UITask's child-0 checkbox repaint contract.
  auto* row = lv_event_get_current_target(event);
  auto* box = lv_obj_get_child(row, 0);
  require(box && !lv_obj_check_type(box, &lv_label_class), "Selection checkbox is not row child 0");
  lv_obj_set_style_bg_opa(box, selected[tapped] ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
  auto* check = lv_obj_get_child(box, 0);
  require(check != nullptr, "Selection checkmark missing");
  if (selected[tapped]) lv_obj_clear_flag(check, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(check, LV_OBJ_FLAG_HIDDEN);
}
bool lookup(const uint8_t* key, ui::ContactEntry& out) {
  ++lookupCalls;
  out.last_heard = 490;
  std::snprintf(out.name, sizeof(out.name), "%s", currentName[key[0]]);
  return true;
}
lv_obj_t* text(lv_obj_t* root, const char* value) {
  if (!root) return nullptr;
  if (lv_obj_check_type(root, &lv_label_class) &&
      std::strcmp(lv_label_get_text(root), value) == 0) return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto* found = text(lv_obj_get_child(root, i), value)) return found;
  return nullptr;
}
lv_obj_t* row(lv_obj_t* list, int index) {
  // Child 0 is the hidden pool parking object; all 128 anchors remain in order.
  return lv_obj_get_child(list, index + 1);
}
void sync(lv_obj_t* list) {
  lv_obj_update_layout(list);
  lv_event_send(list, LV_EVENT_SCROLL_END, nullptr);
}
lv_obj_t* makeList(lv_obj_t* parent, int height) {
  auto* list = lv_list_create(parent);
  lv_obj_set_size(list, lv_disp_get_hor_res(nullptr), height);
  lv_obj_set_pos(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(list, 1, LV_PART_MAIN);
  return list;
}
}

void runContactsPerformanceRegression() {
  const uint32_t rootsBefore = lv_obj_get_child_cnt(lv_layer_top());
  auto* root = lv_obj_create(lv_layer_top());
  lv_obj_set_size(root, lv_disp_get_hor_res(nullptr), 240);
  lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
  auto* list = makeList(root, 150);
  ui::ContactEntry entries[130] = {};
  for (int i = 0; i < 130; ++i) {
    entries[i].mesh_idx = i;
    entries[i].key6[0] = static_cast<uint8_t>(i);
    entries[i].last_heard = 450;
    std::snprintf(entries[i].name, sizeof(entries[i].name), "Node %03d", i);
    std::snprintf(currentName[i], sizeof(currentName[i]), "Node %03d", i);
  }
  Screen screen({isSelected, sanitize, age, distance, rowTap});
  screen.render(list, entries, 130, 500, 0, 0, true);
  sync(list);
  require(screen.rowCount() == 128, "128-row cap changed");
  require(lv_obj_get_child_cnt(list) == 130, "Anchors or overflow notice missing");
  require(std::strchr(lv_label_get_text(lv_obj_get_child(list, 129)), '2') != nullptr,
          "Overflow notice missing");
  require(screen.materializedRowCount() > 0 && screen.materializedRowCount() <= 10,
          "Initial materialized rows exceed viewport and overscan");
  require(screen.materializedWidgetCount() <= 70, "128 rows materialized full widget trees");
  const int initialPool = screen.allocatedCellCount();
  std::printf("contacts: logical=%d initial_pool=%d visible=%d widgets=%d\n",
              screen.rowCount(), initialPool, screen.materializedRowCount(),
              screen.materializedWidgetCount());
  require(text(row(list, 0), "Node 000"), "First sorted snapshot not visible");
  entries[0].name[0] = 'X';
  require(text(row(list, 0), "Node 000"), "Screen retained borrowed snapshot data");
  lv_event_send(row(list, 0), LV_EVENT_CLICKED, nullptr);
  require(tapped == 0 && selected[0], "First row selection failed");

  lv_obj_scroll_to_y(list, LV_COORD_MAX, LV_ANIM_OFF);
  sync(list);
  require(text(row(list, 127), "Node 127"), "Tail row was not materialized on scroll");
  require(screen.materializedRowCount() <= 10 && screen.allocatedCellCount() <= initialPool + 2,
          "Scrolling grew the content pool with list length");
  std::printf("contacts: tail_pool=%d visible=%d widgets=%d\n",
              screen.allocatedCellCount(), screen.materializedRowCount(),
              screen.materializedWidgetCount());
  lv_event_send(row(list, 127), LV_EVENT_CLICKED, nullptr);
  require(tapped == 127 && selected[127], "Tail row callback identity changed");
  auto* group = lv_group_create();
  lv_group_add_obj(group, row(list, 127));
  lv_group_focus_obj(row(list, 127));
  sync(list);
  require(text(row(list, 127), "Node 127"), "Focused keyboard tail lost its content");
  lv_group_del(group);

  std::snprintf(currentName[127], sizeof(currentName[127]), "Updated tail");
  lookupCalls = 0;
  require(screen.refresh(510, lookup), "Refresh rejected a valid virtual row list");
  require(text(row(list, 127), "Updated tail"), "Visible advert name was not refreshed");
  require(lookupCalls <= 10, "Refresh looked up every offscreen contact");
  lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
  sync(list);
  require(text(row(list, 0), "Node 000"), "Scrolling back failed to restore first row");
  auto* box = lv_obj_get_child(row(list, 0), 0);
  require(box && lv_obj_get_style_bg_opa(box, LV_PART_MAIN) == LV_OPA_COVER &&
          !lv_obj_has_flag(lv_obj_get_child(box, 0), LV_OBJ_FLAG_HIDDEN),
          "Selection checkbox did not survive recycling");
  require(screen.allocatedCellCount() <= initialPool + 2, "Repeated scroll failed to reuse widgets");

  // A changed sort order reuses anchor identities but changes their contexts.
  for (int i = 0; i < 64; ++i) {
    ui::ContactEntry tmp = entries[i];
    entries[i] = entries[127 - i];
    entries[127 - i] = tmp;
  }
  lv_obj_t* firstAnchor = row(list, 0);
  screen.render(list, entries, 128, 520, 0, 0, true);
  sync(list);
  require(row(list, 0) == firstAnchor && text(firstAnchor, "Updated tail"),
          "Sort rerender rebuilt anchors or retained stale contents");
  lv_event_send(firstAnchor, LV_EVENT_CLICKED, nullptr);
  require(tapped == 127 && !selected[127], "Sorted row retained stale callback identity");

  screen.render(list, entries, 2, 520, 0, 0, false);
  require(screen.rowCount() == 2 && lv_obj_get_child_cnt(list) == 3,
          "Count shrink retained stale anchors");
  screen.render(list, nullptr, 0, 520, 0, 0, false);
  require(screen.rowCount() == 0 && lv_obj_get_child_cnt(list) == 2 &&
          lv_label_get_text(lv_obj_get_child(list, 1))[0], "Empty state missing");
  screen.render(list, entries, 128, 520, 0, 0, false);
  lv_obj_clean(list);
  require(!screen.refresh(530, lookup), "External clean left stale widget pointers");
  screen.render(list, entries, 128, 530, 0, 0, false);
  sync(list);
  require(text(row(list, 0), "Updated tail"), "Reopen after external clean failed");
  lv_obj_set_height(list, 90);
  sync(list);
  require(screen.materializedRowCount() <= 7, "Resize did not release offscreen content");
  lv_obj_set_height(list, 220);
  sync(list);
  require(screen.materializedRowCount() > 3, "Resize did not materialize newly visible rows");
  lv_obj_del(list);
  require(!screen.refresh(540, lookup), "Deleted list retained a live refresh target");
  list = makeList(root, 120);
  screen.render(list, entries, 1, 540, 0, 0, true);
  sync(list);
  require(text(row(list, 0), "Updated tail"), "Replacement list failed");
  lv_obj_del(root);
  require(!screen.refresh(550, lookup), "Deleted replacement list retained callback data");
  require(lv_obj_get_child_cnt(lv_layer_top()) == rootsBefore, "Contacts regression leaked roots");
}

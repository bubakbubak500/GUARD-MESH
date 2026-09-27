// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/FocusNavigation.h"
#include "widgets/ObjectRef.h"
#include <stdexcept>
namespace {
namespace nav = ui::focus;
int detachCount = 0, editingChanges = 0;
bool editing = false;
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
lv_obj_t *page() {
  auto *root = lv_obj_create(lv_layer_top());
  lv_obj_set_size(root, 300, 180);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE);
  return root;
}
lv_obj_t *button(lv_obj_t *parent, int y, int thread = -1) {
  auto *object = lv_btn_create(parent);
  lv_obj_set_size(object, 120, 30);
  lv_obj_set_pos(object, 10, y);
  if (thread >= 0) {
    lv_obj_add_flag(object, LV_OBJ_FLAG_USER_3);
    lv_obj_set_user_data(object, reinterpret_cast<void *>(intptr_t(thread + 1)));
  }
  return object;
}
void expectFocus(lv_obj_t *object, const char *reason) {
  check(lv_group_get_focused(nav::targets().group()) == object, reason);
}
} // namespace
void runFocusNavigationRegression() {
  const auto rootCount = lv_obj_get_child_cnt(lv_layer_top());
  nav::Host host{};
  host.beforeDetach = [] { ++detachCount; };
  host.setEditing = [](bool value) {
    editing = value;
    ++editingChanges;
  };
  const nav::Flags flags{LV_OBJ_FLAG_USER_1, LV_OBJ_FLAG_USER_2, LV_OBJ_FLAG_USER_3, LV_OBJ_FLAG_USER_4};
  check(nav::initialize(host, flags), "Focus owner could not initialize");
  lv_group_set_focus_cb(nav::targets().group(), nullptr);
  auto *inbox = page();
  auto *first = button(inbox, 0, 3);
  auto *middle = button(inbox, 40, 9);
  button(inbox, 80, 14);
  auto *gear = button(first, 0);
  lv_obj_add_flag(gear, LV_OBJ_FLAG_USER_2);
  nav::Context context;
  context.root = inbox;
  context.screen = lv_scr_act();
  context.onPage = true;
  context.inbox = true;
  nav::requestFocus(middle);
  check(nav::rebuild(context), "First navigation build was skipped");
  expectFocus(middle, "Explicit focus hint ignored");
  check(nav::targets().count() == 4, "Horizontal gear suppressed primary row");
  const uint32_t initialScans = nav::fullTreeScans();
  const uint32_t initialVisits = nav::fullTreeNodesVisited();
  const uint32_t initialFlagChecks = nav::cachedFlagChecks();
  check(!nav::rebuild(context), "Unchanged tree rebuilt focus");
  check(!nav::rebuild(context) && nav::fullTreeScans() == initialScans &&
            nav::fullTreeNodesVisited() == initialVisits && nav::cachedFlagChecks() > initialFlagChecks,
        "Unchanged navigation repeatedly traversed the widget tree");
  nav::requestFocus(middle);
  check(nav::rebuild(context) && nav::fullTreeScans() == initialScans,
        "Focus hint needlessly revalidated the widget tree");
  expectFocus(middle, "Cached rebuild lost focus hint");
  context.signature = 1;
  check(nav::rebuild(context) && nav::fullTreeScans() == initialScans,
        "Context change was ignored or needlessly scanned the tree");
  context.signature = 0;
  check(nav::rebuild(context) && nav::fullTreeScans() == initialScans,
        "Restored context was ignored or needlessly scanned the tree");
  auto *dynamic = button(inbox, 120);
  check(nav::rebuild(context) && nav::fullTreeScans() == initialScans + 1 && nav::targets().count() == 5,
        "New control did not trigger structural validation");
  const uint32_t beforeFlags = nav::fullTreeScans();
  lv_obj_add_flag(dynamic, LV_OBJ_FLAG_HIDDEN);
  check(nav::rebuild(context) && nav::targets().count() == 4 && nav::fullTreeScans() == beforeFlags,
        "Hidden control remained in navigation or forced a full scan");
  lv_obj_clear_flag(dynamic, LV_OBJ_FLAG_HIDDEN);
  check(nav::rebuild(context) && nav::targets().count() == 5 && nav::fullTreeScans() == beforeFlags,
        "Visible control did not return to navigation");
  lv_obj_clear_flag(dynamic, LV_OBJ_FLAG_CLICKABLE);
  check(nav::rebuild(context) && nav::targets().count() == 4 && nav::fullTreeScans() == beforeFlags,
        "Clickability change was not reflected immediately");
  lv_obj_del(dynamic);
  check(nav::rebuild(context) && nav::targets().count() == 4 && nav::fullTreeScans() == beforeFlags + 1,
        "Deleted control did not trigger structural validation");
  lv_obj_move_to_index(middle, 0);
  check(nav::rebuild(context) && nav::targets().at(0) == middle,
        "Child reordering did not update focus order");
  lv_obj_move_to_index(middle, 1);
  check(nav::rebuild(context) && nav::targets().at(0) == gear && nav::targets().at(1) == first,
        "Restored child order was not reflected in focus order");
  auto *passive = lv_obj_create(inbox);
  lv_obj_add_flag(passive, LV_OBJ_FLAG_USER_1);
  button(passive, 0);
  check(!nav::rebuild(context), "Passive overlay interrupted navigation");
  const uint32_t beforeLayout = nav::fullTreeScans();
  auto *caption = lv_label_create(passive);
  lv_label_set_text(caption, "A");
  check(!nav::rebuild(context), "Skipped caption interrupted navigation");
  const uint32_t afterCaption = nav::fullTreeScans();
  lv_label_set_text(caption, "A longer live caption");
  check(!nav::rebuild(context) && nav::fullTreeScans() == afterCaption &&
            afterCaption == beforeLayout + 1,
        "Text/layout update forced a structural navigation scan");
  lv_obj_clear_flag(passive, LV_OBJ_FLAG_USER_1);
  check(nav::rebuild(context) && nav::targets().count() == 5,
        "Previously skipped subtree did not enter navigation");
  lv_obj_add_flag(passive, LV_OBJ_FLAG_USER_1);
  check(nav::rebuild(context) && nav::targets().count() == 4,
        "Skipped subtree stayed in navigation");
  lv_obj_del(passive);
  auto *modal = page();
  auto *modalButton = button(modal, 0);
  auto overlay = context;
  overlay.root = modal;
  overlay.onPage = false;
  overlay.topLayer = true;
  overlay.inbox = false;
  nav::requestFocus(first);
  nav::rebuild(overlay);
  expectFocus(modalButton, "Underlying hint escaped modal focus trap");
  // Page controls are destroyed while NOT in the navigation group. Retained
  // ObjectRef must retire the pointer; stable thread identity survives reordering.
  lv_obj_clean(inbox);
  button(inbox, 0, 14);
  button(inbox, 40, 3);
  auto *restored = button(inbox, 80, 9);
  nav::rebuild(context);
  expectFocus(restored, "Returning from modal lost thread identity after reorder");
  lv_obj_del(modal);
  auto *chat = page();
  auto *composer = lv_textarea_create(chat);
  lv_obj_set_size(composer, 150, 40);
  auto *send = button(chat, 60);
  auto chatContext = context;
  chatContext.root = chat;
  chatContext.chat = chat;
  chatContext.composer = composer;
  chatContext.onPage = false;
  chatContext.inbox = false;
  nav::requestFocus(restored);
  nav::rebuild(chatContext);
  expectFocus(composer, "Uncollected hint prevented new-chat composer focus");
  check(editing && editingChanges == 1, "New chat did not enter editing mode");
  lv_group_focus_obj(send);
  button(chat, 100);
  nav::rebuild(chatContext);
  expectFocus(send, "New chat content stole focus back to composer");
  nav::requestPersistentFocus(restored);
  nav::rebuild(chatContext);
  check(nav::persistentFocus() == restored, "Uncollected persistent focus was discarded");
  nav::rebuild(context);
  expectFocus(restored, "Persistent focus was not applied when collected");
  check(!nav::persistentFocus(), "Persistent focus was not consumed");
  nav::requestPersistentFocus(send);
  lv_obj_del(send);
  check(!nav::persistentFocus(), "Deleted pending target retained pointer");
  lv_obj_del(chat);
  nav::requestFirstThread();
  nav::rebuild(context);
  expectFocus(lv_obj_get_child(inbox, 0), "Inbox first-thread request failed");
  nav::entered(restored);
  lv_obj_clean(inbox);
  check(!nav::enteredObject(), "Deleted entered object retained reference");
  button(inbox, 0);
  auto *replacement = button(inbox, 80);
  lv_obj_update_layout(inbox);
  nav::rebuild(context);
  expectFocus(replacement, "Same-screen rebuild lost activated element's position");
  check(nav::detach() && nav::targets().count() == 0 && !nav::suppressScroll(),
        "Detach retained focus group or scroll suppression");
  nav::rebuild(context);
  check(nav::targets().count() == 2, "Detached owner did not recollect tree");
  lv_obj_del(inbox);
  check(nav::targets().count() == 0, "Root deletion retained targets");
  nav::initialize({}, flags);
  auto *borrowed = page();
  {
    ui::widgets::ObjectRef ref;
    check(ref.set(borrowed) && ref.get() == borrowed, "Borrowed object watch failed");
  }
  check(lv_obj_is_valid(borrowed), "ObjectRef deleted borrowed widget");
  {
    ui::widgets::ObjectRef ref;
    ref.set(borrowed);
    lv_obj_del(borrowed);
    check(!ref.get(), "ObjectRef missed external DELETE");
  }
  check(lv_obj_get_child_cnt(lv_layer_top()) == rootCount && detachCount > 0,
        "Navigation owner leaked roots");
}

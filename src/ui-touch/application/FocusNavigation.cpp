// SPDX-License-Identifier: GPL-3.0-or-later
#include "FocusNavigation.h"
#include "../widgets/ObjectRef.h"
#include "../platform/UiPlatform.h"
#include <limits.h>
#include <cstring>
namespace ui {
namespace focus {
using widgets::ObjectRef;
static Host host{};
static Flags flags{};
static bool dirty = true, scrollSuppressed = false, firstThreadRequested = false;
static void targetDeleted(lv_obj_t *object) {
  dirty = true;
  if (host.targetDeleted)
    host.targetDeleted(object);
}
static FocusTargets collection(targetDeleted);
static ObjectRef hint, persistent, enteredRef, pageFocus, previousScreen, previousRoot;
static uint32_t previousSignature = 0;
struct WatchedNode {
  lv_obj_t *object;
  lv_obj_flag_t flags;
  bool root;
};
static WatchedNode *watched = nullptr;
static size_t watchedCount = 0, watchedCapacity = 0;
static bool watchComplete = false, structureChanged = true;
static uint32_t scanCount = 0, visitedCount = 0, flagCheckCount = 0, previousContextSignature = 0;
static lv_obj_t *previousComposer = nullptr, *previousDefault = nullptr;
static lv_obj_t *previousExtras[5] = {};
static lv_obj_flag_t previousExtraFlags[5] = {};
static bool previousInbox = false;
static const void *previousChat = nullptr;
static bool previousOnPage = true, previousTop = false, reselectPending = false;
static int pageIndex = -1, pageCount = 0, pageThread = -1, reselectX = 0, reselectY = 0;
uint32_t fullTreeScans() { return scanCount; }
uint32_t fullTreeNodesVisited() { return visitedCount; }
uint32_t cachedFlagChecks() { return flagCheckCount; }
static lv_obj_flag_t watchedFlags(lv_obj_t *object) {
  return object->flags & (LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_CLICKABLE | flags.skip |
                          flags.horizontalOnly | flags.threadRow | flags.passthrough);
}
static void watchEvent(lv_event_t *event) {
  // Layout/label size changes also send CHILD_CHANGED, with the child as param.
  // Reordering sends NULL; creation/deletion bubbles separately to the root.
  if (lv_event_get_code(event) == LV_EVENT_CHILD_CHANGED && lv_event_get_param(event))
    return;
  structureChanged = true;
  if (lv_event_get_code(event) != LV_EVENT_DELETE)
    return;
  auto *object = lv_event_get_target(event);
  for (size_t i = 0; i < watchedCount; ++i)
    if (watched[i].object == object) {
      watched[i].object = nullptr;
      break;
    }
}
static void clearWatch() {
  for (size_t i = 0; i < watchedCount; ++i)
    if (watched[i].object) {
      for (int callback = 0; callback < (watched[i].root ? 4 : 2); ++callback)
        lv_obj_remove_event_cb(watched[i].object, watchEvent);
    }
  watchedCount = 0;
  watchComplete = false;
}
static void remember(lv_obj_t *object, bool root) {
  if (!watchComplete)
    return;
  if (watchedCount == watchedCapacity) {
    const size_t capacity = watchedCapacity ? watchedCapacity * 2 : 64;
    auto *next = static_cast<WatchedNode *>(platform::allocate(capacity * sizeof(WatchedNode), false));
    if (!next) {
      watchComplete = false;
      return;
    }
    if (watchedCount)
      std::memcpy(next, watched, watchedCount * sizeof(WatchedNode));
    platform::release(watched);
    watched = next;
    watchedCapacity = capacity;
  }
  const lv_event_code_t events[] = {
      LV_EVENT_CHILD_CHANGED, LV_EVENT_DELETE, LV_EVENT_CHILD_CREATED, LV_EVENT_CHILD_DELETED};
  const int count = root ? 4 : 2;
  for (int i = 0; i < count; ++i) {
    if (lv_obj_add_event_cb(object, watchEvent, events[i], nullptr))
      continue;
    while (i-- > 0)
      lv_obj_remove_event_cb(object, watchEvent);
    watchComplete = false;
    return;
  }
  watched[watchedCount++] = {object, watchedFlags(object), root};
}
static bool watchedFlagsChanged() {
  if (!watchComplete)
    return true;
  for (size_t i = 0; i < watchedCount; ++i) {
    ++flagCheckCount;
    if (!watched[i].object || watched[i].flags != watchedFlags(watched[i].object))
      return true;
  }
  return false;
}
static void refreshWatchedFlags() {
  for (size_t i = 0; i < watchedCount; ++i)
    if (watched[i].object)
      watched[i].flags = watchedFlags(watched[i].object);
}
const FocusTargets &targets() { return collection; }
void invalidate() { dirty = true; }
bool suppressScroll() { return scrollSuppressed; }
void requestFocus(lv_obj_t *object) {
  hint.set(object);
  invalidate();
}
void requestPersistentFocus(lv_obj_t *object) {
  persistent.set(object);
  invalidate();
}
lv_obj_t *persistentFocus() { return persistent.get(); }
void requestFirstThread() {
  firstThreadRequested = true;
  invalidate();
}
lv_obj_t *first() { return collection.at(0); }
lv_obj_t *last() { return collection.at(collection.count() - 1); }
void entered(lv_obj_t *object) {
  enteredRef.set(object);
  if (object) {
    lv_area_t bounds;
    lv_obj_get_coords(object, &bounds);
    reselectX = (bounds.x1 + bounds.x2) / 2;
    reselectY = (bounds.y1 + bounds.y2) / 2;
    reselectPending = true;
  }
}
lv_obj_t *enteredObject() { return enteredRef.get(); }
lv_obj_t *takeEnteredObject() {
  auto *object = enteredRef.get();
  enteredRef.set(nullptr);
  return object;
}
bool detach() {
  if (!collection.group())
    return false;
  if (host.beforeDetach)
    host.beforeDetach();
  collection.clear();
  invalidate();
  return true;
}
bool initialize(const Host &value, Flags valueFlags) {
  detach();
  clearWatch();
  host = value;
  flags = valueFlags;
  scanCount = visitedCount = flagCheckCount = 0;
  structureChanged = true;
  previousContextSignature = 0;
  previousComposer = previousDefault = nullptr;
  for (int i = 0; i < 5; ++i) {
    previousExtras[i] = nullptr;
    previousExtraFlags[i] = 0;
  }
  previousInbox = false;
  hint.set(nullptr);
  persistent.set(nullptr);
  enteredRef.set(nullptr);
  pageFocus.set(nullptr);
  previousScreen.set(nullptr);
  previousRoot.set(nullptr);
  previousChat = nullptr;
  previousOnPage = true;
  previousTop = false;
  reselectPending = false;
  pageIndex = pageThread = -1;
  pageCount = 0;
  firstThreadRequested = false;
  return collection.initialize();
}
static bool eligible(lv_obj_t *object) {
  return object && !lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN) && !lv_obj_has_flag(object, flags.skip);
}
static bool collect(lv_obj_t *object) {
  if (!eligible(object))
    return false;
  bool childClickable = false;
  const uint32_t count = lv_obj_get_child_cnt(object);
  for (uint32_t i = 0; i < count; ++i) {
    auto *child = lv_obj_get_child(object, i);
    if (child && collect(child) && !lv_obj_has_flag(child, flags.horizontalOnly))
      childClickable = true;
  }
  const bool clickable =
      lv_obj_has_flag(object, LV_OBJ_FLAG_CLICKABLE) && !lv_obj_has_flag(object, flags.passthrough);
  if (clickable && !childClickable) {
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    if (!collection.add(object))
      return childClickable;
  }
  return clickable || childClickable;
}
static uint32_t treeSignature(lv_obj_t *object, int depth) {
  if (!object)
    return 0;
  ++visitedCount;
  remember(object, depth == 0); // Also watch hidden/skip subtrees for later visibility changes.
  const bool included = eligible(object);
  uint32_t hash = included ?
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object)) * 2654435761u + depth + 1u : 0;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i) {
    auto *child = lv_obj_get_child(object, i);
    const uint32_t childSignature = treeSignature(child, depth + 1);
    if (included && eligible(child))
      hash = hash * 2654435761u + childSignature;
  }
  return hash;
}
static uint32_t scanTree(lv_obj_t *root) {
  clearWatch();
  watchComplete = true;
  structureChanged = false;
  ++scanCount;
  const uint32_t signature = treeSignature(root, 0);
  if (!watchComplete)
    clearWatch(); // Allocation/registration failure keeps the safe full-scan path.
  return signature;
}
static bool contains(lv_obj_t *object) {
  if (!object)
    return false;
  for (int i = 0; i < collection.count(); ++i)
    if (collection.at(i) == object)
      return true;
  return false;
}
static bool focus(lv_obj_t *object) {
  if (!contains(object))
    return false;
  lv_group_focus_obj(object);
  return true;
}
static int threadId(lv_obj_t *object) {
  if (!object || !lv_obj_has_flag(object, flags.threadRow))
    return -1;
  const auto encoded = reinterpret_cast<intptr_t>(lv_obj_get_user_data(object));
  return encoded > 0 ? static_cast<int>(encoded - 1) : -1;
}
static void reveal(lv_obj_t *object) {
  auto *target = host.scrollTarget ? host.scrollTarget(object) : nullptr;
  lv_obj_scroll_to_view_recursive(target ? target : object, LV_ANIM_OFF);
}
static lv_obj_t *restorePage() {
  const int count = collection.count();
  if (pageThread >= 0)
    for (int i = 0; i < count; ++i)
      if (threadId(collection.at(i)) == pageThread)
        return collection.at(i);
  if (pageIndex >= 0 && pageIndex < count && count == pageCount)
    return collection.at(pageIndex);
  if (contains(pageFocus.get()))
    return pageFocus.get();
  return pageIndex >= 0 && count > 0 ? collection.at(pageIndex < count ? pageIndex : count - 1) : nullptr;
}
bool rebuild(const Context &context) {
  if (!collection.group())
    return false;
  const bool rootChanged = previousRoot.get() != context.root;
  const bool flagsChanged = watchedFlagsChanged();
  const bool needScan = rootChanged || structureChanged || !watchComplete;
  const uint32_t signature = needScan ? scanTree(context.root) : previousSignature;
  bool extrasChanged = false;
  for (int i = 0; i < 5; ++i) {
    auto *object = context.extraTargets[i];
    const lv_obj_flag_t extraFlags = object ? object->flags & (LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_CLICKABLE) : 0;
    if (object != previousExtras[i] || extraFlags != previousExtraFlags[i])
      extrasChanged = true;
  }
  const bool contextChanged = previousScreen.get() != context.screen || rootChanged ||
      previousContextSignature != context.signature || previousChat != context.chat ||
      previousComposer != context.composer || previousDefault != context.defaultTarget ||
      previousOnPage != context.onPage || previousTop != context.topLayer || previousInbox != context.inbox;
  if (!dirty && !flagsChanged && !extrasChanged && !contextChanged && signature == previousSignature)
    return false;
  dirty = false;
  previousSignature = signature;
  previousContextSignature = context.signature;
  previousComposer = context.composer;
  previousDefault = context.defaultTarget;
  previousInbox = context.inbox;
  for (int i = 0; i < 5; ++i) {
    previousExtras[i] = context.extraTargets[i];
    previousExtraFlags[i] = context.extraTargets[i] ?
        context.extraTargets[i]->flags & (LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_CLICKABLE) : 0;
  }
  if (!needScan)
    refreshWatchedFlags();
  previousScreen.set(context.screen);
  previousRoot.set(context.root);
  ObjectRef keep;
  keep.set(lv_group_get_focused(collection.group()));
  if (!context.onPage && previousOnPage) {
    pageFocus.set(keep.get());
    pageIndex = -1;
    pageThread = threadId(keep.get());
    pageCount = collection.count();
    for (int i = 0; i < pageCount; ++i)
      if (collection.at(i) == keep.get()) {
        pageIndex = i;
        break;
      }
  }
  scrollSuppressed = true;
  if (host.beforeDetach)
    host.beforeDetach();
  collection.clear();
  if (context.root)
    collect(context.root);
  if (!context.topLayer)
    for (auto *object : context.extraTargets)
      if (object && !lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN) &&
          lv_obj_has_flag(object, LV_OBJ_FLAG_CLICKABLE)) {
        lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        collection.add(object);
      }
  if (host.afterCollect)
    host.afterCollect();
  bool focused = false;
  if (contains(persistent.get())) {
    auto *object = persistent.get();
    const bool editing = lv_obj_check_type(object, &lv_textarea_class);
    if (!editing && host.showFocus)
      host.showFocus();
    focused = focus(object);
    if (host.setEditing)
      host.setEditing(editing);
    persistent.set(nullptr);
  }
  ObjectRef requested;
  requested.set(contains(hint.get()) ? hint.get() : nullptr);
  hint.set(nullptr);
  if (focused) {
  } else if (firstThreadRequested && context.onPage && context.inbox) {
    firstThreadRequested = false;
    for (int i = 0; i < collection.count(); ++i) {
      auto *object = collection.at(i);
      if (threadId(object) < 0)
        continue;
      focused = focus(object);
      if (focused)
        reveal(object);
      break;
    }
  } else if (requested.get())
    focused = focus(requested.get());
  else if (context.chat && context.chat != previousChat && context.composer) {
    focused = focus(context.composer);
    if (focused) {
      if (host.setEditing)
        host.setEditing(true);
      if (host.syncCursor)
        host.syncCursor();
    }
  } else if (context.onPage && !previousOnPage && (pageFocus.get() || pageIndex >= 0 || pageThread >= 0)) {
    auto *object = restorePage();
    focused = focus(object);
    if (focused)
      reveal(object);
  } else if (keep.get())
    focused = focus(keep.get());
  else if (reselectPending && context.topLayer == previousTop && context.chat == previousChat &&
           context.onPage == previousOnPage) {
    long best = LONG_MAX;
    lv_obj_t *nearest = nullptr;
    for (int i = 0; i < collection.count(); ++i) {
      auto *object = collection.at(i);
      if (!object)
        continue;
      lv_area_t bounds;
      lv_obj_get_coords(object, &bounds);
      const long dx = (bounds.x1 + bounds.x2) / 2 - reselectX, dy = (bounds.y1 + bounds.y2) / 2 - reselectY;
      const long distance = dx * dx + dy * dy;
      if (distance < best) {
        best = distance;
        nearest = object;
      }
    }
    if (nearest && best <= 40 * 40) {
      if (host.showFocus)
        host.showFocus();
      focused = focus(nearest);
    }
  }
  if (!focused && context.onPage && context.defaultTarget)
    focused = focus(context.defaultTarget);
  if (!focused) {
    bool horizontal = false;
    for (int i = 0; i < collection.count(); ++i) {
      auto *object = collection.at(i);
      if (object && lv_obj_has_flag(object, flags.horizontalOnly)) {
        horizontal = true;
        break;
      }
    }
    if (horizontal)
      for (int i = 0; i < collection.count(); ++i) {
        auto *object = collection.at(i);
        if (object && !lv_obj_has_flag(object, flags.horizontalOnly)) {
          focus(object);
          break;
        }
      }
  }
  scrollSuppressed = false;
  reselectPending = false;
  previousChat = context.chat;
  previousOnPage = context.onPage;
  previousTop = context.topLayer;
  return true;
}
} // namespace focus
} // namespace ui

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FocusTargets.h"
#include <stdint.h>
namespace ui {
namespace focus {
struct Flags {
  lv_obj_flag_t skip, horizontalOnly, threadRow, passthrough;
};
struct Context {
  lv_obj_t *root = nullptr;
  lv_obj_t *screen = nullptr;
  const void *chat = nullptr;
  lv_obj_t *composer = nullptr;
  lv_obj_t *defaultTarget = nullptr;
  lv_obj_t *extraTargets[5] = {};
  uint32_t signature = 0;
  bool onPage = false, topLayer = false, inbox = false;
};
struct Host {
  void (*targetDeleted)(lv_obj_t *);
  void (*beforeDetach)();
  void (*afterCollect)();
  lv_obj_t *(*scrollTarget)(lv_obj_t *);
  void (*showFocus)();
  void (*setEditing)(bool);
  void (*syncCursor)();
};
// One UI navigation owner: target collection, rebuild signatures, focus hints
// and restoration. Screen selection supplies a borrowed Context each UI tick.
bool initialize(const Host &, Flags);
const FocusTargets &targets();
bool rebuild(const Context &);
// Deterministic work counters since initialize (diagnostics/tests). Cached flag
// checks remain linear in watched nodes; they avoid recursive child traversal.
uint32_t fullTreeScans();
uint32_t fullTreeNodesVisited();
uint32_t cachedFlagChecks();
bool detach();
void invalidate();
bool suppressScroll();
void requestFocus(lv_obj_t *);
void requestPersistentFocus(lv_obj_t *);
lv_obj_t *persistentFocus();
void requestFirstThread();
void entered(lv_obj_t *);
lv_obj_t *enteredObject();
lv_obj_t *takeEnteredObject();
lv_obj_t *first();
lv_obj_t *last();
} // namespace focus
} // namespace ui

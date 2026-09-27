// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.h"
#include "screens/ContactActionSheet.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
using Sheet = ui::screens::ContactActionSheet;
using Action = Sheet::Action;

const char *currentScenario = "contact-actions";

void require(bool condition, const char *message) {
  if (!condition) {
    char detail[320];
    std::snprintf(detail, sizeof(detail), "Contact action scenario '%s': %s", currentScenario, message);
    throw std::runtime_error(detail);
  }
}

struct Fixture;
Fixture *activeFixture = nullptr;

struct Fixture {
  Sheet *sheet = nullptr;
  Sheet::Snapshot replacement;
  lv_coord_t top = 24;
  bool reopenOnClose = false;
  bool replacementOpened = false;
  unsigned closeCalls = 0;
  unsigned dispatchCount = 0;
  Action actions[32] = {};
  uint8_t keys[32][32] = {};

  static lv_coord_t contentTop() { return activeFixture ? activeFixture->top : 0; }

  static void closeRoot(void *context, lv_obj_t **root) {
    auto &fixture = *static_cast<Fixture *>(context);
    if (!root || !*root)
      return;
    lv_obj_t *old = *root;
    *root = nullptr;
    ++fixture.closeCalls;
    lv_obj_del_async(old);
    if (fixture.reopenOnClose && fixture.sheet) {
      // The replacement is opened synchronously while the retired action is
      // still on the event stack. A correct owner must suppress that action.
      fixture.reopenOnClose = false;
      fixture.replacementOpened = true;
      fixture.sheet->open(fixture.replacement);
    }
  }

  static void dispatch(void *context, Action action, const uint8_t publicKey[32]) {
    auto &fixture = *static_cast<Fixture *>(context);
    if (fixture.dispatchCount >= sizeof(fixture.actions) / sizeof(fixture.actions[0]))
      return;
    fixture.actions[fixture.dispatchCount] = action;
    std::memcpy(fixture.keys[fixture.dispatchCount], publicKey, 32);
    ++fixture.dispatchCount;
  }

  Sheet::Host host() {
    Sheet::Host value;
    value.context = this;
    value.contentTop = contentTop;
    value.closeRoot = closeRoot;
    value.dispatch = dispatch;
    return value;
  }
};

const Action chatAll[] = {
    Action::Message,       Action::Telemetry,    Action::ShowOnMap,
    Action::RangeTest,     Action::Sightline,    Action::Favorite,
    Action::ShareLocation, Action::ResetPath,    Action::ShareContact,
    Action::Block,         Action::Delete,
};

const Action repeaterAll[] = {
    Action::Ping,           Action::Telemetry,    Action::ShowOnMap,
    Action::Trace,          Action::Admin,        Action::RangeTest,
    Action::Sightline,      Action::Favorite,     Action::ShareLocation,
    Action::ResetPath,      Action::ShareContact, Action::Block,
    Action::Delete,
};

const Action roomAll[] = {
    Action::Join,           Action::Message,      Action::Telemetry,
    Action::ShowOnMap,      Action::RangeTest,    Action::Sightline,
    Action::Favorite,       Action::ShareLocation, Action::ResetPath,
    Action::ShareContact,   Action::Block,        Action::Delete,
};

const Action mapActions[] = {
    Action::Message, Action::Telemetry, Action::RangeTest, Action::Sightline,
    Action::Favorite, Action::ResetPath, Action::Delete,
};

const Action noGpsActions[] = {
    Action::Message,       Action::Telemetry,     Action::RangeTest,
    Action::Favorite,      Action::ShareLocation, Action::ResetPath,
    Action::ShareContact,  Action::Block,         Action::Delete,
};

const Action noShareActions[] = {
    Action::Message,   Action::Telemetry,    Action::ShowOnMap,
    Action::RangeTest, Action::Sightline,    Action::Favorite,
    Action::ResetPath, Action::ShareContact, Action::Block,
    Action::Delete,
};

lv_obj_t *lastRoot() {
  auto *layer = lv_layer_top();
  const uint32_t count = lv_obj_get_child_cnt(layer);
  return count ? lv_obj_get_child(layer, count - 1) : nullptr;
}

void collectButtons(lv_obj_t *object, lv_obj_t **out, unsigned &count) {
  if (!object || count >= 32)
    return;
  // The close badge is an lv_obj_t, deliberately not an lv_btn, so this
  // enumerates only action controls in their creation/display order.
  if (lv_obj_check_type(object, &lv_btn_class))
    out[count++] = object;
  const uint32_t children = lv_obj_get_child_cnt(object);
  for (uint32_t i = 0; i < children; ++i)
    collectButtons(lv_obj_get_child(object, i), out, count);
}

void actionButtons(lv_obj_t *root, lv_obj_t **out, unsigned &count) {
  require(root != nullptr, "Contact action root missing");
  lv_obj_update_layout(root);
  count = 0;
  collectButtons(root, out, count);
  require(count <= 15, "Contact action sheet exposed more than 15 action buttons");
}

bool labelContains(lv_obj_t *object, const char *needle) {
  if (!object)
    return false;
  if (lv_obj_check_type(object, &lv_label_class)) {
    const lv_label_long_mode_t mode = lv_label_get_long_mode(object);
    if (mode == LV_LABEL_LONG_DOT)
      lv_label_set_long_mode(object, LV_LABEL_LONG_CLIP);
    const char *text = lv_label_get_text(object);
    const bool found = text && needle && std::strstr(text, needle);
    if (mode == LV_LABEL_LONG_DOT)
      lv_label_set_long_mode(object, mode);
    if (found)
      return true;
  }
  const uint32_t children = lv_obj_get_child_cnt(object);
  for (uint32_t i = 0; i < children; ++i)
    if (labelContains(lv_obj_get_child(object, i), needle))
      return true;
  return false;
}

bool buttonContains(lv_obj_t *button, const char *needle) {
  return labelContains(button, needle);
}

lv_obj_t *findCloseBadge(lv_obj_t *object) {
  if (!object)
    return nullptr;
  if (!lv_obj_check_type(object, &lv_btn_class) &&
      lv_obj_has_flag(object, LV_OBJ_FLAG_CLICKABLE) &&
      lv_obj_has_flag(object, LV_OBJ_FLAG_FLOATING) && labelContains(object, LV_SYMBOL_CLOSE))
    return object;
  const uint32_t children = lv_obj_get_child_cnt(object);
  for (uint32_t i = 0; i < children; ++i)
    if (auto *found = findCloseBadge(lv_obj_get_child(object, i)))
      return found;
  return nullptr;
}

Sheet::Snapshot makeSnapshot(uint8_t keyBase) {
  Sheet::Snapshot snapshot;
  for (unsigned i = 0; i < sizeof(snapshot.publicKey); ++i)
    snapshot.publicKey[i] = static_cast<uint8_t>(keyBase + i);
  std::snprintf(snapshot.displayName, sizeof(snapshot.displayName), "Contact %u",
                static_cast<unsigned>(keyBase));
  snapshot.hasMap = true;
  snapshot.hasSightline = true;
  snapshot.canShareLocation = true;
  return snapshot;
}

void click(lv_obj_t *object) {
  require(object != nullptr, "Contact action click target missing");
  lv_event_send(object, LV_EVENT_CLICKED, nullptr);
}

void expectKey(const Fixture &fixture, const uint8_t expected[32]) {
  require(fixture.dispatchCount == 1, "Contact action dispatch count changed");
  require(!std::memcmp(fixture.keys[0], expected, 32),
          "Contact action dispatch used a mutated public key");
}

template <std::size_t N>
void verifyScenario(Fixture &fixture, Sheet &sheet, const Sheet::Snapshot &snapshot,
                    const Action (&expected)[N], void (*pump)(unsigned), const char *name) {
  for (std::size_t selected = 0; selected < N; ++selected) {
    currentScenario = name;
    fixture.dispatchCount = 0;
    fixture.replacementOpened = false;
    sheet.open(snapshot);
    require(sheet.isOpen(), "Contact action sheet did not open");

    lv_obj_t *buttons[32] = {};
    unsigned count = 0;
    actionButtons(lastRoot(), buttons, count);
    require(count == N, "Contact action count/order matrix changed");
    click(buttons[selected]);
    expectKey(fixture, snapshot.publicKey);
    require(fixture.actions[0] == expected[selected], "Contact action enum mapping changed");
    pump(40);
    require(!sheet.isOpen(), "Contact action remained open after dispatch");
  }
}

template <std::size_t N>
void verifyLabelVariant(Fixture &fixture, Sheet &sheet, const Sheet::Snapshot &snapshot,
                        const Action (&expected)[N], const char *token, unsigned buttonIndex,
                        void (*pump)(unsigned), const char *name) {
  currentScenario = name;
  fixture.dispatchCount = 0;
  sheet.open(snapshot);
  lv_obj_t *buttons[32] = {};
  unsigned count = 0;
  actionButtons(lastRoot(), buttons, count);
  require(count == N, "Label-only state changed contact action count/order");
  require(buttonIndex < count && buttonContains(buttons[buttonIndex], token),
          "Contact action state label did not change as expected");
  sheet.close();
  pump(40);
  require(!sheet.isOpen(), "Contact action label fixture remained open");
  (void)expected;
}

void countDelete(lv_event_t *event) {
  auto *count = static_cast<int *>(lv_event_get_user_data(event));
  if (count)
    ++*count;
}

void copyAndStaleRegression(Fixture &fixture, Sheet &sheet, void (*pump)(unsigned)) {
  currentScenario = "copy-and-stale";
  Sheet::Snapshot original = makeSnapshot(0x20);
  Sheet::Snapshot supplied = original;
  sheet.open(supplied);
  lv_obj_t *oldRoot = lastRoot();
  lv_obj_t *oldButtons[32] = {};
  unsigned oldCount = 0;
  actionButtons(oldRoot, oldButtons, oldCount);
  require(oldCount == sizeof(chatAll) / sizeof(chatAll[0]), "Initial contact action matrix missing rows");
  for (unsigned i = 0; i < sizeof(supplied.publicKey); ++i)
    supplied.publicKey[i] ^= 0xFF;
  click(oldButtons[0]);
  expectKey(fixture, original.publicKey);
  require(fixture.actions[0] == Action::Message, "Initial contact action enum was not preserved");
  pump(40);

  fixture.dispatchCount = 0;
  sheet.open(original);
  oldRoot = lastRoot();
  actionButtons(oldRoot, oldButtons, oldCount);
  lv_obj_t *oldButton = oldButtons[0];
  sheet.close();
  click(oldButton);
  require(fixture.dispatchCount == 0, "Closed contact action accepted an old callback");

  Sheet::Snapshot replacement = makeSnapshot(0x70);
  sheet.open(replacement);
  lv_obj_t *newRoot = lastRoot();
  lv_obj_t *newButtons[32] = {};
  unsigned newCount = 0;
  actionButtons(newRoot, newButtons, newCount);
  click(oldButton);
  require(fixture.dispatchCount == 0, "Replaced contact action accepted an old callback");
  click(newButtons[0]);
  expectKey(fixture, replacement.publicKey);
  require(fixture.actions[0] == Action::Message, "Replacement contact action did not dispatch");
  pump(40);
}

void externalDeleteRegression(Fixture &fixture, Sheet &sheet, void (*pump)(unsigned)) {
  currentScenario = "external-delete";
  Sheet::Snapshot snapshot = makeSnapshot(0x32);
  sheet.open(snapshot);
  lv_obj_t *root = lastRoot();
  int observers = 0;
  lv_obj_add_event_cb(root, countDelete, LV_EVENT_DELETE, &observers);
  lv_obj_del(root);
  require(observers == 1, "External root DELETE skipped a later observer");
  require(!sheet.isOpen(), "External root DELETE left the contact action live");
  pump(40);
  require(observers == 1, "External root DELETE observer fired more than once");
}

void ownerLifetimeRegression(Fixture &fixture, void (*pump)(unsigned)) {
  currentScenario = "owner-lifetime";
  fixture.dispatchCount = 0;
  fixture.reopenOnClose = true;
  fixture.replacementOpened = false;
  const uint32_t rootsBefore = lv_obj_get_child_cnt(lv_layer_top());
  lv_obj_t *retainedButton = nullptr;
  {
    Sheet temporary(fixture.host());
    fixture.sheet = &temporary;
    activeFixture = &fixture;
    temporary.open(makeSnapshot(0x44));
    lv_obj_t *buttons[32] = {};
    unsigned count = 0;
    actionButtons(lastRoot(), buttons, count);
    retainedButton = buttons[0];
    // Keep the owner pointer through its destructor so closeRoot attempts the
    // reentrant open; ContactActionSheet must reject it while destroying.
  }
  fixture.sheet = nullptr;
  require(fixture.replacementOpened && lv_obj_get_child_cnt(lv_layer_top()) == rootsBefore + 1,
          "Destroyed contact action owner reopened a live replacement tree");
  click(retainedButton);
  require(fixture.dispatchCount == 0,
          "Destroyed contact action tree retained a reentrant callback");
  pump(40);
  require(lv_obj_get_child_cnt(lv_layer_top()) == rootsBefore,
          "Destroyed contact action tree leaked its deferred root");
  fixture.reopenOnClose = false;
}

void reentrantCloseRegression(Fixture &fixture, Sheet &sheet, void (*pump)(unsigned)) {
  currentScenario = "reentrant-close";
  fixture.dispatchCount = 0;
  fixture.replacementOpened = false;
  fixture.reopenOnClose = false;
  Sheet::Snapshot initial = makeSnapshot(0x50);
  fixture.replacement = makeSnapshot(0x90);
  sheet.open(initial);
  lv_obj_t *oldRoot = lastRoot();
  lv_obj_t *oldButtons[32] = {};
  unsigned oldCount = 0;
  actionButtons(oldRoot, oldButtons, oldCount);
  fixture.reopenOnClose = true;
  click(oldButtons[0]);
  require(fixture.dispatchCount == 0, "Retired contact action dispatched after replacement opened");
  require(fixture.replacementOpened && sheet.isOpen(), "Contact replacement did not remain live");

  lv_obj_t *newRoot = lastRoot();
  require(newRoot != oldRoot, "Contact replacement reused the retired root");
  lv_obj_t *newButtons[32] = {};
  unsigned newCount = 0;
  actionButtons(newRoot, newButtons, newCount);
  click(newButtons[0]);
  expectKey(fixture, fixture.replacement.publicKey);
  require(fixture.actions[0] == Action::Message, "Live contact replacement did not dispatch");
  pump(40);
}

void topHeightRegression(Fixture &fixture, Sheet &sheet, void (*pump)(unsigned)) {
  currentScenario = "content-top";
  fixture.top = 19;
  sheet.open(makeSnapshot(0x60));
  lv_obj_t *first = lastRoot();
  lv_obj_update_layout(first);
  require(lv_obj_get_y(first) == fixture.top, "Contact root ignored the first content top height");
  sheet.close();
  pump(40);

  fixture.top = 47;
  sheet.open(makeSnapshot(0x61));
  lv_obj_t *second = lastRoot();
  lv_obj_update_layout(second);
  require(lv_obj_get_y(second) == fixture.top, "Contact root ignored the changed content top height");
  sheet.close();
  pump(40);
}
} // namespace

void runContactActionsRegression(void (*pump)(unsigned)) {
  const uint32_t rootsBefore = lv_obj_get_child_cnt(lv_layer_top());
  const uint8_t previousLanguage = i18nGetLang();
  i18nSetLang(LANG_EN);

  Fixture fixture;
  activeFixture = &fixture;
  Sheet sheet(fixture.host());
  fixture.sheet = &sheet;

  topHeightRegression(fixture, sheet, pump);
  copyAndStaleRegression(fixture, sheet, pump);
  externalDeleteRegression(fixture, sheet, pump);
  ownerLifetimeRegression(fixture, pump);
  fixture.sheet = &sheet;
  reentrantCloseRegression(fixture, sheet, pump);

  Sheet::Snapshot all = makeSnapshot(0x10);
  verifyScenario(fixture, sheet, all, chatAll, pump, "chat-all");

  Sheet::Snapshot repeater = all;
  repeater.isRepeater = true;
  verifyScenario(fixture, sheet, repeater, repeaterAll, pump, "repeater-all");

  Sheet::Snapshot room = all;
  room.isRoom = true;
  verifyScenario(fixture, sheet, room, roomAll, pump, "room-all");

  Sheet::Snapshot fromMap = all;
  fromMap.fromMap = true;
  fromMap.hasMap = false;
  verifyScenario(fixture, sheet, fromMap, mapActions, pump, "from-map");

  Sheet::Snapshot absentGps = all;
  absentGps.hasMap = false;
  absentGps.hasSightline = false;
  verifyScenario(fixture, sheet, absentGps, noGpsActions, pump, "absent-gps");

  Sheet::Snapshot noShare = all;
  noShare.canShareLocation = false;
  verifyScenario(fixture, sheet, noShare, noShareActions, pump, "share-disabled");

  Sheet::Snapshot favorite = all;
  favorite.favorite = true;
  verifyLabelVariant(fixture, sheet, favorite, chatAll, "Unfav", 5, pump, "favorite-label");
  verifyScenario(fixture, sheet, favorite, chatAll, pump, "favorite-label-dispatch");

  Sheet::Snapshot locationShared = all;
  locationShared.locationShared = true;
  verifyLabelVariant(fixture, sheet, locationShared, chatAll, "Stop sharing loc", 6, pump,
                     "location-shared-label");
  verifyScenario(fixture, sheet, locationShared, chatAll, pump, "location-shared-label-dispatch");

  Sheet::Snapshot blocked = all;
  blocked.blocked = true;
  verifyLabelVariant(fixture, sheet, blocked, chatAll, "Unblock", 9, pump, "blocked-label");
  verifyScenario(fixture, sheet, blocked, chatAll, pump, "blocked-label-dispatch");

  currentScenario = "close-and-backdrop";
  fixture.dispatchCount = 0;
  sheet.open(all);
  sheet.close();
  require(fixture.dispatchCount == 0, "Normal contact close dispatched an action");
  pump(40);

  sheet.open(all);
  click(findCloseBadge(lastRoot()));
  require(fixture.dispatchCount == 0, "Contact close badge dispatched an action");
  pump(40);

  sheet.open(all);
  click(lastRoot());
  require(fixture.dispatchCount == 0, "Contact backdrop close dispatched an action");
  pump(40);
  require(!sheet.isOpen(), "Contact backdrop close left the sheet open");

  fixture.sheet = nullptr;
  sheet.close();
  pump(40);
  activeFixture = nullptr;
  require(lv_obj_get_child_cnt(lv_layer_top()) == rootsBefore,
          "Contact action regression leaked roots or pending async deletes");
  i18nSetLang(previousLanguage);
}

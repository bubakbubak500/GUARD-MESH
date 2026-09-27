// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/BlockedUsersScreen.h"
#include "i18n.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using Screen = ui::screens::BlockedUsersScreen;
const char *scenario = "setup";
void check(bool okay, const char *message) {
  if (okay) return;
  char detail[320];
  std::snprintf(detail, sizeof detail, "Blocked users '%s': %s", scenario, message);
  throw std::runtime_error(detail);
}
std::string labelText(lv_obj_t *label) {
  const auto mode = lv_label_get_long_mode(label);
  if (mode == LV_LABEL_LONG_DOT) lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  const char *text = lv_label_get_text(label);
  const std::string result = text ? text : "";
  if (mode == LV_LABEL_LONG_DOT) lv_label_set_long_mode(label, mode);
  return result;
}
lv_obj_t *findLabel(lv_obj_t *root, const char *text) {
  if (!root) return nullptr;
  if (lv_obj_check_type(root, &lv_label_class) && labelText(root) == text) return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = findLabel(lv_obj_get_child(root, i), text)) return found;
  return nullptr;
}
lv_obj_t *unblockFor(lv_obj_t *root, const char *display) {
  auto *label = findLabel(root, display);
  check(label != nullptr, "target display row missing");
  auto *row = lv_obj_get_parent(label);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(row); ++i) {
    auto *child = lv_obj_get_child(row, i);
    if (lv_obj_check_type(child, &lv_btn_class) && findLabel(child, TR("Unblock")))
      return child;
  }
  throw std::runtime_error("Blocked users: Unblock button missing from target row");
}
unsigned countUnblock(lv_obj_t *root) {
  if (!root) return 0;
  unsigned count = lv_obj_check_type(root, &lv_btn_class) &&
                   findLabel(root, TR("Unblock")) ? 1 : 0;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    count += countUnblock(lv_obj_get_child(root, i));
  return count;
}
void click(lv_obj_t *button) {
  check(button != nullptr, "click target missing");
  lv_event_send(button, LV_EVENT_CLICKED, nullptr);
}
void observedDelete(lv_event_t *event) {
  ++*static_cast<unsigned *>(lv_event_get_user_data(event));
}
void key(Screen::KeyEntry &entry, uint8_t tail, const char *display) {
  entry.key[0] = 0x01; entry.key[1] = 0x02; entry.key[2] = 0x03;
  entry.key[3] = 0x04; entry.key[4] = 0x05; entry.key[5] = tail;
  if (display) std::snprintf(entry.display, sizeof entry.display, "%s", display);
}
bool sameKey(const uint8_t *value, uint8_t tail) {
  const uint8_t expected[6] = {1, 2, 3, 4, 5, tail};
  return std::memcmp(value, expected, sizeof expected) == 0;
}

struct Fixture {
  Screen *screen = nullptr;
  Screen::Snapshot source{};
  int top = 44;
  unsigned reads = 0, begins = 0, ends = 0, fronts = 0, dirties = 0, closes = 0;
  unsigned keyCalls = 0, nameCalls = 0;
  bool readOkay = true, deferClose = true;
  bool replaceOnRead = false, replaceOnBegin = false, replaceOnFront = false;
  bool replaceOnClose = false, replaceOnEnd = false, replaceOnUnblock = false;
  uint8_t lastKey[6]{};
  char lastName[32]{};
  std::string title;

  static Fixture &self(void *context) { return *static_cast<Fixture *>(context); }
  void replace() {
    if (!screen) return;
    screen->close();
    screen->show();
  }
  static bool read(void *context, Screen::Snapshot &out) {
    auto &f = self(context); ++f.reads;
    out = f.source;
    if (f.replaceOnRead) { f.replaceOnRead = false; f.replace(); }
    return f.readOkay;
  }
  static void unblockKey(void *context, const uint8_t value[Screen::KeyBytes]) {
    auto &f = self(context); ++f.keyCalls;
    std::memcpy(f.lastKey, value, sizeof f.lastKey);
    if (f.replaceOnUnblock) { f.replaceOnUnblock = false; f.replace(); }
  }
  static void unblockName(void *context, const char *name) {
    auto &f = self(context); ++f.nameCalls;
    std::snprintf(f.lastName, sizeof f.lastName, "%s", name ? name : "");
    if (f.replaceOnUnblock) { f.replaceOnUnblock = false; f.replace(); }
  }
  static int begin(void *context, const char *pageTitle) {
    auto &f = self(context); ++f.begins;
    f.title = pageTitle ? pageTitle : "";
    if (f.replaceOnBegin) { f.replaceOnBegin = false; f.replace(); }
    return f.top;
  }
  static void end(void *context) {
    auto &f = self(context); ++f.ends;
    if (f.replaceOnEnd) { f.replaceOnEnd = false; f.screen->show(); }
  }
  static void front(void *context) {
    auto &f = self(context); ++f.fronts;
    if (f.replaceOnFront) { f.replaceOnFront = false; f.replace(); }
  }
  static void nav(void *context) { ++self(context).dirties; }
  static void closeRoot(void *context, lv_obj_t **root) {
    auto &f = self(context);
    if (!root || !*root) return;
    auto *old = *root;
    *root = nullptr;
    ++f.closes;
    if (f.deferClose) lv_obj_del_async(old);
    else lv_obj_del(old);
    if (f.replaceOnClose) { f.replaceOnClose = false; f.screen->show(); }
  }
  Screen::Host host() {
    Screen::Host result;
    result.context = this;
    result.readSnapshot = read;
    result.unblockKey = unblockKey;
    result.unblockName = unblockName;
    result.beginPage = begin;
    result.endPageIfOwned = end;
    result.bringStatusBarFront = front;
    result.navDirty = nav;
    result.closeRoot = closeRoot;
    return result;
  }
};

void noTrees(unsigned baseline, void (*pump)(unsigned)) {
  pump(2);
  check(lv_obj_get_child_cnt(lv_layer_top()) == baseline,
        "retired page or deferred rows leaked an LVGL tree");
}
} // namespace

void runBlockedUsersRegression(void (*pump)(unsigned)) {
  const unsigned baseline = lv_obj_get_child_cnt(lv_layer_top());
  const char *emptyText = TR("No blocked users.\n\nLong-press a message and tap\nBlock to add one.");

  scenario = "copied key and name targets after source reorder";
  {
    Fixture f;
    key(f.source.keys[0], 0x06, "Alice");
    key(f.source.keys[1], 0x07, "Bob");
    f.source.keyCount = 2;
    std::snprintf(f.source.names[0].name, sizeof f.source.names[0].name, "Channel bot");
    f.source.nameCount = 1;
    Screen screen(f.host()); f.screen = &screen;
    screen.show();
    check(screen.isOpen() && f.title == "Blocked users" && f.begins == 1 && f.fronts == 1,
          "show omitted page chrome or status-bar foreground");
    lv_obj_update_layout(screen.root());
    check(lv_obj_get_y(screen.root()) == 44 &&
          lv_obj_get_height(screen.root()) == lv_disp_get_ver_res(nullptr) - 44,
          "page did not use Host-measured 44px tall bar");
    check(lv_obj_get_child_cnt(screen.root()) == 1 &&
          lv_obj_get_height(lv_obj_get_child(screen.root(), 0)) ==
            lv_disp_get_ver_res(nullptr) - 44 - 16,
          "list height ignored Host-measured 44px tall bar");
    auto *oldAlice = unblockFor(screen.root(), "Alice");
    std::swap(f.source.keys[0], f.source.keys[1]);
    click(oldAlice);
    check(f.keyCalls == 1 && sameKey(f.lastKey, 0x06),
          "reordered source list redirected old row to a different key");
    // Rebuild retires the old root; its row context was freed but its deferred
    // button can still receive events until LVGL processes the async deletion.
    click(oldAlice);
    check(f.keyCalls == 1, "retired deferred row retained a callback to freed context");
    click(unblockFor(screen.root(), "Bob"));
    check(f.keyCalls == 2 && sameKey(f.lastKey, 0x07),
          "current rebuilt row did not own Bob's six-byte key");
    click(unblockFor(screen.root(), "Channel bot"));
    check(f.nameCalls == 1 && std::strcmp(f.lastName, "Channel bot") == 0,
          "name-only row used an index or a stale source name");
    screen.close(); noTrees(baseline, pump);
  }

  scenario = "empty, unnamed key, exact boundaries and bounded counts";
  {
    Fixture f;
    Screen screen(f.host()); f.screen = &screen;
    screen.show();
    check(findLabel(screen.root(), emptyText) && countUnblock(screen.root()) == 0,
          "empty snapshot did not render the translated empty state");
    screen.close(); noTrees(baseline, pump);

    key(f.source.keys[0], 0xAF, "");
    f.source.keyCount = 1;
    f.source.nameCount = 1; // empty name slot stays invisible
    f.top = 22;
    screen.show();
    lv_obj_update_layout(screen.root());
    check(lv_obj_get_y(screen.root()) == 22 &&
          lv_obj_get_height(screen.root()) == lv_disp_get_ver_res(nullptr) - 22,
          "page did not use Host-measured 22px bar");
    check(lv_obj_get_child_cnt(screen.root()) == 1 &&
          lv_obj_get_height(lv_obj_get_child(screen.root(), 0)) ==
            lv_disp_get_ver_res(nullptr) - 22 - 16,
          "list height ignored Host-measured 22px bar");
    check(findLabel(screen.root(), "0102030405AF") && countUnblock(screen.root()) == 1,
          "unnamed key did not use uppercase 12-digit hex or empty name leaked a row");
    click(unblockFor(screen.root(), "0102030405AF"));
    check(f.keyCalls == 1 && sameKey(f.lastKey, 0xAF),
          "hex fallback row lost an exact six-byte key");
    screen.close(); noTrees(baseline, pump);

    f.source = Screen::Snapshot{};
    const char *full = "1234567890123456789012345678901"; // 31 bytes + NUL
    std::memcpy(f.source.names[0].name, full, 31);
    f.source.names[0].name[31] = 0;
    f.source.nameCount = 1;
    screen.show();
    click(unblockFor(screen.root(), full));
    check(f.nameCalls == 1 && std::strcmp(f.lastName, full) == 0,
          "full name[32] was shortened before dispatch");
    screen.close(); noTrees(baseline, pump);

    f.source = Screen::Snapshot{};
    for (unsigned i = 0; i < Screen::KeyCapacity; ++i)
      key(f.source.keys[i], static_cast<uint8_t>(i), "");
    for (unsigned i = 0; i < Screen::NameCapacity; ++i)
      std::snprintf(f.source.names[i].name, sizeof f.source.names[i].name, "Sender %02u", i);
    f.source.keyCount = 999;
    f.source.nameCount = 999;
    screen.show();
    check(countUnblock(screen.root()) == Screen::KeyCapacity + Screen::NameCapacity,
          "snapshot counts exceeded the fixed 32-key/16-name row capacities");
    screen.close(); noTrees(baseline, pump);
  }

  scenario = "failed read is distinct from empty state";
  {
    Fixture f;
    Screen screen(f.host()); f.screen = &screen;
    f.readOkay = false;
    screen.show();
    check(f.reads == 1 && findLabel(screen.root(), TR("Could not load blocked users")) &&
              !findLabel(screen.root(), emptyText),
          "failed snapshot read was presented as an empty list");
    screen.close(); noTrees(baseline, pump);
  }

  scenario = "empty-page external DELETE observers";
  {
    Fixture f;
    Screen screen(f.host()); f.screen = &screen;
    screen.show();
    check(f.reads == 1 && findLabel(screen.root(), emptyText),
          "empty snapshot did not present the empty state");
    auto *root = screen.root();
    unsigned observers = 0;
    lv_obj_add_event_cb(root, observedDelete, LV_EVENT_DELETE, &observers);
    lv_obj_add_event_cb(root, observedDelete, LV_EVENT_DELETE, &observers);
    lv_obj_del(root);
    check(!screen.isOpen() && observers == 2 && f.ends == 1,
          "empty-page external DELETE skipped later observers or page teardown");
    screen.close(); noTrees(baseline, pump);
  }

  scenario = "Host callback replacement and retired actions";
  {
    Fixture f;
    key(f.source.keys[0], 0xD1, "Reentrant"); f.source.keyCount = 1;
    Screen screen(f.host()); f.screen = &screen;
    f.replaceOnRead = true;
    screen.show();
    check(screen.isOpen() && f.reads >= 2 && countUnblock(screen.root()) == 1,
          "readSnapshot replacement was overwritten by stale row construction");
    f.replaceOnUnblock = true;
    auto *old = unblockFor(screen.root(), "Reentrant");
    click(old);
    check(f.keyCalls == 1 && sameKey(f.lastKey, 0xD1) && screen.isOpen(),
          "unblock callback replacement lost captured identity");
    click(old);
    check(f.keyCalls == 1, "retired row acted again after unblock reentry");
    f.deferClose = false; // exercise synchronous deletion inside Host::closeRoot
    f.replaceOnClose = true;
    screen.close();
    check(screen.isOpen() && countUnblock(screen.root()) == 1,
          "closeRoot reentry closed the replacement page");
    f.replaceOnEnd = true;
    screen.close();
    check(screen.isOpen(), "endPageIfOwned reentry lost replacement page");
    screen.close(); noTrees(baseline, pump);
  }

  scenario = "chrome reentry and destructor";
  {
    Fixture f;
    key(f.source.keys[0], 0xE2, "Chrome"); f.source.keyCount = 1;
    f.replaceOnBegin = true;
    {
      Screen screen(f.host()); f.screen = &screen;
      screen.show();
      check(screen.isOpen() && countUnblock(screen.root()) == 1,
            "beginPage reentry let old show paint replacement");
      f.replaceOnFront = true;
      screen.show();
      check(screen.isOpen() && countUnblock(screen.root()) == 1,
            "status bar foreground reentry let old show paint replacement");
    }
    noTrees(baseline, pump);
  }

  std::puts("Blocked users: copied targets, bounded rows, measured chrome, retired callbacks, "
            "external DELETE and Host reentry passed.");
}

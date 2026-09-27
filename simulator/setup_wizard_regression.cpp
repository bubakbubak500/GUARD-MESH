// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/SetupWizardScreen.h"
#include "i18n.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {
using Screen = ui::screens::SetupWizardScreen;
const char *scenario = "setup";

void check(bool okay, const char *message) {
  if (okay) return;
  char detail[320];
  std::snprintf(detail, sizeof detail, "Setup wizard '%s': %s", scenario, message);
  throw std::runtime_error(detail);
}

lv_obj_t *findLabel(lv_obj_t *root, const char *text) {
  if (!root) return nullptr;
  if (lv_obj_check_type(root, &lv_label_class)) {
    const auto mode = lv_label_get_long_mode(root);
    if (mode == LV_LABEL_LONG_DOT) lv_label_set_long_mode(root, LV_LABEL_LONG_CLIP);
    const bool match = std::strcmp(lv_label_get_text(root), text) == 0;
    if (mode == LV_LABEL_LONG_DOT) lv_label_set_long_mode(root, mode);
    if (match) return root;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = findLabel(lv_obj_get_child(root, i), text)) return found;
  return nullptr;
}
lv_obj_t *findType(lv_obj_t *root, const lv_obj_class_t *type) {
  if (!root) return nullptr;
  if (lv_obj_check_type(root, type)) return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = findType(lv_obj_get_child(root, i), type)) return found;
  return nullptr;
}
lv_obj_t *button(lv_obj_t *root, const char *caption) {
  auto *label = findLabel(root, TR(caption));
  check(label != nullptr, "button label missing");
  auto *control = lv_obj_get_parent(label);
  check(control && lv_obj_check_type(control, &lv_btn_class), "label parent is not a button");
  return control;
}
void click(lv_obj_t *control) {
  check(control != nullptr, "click target missing");
  lv_event_send(control, LV_EVENT_CLICKED, nullptr);
}
void observedDelete(lv_event_t *event) {
  ++*static_cast<unsigned *>(lv_event_get_user_data(event));
}

struct Fixture {
  Screen *screen = nullptr;
  Screen::Committed committed{};
  std::string appliedName, alertText;
  unsigned presets = 3;
  unsigned reads = 0, names = 0, regions = 0, dones = 0;
  unsigned hidden = 0, shown = 0, kbHides = 0, kbSyncs = 0, attaches = 0;
  unsigned resets = 0, detaches = 0, dirties = 0, focuses = 0, closes = 0, alerts = 0;
  unsigned appliedRegion = 999;
  bool skipped = false, nameOkay = true, regionOkay = true, doneOkay = true;
  bool replaceOnSync = false, replaceOnName = false, replaceOnRegion = false;
  bool replaceOnDone = false, replaceOnStatus = false, replaceOnRead = false;
  bool backOnName = false, backOnRegion = false, deferClose = false;
  lv_obj_t *attached = nullptr, *focused = nullptr, *replacement = nullptr, *retired = nullptr;

  Fixture() { std::snprintf(committed.name, sizeof committed.name, "Old name"); }
  static Fixture &self(void *ctx) { return *static_cast<Fixture *>(ctx); }
  void replace() {
    screen->hide();
    screen->show();
    replacement = screen->root();
  }
  static bool read(void *ctx, Screen::Committed &out) {
    auto &f = self(ctx); ++f.reads; out = f.committed;
    if (f.replaceOnRead) { f.replaceOnRead = false; f.replace(); }
    return true;
  }
  static unsigned count(void *ctx) { return self(ctx).presets; }
  static const char *label(void *, unsigned index) {
    static const char *labels[] = {"Region zero", "Region one", "Region two"};
    return labels[index % 3];
  }
  static bool applyName(void *ctx, const char *name) {
    auto &f = self(ctx); ++f.names; f.appliedName = name ? name : "";
    if (f.replaceOnName) { f.replaceOnName = false; f.replace(); }
    if (f.backOnName) { f.backOnName = false; f.screen->back(); }
    if (f.nameOkay)
      std::snprintf(f.committed.name, sizeof f.committed.name, "%s", f.appliedName.c_str());
    return f.nameOkay;
  }
  static bool applyRegion(void *ctx, unsigned index) {
    auto &f = self(ctx); ++f.regions; f.appliedRegion = index;
    if (f.replaceOnRegion) { f.replaceOnRegion = false; f.replace(); }
    if (f.backOnRegion) { f.backOnRegion = false; f.screen->back(); }
    if (f.regionOkay) f.committed.regionIndex = static_cast<int>(index);
    return f.regionOkay;
  }
  static bool markDone(void *ctx, bool skip) {
    auto &f = self(ctx); ++f.dones; f.skipped = skip;
    if (f.replaceOnDone) { f.replaceOnDone = false; f.replace(); }
    return f.doneOkay;
  }
  static void hideKeyboard(void *ctx) { ++self(ctx).kbHides; }
  static void syncKeyboard(void *ctx) {
    auto &f = self(ctx); ++f.kbSyncs;
    if (f.replaceOnSync) { f.replaceOnSync = false; f.replace(); }
  }
  static void attach(void *ctx, lv_obj_t *field) {
    auto &f = self(ctx); ++f.attaches; f.attached = field;
  }
  static void status(void *ctx, bool hide) {
    auto &f = self(ctx);
    if (hide) ++f.hidden; else ++f.shown;
    if (hide && f.replaceOnStatus) { f.replaceOnStatus = false; f.replace(); }
  }
  static void reset(void *ctx) { ++self(ctx).resets; }
  static void detach(void *ctx) { ++self(ctx).detaches; }
  static void dirty(void *ctx) { ++self(ctx).dirties; }
  static void focus(void *ctx, lv_obj_t *object) {
    auto &f = self(ctx); ++f.focuses; f.focused = object;
  }
  static void alert(void *ctx, const char *message, unsigned) {
    auto &f = self(ctx); ++f.alerts; f.alertText = message ? message : "";
  }
  static void close(void *ctx, lv_obj_t **root) {
    auto &f = self(ctx); ++f.closes;
    if (root && *root) {
      if (f.deferClose) f.retired = *root;
      else lv_obj_del(*root);
      *root = nullptr;
    }
  }
  Screen::Host host() {
    Screen::Host result;
    result.context = this;
    result.readCommitted = read;
    result.presetCount = count;
    result.presetLabel = label;
    result.applyName = applyName;
    result.applyRegion = applyRegion;
    result.markDone = markDone;
    result.hideKeyboard = hideKeyboard;
    result.syncKeyboard = syncKeyboard;
    result.attachTextArea = attach;
    result.statusBarHidden = status;
    result.resetNav = reset;
    result.navDetachBeforeTreeMutation = detach;
    result.navDirty = dirty;
    result.focusNext = focus;
    result.showAlert = alert;
    result.closeRoot = close;
    return result;
  }
};

void start(Screen &screen) {
  screen.show();
  click(button(screen.root(), "Get Started"));
  check(screen.step() == Screen::Step::Name, "Get Started did not open name step");
}
void enterName(Screen &screen, const char *name) {
  auto *field = findType(screen.root(), &lv_textarea_class);
  check(field != nullptr, "name textarea missing");
  lv_textarea_set_text(field, name);
}
void toRegion(Screen &screen, const char *name) {
  start(screen);
  enterName(screen, name);
  click(button(screen.root(), "Next"));
  check(screen.step() == Screen::Step::Region, "name step did not advance");
}
lv_obj_t *regionRow(Screen &screen, unsigned index, unsigned expectedCount = 3) {
  lv_obj_update_layout(screen.root());
  lv_obj_t *list = nullptr;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(screen.root()); ++i) {
    auto *candidate = lv_obj_get_child(screen.root(), i);
    if (lv_obj_get_child_cnt(candidate) != expectedCount) continue;
    bool rows = true;
    for (unsigned j = 0; j < expectedCount; ++j)
      rows = rows && lv_obj_check_type(lv_obj_get_child(candidate, j), &lv_btn_class);
    if (rows) { list = candidate; break; }
  }
  check(list != nullptr, "region list missing or catalog count not bounded");
  auto *row = lv_obj_get_child(list, index);
  check(row && lv_obj_check_type(row, &lv_btn_class), "region label parent is not a button");
  return row;
}
} // namespace

void runSetupWizardRegression(void (*pump)(unsigned)) {
  const unsigned baseline = lv_obj_get_child_cnt(lv_layer_top());

  scenario = "workflow, draft, Back and finish";
  {
    Fixture f; Screen screen(f.host()); f.screen = &screen;
    screen.show(); auto *root = screen.root(); screen.show();
    check(screen.root() == root && f.hidden == 1, "show replaced a visible wizard");
    click(button(root, "Get Started"));
    enterName(screen, "Uncommitted draft");
    screen.back();
    check(screen.step() == Screen::Step::Welcome, "hardware Back missed Welcome");
    click(button(screen.root(), "Get Started"));
    auto *field = findType(screen.root(), &lv_textarea_class);
    check(field && std::strcmp(lv_textarea_get_text(field), "Old name") == 0,
          "Back did not reload committed name");
    enterName(screen, "Uncommitted draft");
    click(button(screen.root(), "Next"));
    check(f.names == 1 && f.appliedName == "Uncommitted draft" &&
          screen.step() == Screen::Step::Region, "name commit/order failed");
    click(regionRow(screen, 1));
    check(f.focuses == 0, "first region click prematurely focused Next");
    click(regionRow(screen, 1));
    check(f.focuses == 1 && f.focused == button(screen.root(), "Next"),
          "second region click did not focus Next");
    click(button(screen.root(), "Next"));
    check(f.regions == 1 && f.appliedRegion == 1 &&
          screen.step() == Screen::Step::Connectivity, "region commit/order failed");
    screen.back();
    check(screen.step() == Screen::Step::Region, "Back from connectivity missed region");
    click(button(screen.root(), "Next"));
    click(button(screen.root(), "Finish"));
    check(!screen.visible() && f.dones == 1 && !f.skipped && f.shown == 1 &&
          f.alertText == TR("Setup complete"), "Finish did not complete and close wizard");
    pump(2);
    check(lv_obj_get_child_cnt(lv_layer_top()) == baseline, "finished wizard leaked root");
  }

  scenario = "empty name, save failures and UTF-8 capacity";
  {
    Fixture f; Screen screen(f.host()); f.screen = &screen;
    start(screen); enterName(screen, ""); click(button(screen.root(), "Next"));
    check(f.names == 0 && screen.step() == Screen::Step::Name &&
          f.alertText == TR("Enter a name"), "empty name committed or advanced");
    f.nameOkay = false; enterName(screen, "Retry me");
    click(button(screen.root(), "Next"));
    check(f.names == 1 && screen.step() == Screen::Step::Name &&
          f.alertText == TR("Save failed"), "failed name save advanced");
    f.nameOkay = true;
    const std::string unicode = "Žluťoučký kůň Žluťoučký kůň"; // 27 code points, > 30 UTF-8 bytes
    check(unicode.size() > 30, "UTF-8 fixture lacks multibyte coverage");
    enterName(screen, unicode.c_str());
    click(button(screen.root(), "Next"));
    check(f.appliedName == unicode && screen.step() == Screen::Step::Region,
          "name was truncated by byte count before commit");
    f.regionOkay = false; click(regionRow(screen, 0));
    click(button(screen.root(), "Next"));
    check(f.regions == 1 && screen.step() == Screen::Step::Region &&
          f.alertText == TR("Save failed"), "failed region save advanced");
    f.regionOkay = true; click(button(screen.root(), "Next"));
    f.doneOkay = false; click(button(screen.root(), "Finish"));
    check(screen.visible() && f.dones == 1 && f.alertText == TR("Save failed"),
          "failed completion closed wizard");
    f.doneOkay = true; click(button(screen.root(), "Finish"));
    check(!screen.visible() && f.dones == 2, "completion retry did not close wizard");
    pump(2);
  }

  scenario = "bounded region catalog and skip";
  {
    Fixture f; f.presets = Screen::PresetCapacity + 40;
    Screen screen(f.host()); f.screen = &screen;
    toRegion(screen, "Bounded");
    auto *row = regionRow(screen, Screen::PresetCapacity - 1, Screen::PresetCapacity);
    click(row); click(button(screen.root(), "Next"));
    check(f.appliedRegion == Screen::PresetCapacity - 1 &&
          screen.step() == Screen::Step::Connectivity, "bounded final region not selectable");
    screen.hide(); screen.show();
    click(button(screen.root(), "Skip"));
    check(!screen.visible() && f.dones == 1 && f.skipped &&
          f.alertText == TR("You can run setup later in Settings \xE2\x86\x92 Device"),
          "Skip did not mark done and close");
    pump(2);
  }

  scenario = "sync and commit callbacks replace active wizard";
  {
    Fixture f; Screen screen(f.host()); f.screen = &screen;
    start(screen); enterName(screen, "Stale");
    f.replaceOnSync = true; click(button(screen.root(), "Next"));
    check(screen.root() == f.replacement && screen.step() == Screen::Step::Welcome &&
          f.names == 0, "keyboard sync committed stale name after replacement");
    click(button(screen.root(), "Get Started"));
    enterName(screen, "Stale again");
    f.replaceOnName = true; click(button(screen.root(), "Next"));
    check(screen.root() == f.replacement && screen.step() == Screen::Step::Welcome &&
          f.names == 1, "name callback advanced replacement wizard");
    toRegion(screen, "New session");
    click(regionRow(screen, 2));
    f.replaceOnRegion = true; click(button(screen.root(), "Next"));
    check(screen.root() == f.replacement && screen.step() == Screen::Step::Welcome &&
          f.regions == 1, "region callback advanced replacement wizard");
    toRegion(screen, "Final session");
    click(button(screen.root(), "Next")); // no region selection is allowed
    check(screen.step() == Screen::Step::Connectivity,
          "optional region failed to advance to connectivity");
    f.replaceOnDone = true; click(button(screen.root(), "Finish"));
    check(screen.visible() && screen.root() == f.replacement &&
          screen.step() == Screen::Step::Welcome && f.dones == 1,
          "completion callback closed or advanced replacement wizard");
    screen.hide(); pump(2);
  }

  scenario = "same-root Back during Host save";
  {
    Fixture f; Screen screen(f.host()); f.screen = &screen;
    start(screen); enterName(screen, "Back from callback");
    auto *root = screen.root();
    f.backOnName = true; click(button(root, "Next"));
    check(screen.root() == root && screen.step() == Screen::Step::Welcome &&
          f.names == 1, "old name continuation advanced after Host Back");
    toRegion(screen, "Region callback");
    click(regionRow(screen, 2));
    f.backOnRegion = true; click(button(root, "Next"));
    check(screen.root() == root && screen.step() == Screen::Step::Name &&
          f.regions == 1, "old region continuation advanced after Host Back");
    screen.hide(); pump(2);
  }

  scenario = "read callback replaces wizard and optional closer";
  {
    Fixture f; Screen screen(f.host()); f.screen = &screen;
    f.replaceOnRead = true;
    screen.show();
    check(screen.visible() && screen.root() == f.replacement &&
          screen.step() == Screen::Step::Welcome && f.reads >= 2,
          "stale committed read mutated or closed replacement wizard");
    screen.hide(); pump(2);
  }
  {
    Fixture f; auto host = f.host(); host.closeRoot = nullptr;
    Screen screen(host); f.screen = &screen;
    screen.show(); screen.hide(); pump(2);
    check(lv_obj_get_child_cnt(lv_layer_top()) == baseline,
          "missing closeRoot Host leaked wizard overlay");
  }

  scenario = "external DELETE and stale button callbacks";
  {
    Fixture f; Screen screen(f.host()); f.screen = &screen;
    screen.show(); auto *root = screen.root();
    unsigned deletes = 0;
    lv_obj_add_event_cb(root, observedDelete, LV_EVENT_DELETE, &deletes);
    lv_obj_del(root);
    check(!screen.visible() && deletes == 1 && f.shown == 1 && f.dones == 0,
          "external root DELETE left wizard active or skipped observers");
    screen.show();
    check(screen.root() && screen.step() == Screen::Step::Welcome &&
          button(screen.root(), "Get Started"), "show did not rebuild deleted wizard");
    // Keep a retired tree alive to exercise its detached event callbacks.
    auto *retired = screen.root();
    auto *oldStarted = button(retired, "Get Started");
    f.closes = 0; f.deferClose = true;
    screen.hide();
    check(f.closes == 1 && f.retired == retired && !screen.visible(),
          "hide did not retire root once");
    click(oldStarted);
    check(!screen.visible() && screen.step() == Screen::Step::Welcome,
          "retired Get Started button remained active");
    lv_obj_del(f.retired); f.retired = nullptr;
    f.deferClose = false;
    screen.show();
    check(screen.step() == Screen::Step::Welcome && f.dones == 0,
          "retired controls affected replacement wizard");
    screen.hide(); pump(2);
  }

  check(lv_obj_get_child_cnt(lv_layer_top()) == baseline, "wizard regression leaked overlay");
  std::puts("Setup wizard: workflow, drafts, failures, bounded regions, replacement and DELETE passed.");
}

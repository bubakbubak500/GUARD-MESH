// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.h"
#include "screens/WifiFormsScreen.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Screen = ui::screens::WifiFormsScreen;

const char *scenario = "setup";
void check(bool condition, const char *message) {
  if (condition) return;
  char detail[320];
  std::snprintf(detail, sizeof detail, "Wi-Fi forms '%s': %s", scenario, message);
  throw std::runtime_error(detail);
}

std::string labelText(lv_obj_t *label) {
  const auto mode = lv_label_get_long_mode(label);
  if (mode == LV_LABEL_LONG_DOT) lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  const char *value = lv_label_get_text(label);
  const std::string copy = value ? value : "";
  if (mode == LV_LABEL_LONG_DOT) lv_label_set_long_mode(label, mode);
  return copy;
}

lv_obj_t *findLabel(lv_obj_t *root, const char *text, bool substring = false) {
  if (!root) return nullptr;
  if (lv_obj_check_type(root, &lv_label_class)) {
    const std::string value = labelText(root);
    if (substring ? value.find(text) != std::string::npos : value == text) return root;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = findLabel(lv_obj_get_child(root, i), text, substring)) return found;
  return nullptr;
}

void collectLabels(lv_obj_t *root, std::vector<std::string> &out) {
  if (!root) return;
  if (lv_obj_check_type(root, &lv_label_class)) out.push_back(labelText(root));
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    collectLabels(lv_obj_get_child(root, i), out);
}

unsigned labelIndex(const std::vector<std::string> &labels, const char *value) {
  for (unsigned i = 0; i < labels.size(); ++i)
    if (labels[i] == value) return i;
  return static_cast<unsigned>(labels.size());
}

unsigned labelCount(const std::vector<std::string> &labels, const char *value) {
  unsigned result = 0;
  for (const auto &label : labels) if (label == value) ++result;
  return result;
}

void collectType(lv_obj_t *root, const lv_obj_class_t *type, std::vector<lv_obj_t *> &out) {
  if (!root) return;
  if (lv_obj_check_type(root, type)) out.push_back(root);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    collectType(lv_obj_get_child(root, i), type, out);
}

lv_obj_t *findType(lv_obj_t *root, const lv_obj_class_t *type, unsigned occurrence = 0) {
  std::vector<lv_obj_t *> found;
  collectType(root, type, found);
  return occurrence < found.size() ? found[occurrence] : nullptr;
}

lv_obj_t *button(lv_obj_t *root, const char *caption, bool substring = false) {
  lv_obj_update_layout(root);
  auto *label = findLabel(root, caption, substring);
  check(label != nullptr, "expected labeled control missing");
  return lv_obj_get_parent(label);
}

void click(lv_obj_t *object) {
  check(object != nullptr, "click target missing");
  lv_event_send(object, LV_EVENT_CLICKED, nullptr);
}

lv_obj_t *body() {
  auto *root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, 220, 260);
  return root;
}

void deleted(lv_event_t *event) {
  ++*static_cast<unsigned *>(lv_event_get_user_data(event));
}

struct Fixture {
  struct Action {
    std::string kind, ssid, value;
    bool flag;
    Action(const char *action, const char *name, const char *text, bool enabled)
      : kind(action ? action : ""), ssid(name ? name : ""),
        value(text ? text : ""), flag(enabled) {}
  };
  Screen *screen = nullptr;
  Screen::PageSnapshot state{};
  std::vector<Action> actions;
  std::string storedSsid, storedPassword, title, alertText;
  lv_obj_t *replacementBody = nullptr;
  lv_obj_t *syncField = nullptr;
  std::string syncValue;
  bool applySyncValue = false;
  unsigned snapshots = 0, scans = 0, alerts = 0, pageBegins = 0, pageEnds = 0;
  unsigned syncs = 0, hides = 0, mirrors = 0, navs = 0, statusUpdates = 0;
  unsigned closes = 0;
  bool mirrorHidden = true;
  bool failSave = false, failRadio = false, failAutoJoin = false;
  bool replaceOnSync = false, replaceOnClose = false, replaceOnSave = false;
  bool replaceOnConnect = false, replaceOnForget = false, replaceOnAutoJoin = false;
  bool replaceOnRadio = false, replaceOnPasswordLoad = false;

  Fixture() {
    state.radioEnabled = true;
    std::snprintf(state.status, sizeof state.status, "%s", "Wi-Fi ready");
  }
  static Fixture &self(void *context) { return *static_cast<Fixture *>(context); }
  void addSaved(const char *ssid, uint32_t rank, bool autoJoin = false) {
    check(state.savedCount < Screen::SavedCapacity, "fixture saved capacity exceeded");
    auto &slot = state.saved[state.savedCount++];
    std::snprintf(slot.ssid, sizeof slot.ssid, "%s", ssid);
    slot.rank = rank;
    slot.autoJoin = autoJoin;
  }
  void addScan(const char *ssid) {
    check(state.scannedCount < Screen::ScanCapacity, "fixture scan capacity exceeded");
    std::snprintf(state.scanned[state.scannedCount++],
                  sizeof state.scanned[0], "%s", ssid);
  }
  bool hasSaved(const char *ssid) const {
    for (unsigned i = 0; i < state.savedCount; ++i)
      if (!std::strcmp(state.saved[i].ssid, ssid)) return true;
    return false;
  }
  void replacePage() {
    if (!screen) return;
    auto *next = replacementBody ? replacementBody : body();
    replacementBody = nullptr;
    screen->build(next, 220);
  }
  void reenter(bool &flag) {
    if (!flag) return;
    flag = false;
    replacePage();
  }
  static bool readSnapshot(void *context, Screen::PageSnapshot &out) {
    auto &f = self(context);
    ++f.snapshots;
    out = f.state;
    return true;
  }
  static bool loadPassword(void *context, const char *ssid, char *out, size_t capacity) {
    auto &f = self(context);
    f.actions.push_back({"load", ssid ? ssid : "", "", false});
    f.reenter(f.replaceOnPasswordLoad);
    if (f.storedSsid != (ssid ? ssid : "") || !capacity) return false;
    std::snprintf(out, capacity, "%s", f.storedPassword.c_str());
    return true;
  }
  static bool saveAndConnect(void *context, const char *ssid, const char *password, bool autoJoin) {
    auto &f = self(context);
    f.actions.push_back({"save", ssid ? ssid : "", password ? password : "", autoJoin});
    f.reenter(f.replaceOnSave);
    return !f.failSave;
  }
  static bool connectSaved(void *context, const char *ssid) {
    auto &f = self(context);
    f.actions.push_back({"connect", ssid ? ssid : "", "", false});
    f.reenter(f.replaceOnConnect);
    return f.hasSaved(ssid);
  }
  static bool forgetSaved(void *context, const char *ssid) {
    auto &f = self(context);
    f.actions.push_back({"forget", ssid ? ssid : "", "", false});
    f.reenter(f.replaceOnForget);
    return f.hasSaved(ssid);
  }
  static bool setAutoJoin(void *context, const char *ssid, bool enabled) {
    auto &f = self(context);
    f.actions.push_back({"auto", ssid ? ssid : "", "", enabled});
    f.reenter(f.replaceOnAutoJoin);
    return !f.failAutoJoin && f.hasSaved(ssid);
  }
  static bool setRadioEnabled(void *context, bool enabled) {
    auto &f = self(context);
    f.actions.push_back({"radio", "", "", enabled});
    if (f.failRadio) return false;
    f.state.radioEnabled = enabled;
    f.reenter(f.replaceOnRadio);
    return true;
  }
  static bool requestScan(void *context) { ++self(context).scans; return true; }
  static void syncKeyboard(void *context) {
    auto &f = self(context);
    ++f.syncs;
    if (f.applySyncValue && f.syncField) {
      lv_textarea_set_text(f.syncField, f.syncValue.c_str());
      f.applySyncValue = false;
    }
    f.reenter(f.replaceOnSync);
  }
  static void hideKeyboard(void *context) { ++self(context).hides; }
  static void attachTextArea(void *, lv_obj_t *) {}
  static void attachSymbolButton(void *, lv_obj_t *) {}
  static void setPasswordMirror(void *context, bool hidden) {
    auto &f = self(context);
    ++f.mirrors;
    f.mirrorHidden = hidden;
  }
  static void alert(void *context, const char *message, unsigned) {
    auto &f = self(context);
    ++f.alerts;
    f.alertText = message ? message : "";
  }
  static int statusHeight(void *) { return 24; }
  static void beginPage(void *context, const char *title) {
    auto &f = self(context);
    ++f.pageBegins;
    f.title = title ? title : "";
  }
  static void endPageIfOwned(void *context) { ++self(context).pageEnds; }
  static void updateStatusBar(void *context) { ++self(context).statusUpdates; }
  static void navDirty(void *context) { ++self(context).navs; }
  static void closeRoot(void *context, lv_obj_t **root) {
    auto &f = self(context);
    if (!root || !*root) return;
    auto *old = *root;
    *root = nullptr;
    ++f.closes;
    lv_obj_del_async(old);
    f.reenter(f.replaceOnClose);
  }
  Screen::Host host() {
    Screen::Host h;
    h.context = this;
    h.readSnapshot = readSnapshot;
    h.loadPassword = loadPassword;
    h.saveAndConnect = saveAndConnect;
    h.connectSaved = connectSaved;
    h.forgetSaved = forgetSaved;
    h.setAutoJoin = setAutoJoin;
    h.setRadioEnabled = setRadioEnabled;
    h.requestScan = requestScan;
    h.syncKeyboard = syncKeyboard;
    h.hideKeyboard = hideKeyboard;
    h.attachTextArea = attachTextArea;
    h.attachSymbolButton = attachSymbolButton;
    h.setPasswordMirror = setPasswordMirror;
    h.alert = alert;
    h.statusHeight = statusHeight;
    h.beginPage = beginPage;
    h.endPageIfOwned = endPageIfOwned;
    h.updateStatusBar = updateStatusBar;
    h.navDirty = navDirty;
    h.closeRoot = closeRoot;
    return h;
  }
};

} // namespace

void runWifiFormsRegression(void (*pump)(unsigned)) {
  scenario = "saved rank and scanned identity";
  {
    Fixture f;
    f.addSaved("Saved Low", 2);
    f.addSaved("Saved High", 20, true);
    f.addScan("Cafe North");
    f.addScan("Saved High");
    f.addScan("Library West");
    Screen screen(f.host());
    f.screen = &screen;
    auto *page = body();
    screen.build(page, 220);
    std::vector<std::string> labels;
    collectLabels(page, labels);
    check(labelCount(labels, "Saved High") == 1 && labelCount(labels, "Saved Low") == 1 &&
          labelCount(labels, "Cafe North") == 1 && labelCount(labels, "Library West") == 1,
          "saved or scanned rows missing or duplicated");
    check(labelIndex(labels, "Saved High") < labelIndex(labels, "Saved Low"),
          "saved networks are not ranked by descending priority");
    check(labelIndex(labels, "Cafe North") < labelIndex(labels, "Library West"),
          "scanned order was changed");
    check(f.pageBegins == 0, "building the list opened a sheet page");

    auto *captured = button(page, "Cafe North");
    std::snprintf(f.state.scanned[0], sizeof f.state.scanned[0], "%s", "Library West");
    std::snprintf(f.state.scanned[2], sizeof f.state.scanned[2], "%s", "Cafe North");
    click(captured);
    check(screen.sheetOpen() && f.pageBegins == 1, "captured scanned row did not open join sheet");
    check(!std::strcmp(screen.pageTitle(), "Cafe North") && f.title == "Cafe North" &&
          !f.actions.empty() && f.actions.back().kind == "load" &&
          f.actions.back().ssid == "Cafe North",
          "scanned row used a reordered host index instead of its captured SSID");
    screen.closeSheet();
    pump(2);
    const unsigned beforeRescan = f.scans;
    click(button(page, TR("Scan again"), true));
    check(f.scans == beforeRescan + 1, "Scan again did not request a new scan");
    screen.detach();
    lv_obj_del(page);
    pump(2);
  }

  scenario = "saved identity after slot replacement";
  {
    Fixture f;
    f.addSaved("Home Mesh", 12);
    f.addSaved("Office Mesh", 5);
    Screen screen(f.host());
    f.screen = &screen;
    auto *page = body();
    screen.build(page, 220);
    auto *captured = button(page, "Home Mesh");
    std::snprintf(f.state.saved[0].ssid, sizeof f.state.saved[0].ssid,
                  "%s", "Replacement Mesh");
    click(captured);
    check(!screen.sheetOpen() && f.actions.empty(),
          "replaced saved slot opened or operated on the wrong network");

    std::snprintf(f.state.saved[0].ssid, sizeof f.state.saved[0].ssid,
                  "%s", "Home Mesh");
    screen.rebuildList();
    click(button(page, "Home Mesh"));
    check(screen.sheetOpen() && !std::strcmp(screen.pageTitle(), "Home Mesh"),
          "saved details lost captured network identity");
    std::snprintf(f.state.saved[0].ssid, sizeof f.state.saved[0].ssid,
                  "%s", "Replacement Mesh");
    click(button(screen.focusRoot(), TR("Connect")));
    check(!f.actions.empty() && f.actions.back().kind == "connect" &&
          f.actions.back().ssid == "Home Mesh" && screen.sheetOpen(),
          "connect used replacement slot or closed on a rejected target");
    auto *toggle = findType(screen.focusRoot(), &lv_switch_class);
    check(toggle != nullptr, "details auto-join switch missing");
    lv_obj_add_state(toggle, LV_STATE_CHECKED);
    lv_event_send(toggle, LV_EVENT_VALUE_CHANGED, nullptr);
    check(f.actions.back().kind == "auto" && f.actions.back().ssid == "Home Mesh" &&
          f.actions.back().flag && !lv_obj_has_state(toggle, LV_STATE_CHECKED),
          "auto-join applied to replacement slot or failed to roll back");
    std::snprintf(f.state.saved[0].ssid, sizeof f.state.saved[0].ssid,
                  "%s", "Home Mesh");
    lv_obj_add_state(toggle, LV_STATE_CHECKED);
    lv_event_send(toggle, LV_EVENT_VALUE_CHANGED, nullptr);
    check(f.actions.back().kind == "auto" && f.actions.back().ssid == "Home Mesh" &&
          f.actions.back().flag,
          "auto-join did not address captured SSID after slot restoration");
    click(button(screen.focusRoot(), TR("Forget network")));
    check(f.actions.back().kind == "forget" && f.actions.back().ssid == "Home Mesh" &&
          !screen.sheetOpen(), "forget targeted wrong SSID or left details open");
    pump(2);
    screen.detach();
    lv_obj_del(page);
    pump(2);
  }

  scenario = "hidden join mirrors keyboard and retains failed draft";
  {
    Fixture f;
    Screen screen(f.host());
    f.screen = &screen;
    auto *page = body();
    screen.build(page, 220);
    click(button(page, TR("Other (hidden) network\xe2\x80\xa6"), true));
    check(screen.sheetOpen() && !std::strcmp(screen.pageTitle(), TR("Hidden network")),
          "hidden network form did not open");
    auto *ssid = findType(screen.focusRoot(), &lv_textarea_class, 0);
    auto *password = findType(screen.focusRoot(), &lv_textarea_class, 1);
    check(ssid && password, "hidden SSID/password fields missing");
    lv_textarea_set_text(ssid, "stale-name");
    lv_textarea_set_text(password, "draft-password");
    auto *eye = button(screen.focusRoot(), LV_SYMBOL_EYE_OPEN);
    click(eye);
    check(!lv_textarea_get_password_mode(password) && !f.mirrorHidden,
          "password reveal did not update field and keyboard mirror");
    click(eye);
    check(lv_textarea_get_password_mode(password) && f.mirrorHidden,
          "password rehide did not update field and keyboard mirror");

    f.syncField = ssid;
    f.syncValue = "fresh-hidden-name";
    f.applySyncValue = true;
    f.failSave = true;
    click(button(screen.focusRoot(), TR("Join")));
    check(f.syncs == 1 && !f.actions.empty() && f.actions.back().kind == "save" &&
          f.actions.back().ssid == "fresh-hidden-name" &&
          f.actions.back().value == "draft-password" && f.actions.back().flag,
          "Join read stale hidden SSID/password or lost auto-join choice");
    check(screen.sheetOpen() && !std::strcmp(lv_textarea_get_text(ssid), "fresh-hidden-name") &&
          !std::strcmp(lv_textarea_get_text(password), "draft-password") &&
          f.alertText == TR("Save failed"),
          "failed save closed form, erased draft, or announced a connection");

    f.failSave = false;
    f.syncField = password;
    f.syncValue = "fresh-password";
    f.applySyncValue = true;
    click(button(screen.focusRoot(), TR("Join")));
    check(f.actions.back().kind == "save" &&
          f.actions.back().ssid == "fresh-hidden-name" &&
          f.actions.back().value == "fresh-password" && !screen.sheetOpen(),
          "successful Join did not sync password or close its sheet");
    check(f.alertText == TR("Connecting\xe2\x80\xa6"),
          "successful Join did not announce connection");
    pump(2);
    screen.detach();
    lv_obj_del(page);
    pump(2);
  }

  scenario = "radio rejection and reentrant replacement";
  {
    Fixture f;
    Screen screen(f.host());
    f.screen = &screen;
    auto *page = body();
    screen.build(page, 220);
    auto *radio = findType(page, &lv_switch_class);
    check(radio && lv_obj_has_state(radio, LV_STATE_CHECKED),
          "enabled radio switch was not initialized");
    f.failRadio = true;
    lv_obj_clear_state(radio, LV_STATE_CHECKED);
    lv_event_send(radio, LV_EVENT_VALUE_CHANGED, nullptr);
    check(!f.actions.empty() && f.actions.back().kind == "radio" &&
          !f.actions.back().flag && lv_obj_has_state(radio, LV_STATE_CHECKED),
          "rejected radio change was not rolled back");
    f.failRadio = false;
    f.replaceOnRadio = true;
    auto *replacement = body();
    f.replacementBody = replacement;
    lv_obj_clear_state(radio, LV_STATE_CHECKED);
    lv_event_send(radio, LV_EVENT_VALUE_CHANGED, nullptr);
    check(f.actions.back().kind == "radio" && !f.actions.back().flag &&
          findType(replacement, &lv_switch_class) != nullptr &&
          !lv_obj_has_state(findType(replacement, &lv_switch_class), LV_STATE_CHECKED),
          "radio callback touched stale widget or lost replacement page");
    screen.detach();
    lv_obj_del(page);
    lv_obj_del(replacement);
    pump(2);
  }

  scenario = "list rebuild destroys old rows and retires callbacks";
  {
    Fixture f;
    f.addSaved("Original", 1);
    Screen screen(f.host());
    f.screen = &screen;
    auto *oldPage = body();
    screen.build(oldPage, 220);
    auto *oldRow = button(oldPage, "Original");
    unsigned oldRowDeletes = 0;
    lv_obj_add_event_cb(oldRow, deleted, LV_EVENT_DELETE, &oldRowDeletes);
    screen.rebuildList();
    check(oldRowDeletes == 1, "scan list rebuild retained an obsolete row");
    auto *retiredRow = button(oldPage, "Original");
    auto *newPage = body();
    screen.build(newPage, 220);
    click(retiredRow);
    check(!screen.sheetOpen() && f.pageBegins == 0,
          "retired list row retained a live callback");
    screen.detach();
    lv_obj_del(oldPage);
    lv_obj_del(newPage);
    pump(2);
  }

  scenario = "reentrant Join and close callbacks";
  for (unsigned operation = 0; operation < 3; ++operation) {
    Fixture f;
    f.addScan("Transient Cafe");
    Screen screen(f.host());
    f.screen = &screen;
    auto *page = body();
    screen.build(page, 220);
    click(button(page, "Transient Cafe"));
    check(screen.sheetOpen(), "reentrant test join sheet missing");
    auto *password = findType(screen.focusRoot(), &lv_textarea_class);
    check(password, "reentrant test password field missing");
    lv_textarea_set_text(password, "reentrant-draft");
    auto *replacement = body();
    f.replacementBody = replacement;
    if (operation == 0) f.replaceOnSync = true;
    if (operation == 1) f.replaceOnSave = true;
    if (operation == 2) f.replaceOnClose = true;
    if (operation == 2) click(button(screen.focusRoot(), TR("Cancel")));
    else click(button(screen.focusRoot(), TR("Join")));
    check(findLabel(replacement, "Transient Cafe") != nullptr && !screen.sheetOpen(),
          "old sheet callback damaged replacement Wi-Fi page");
    if (operation == 0)
      check(f.actions.size() == 1 && f.actions[0].kind == "load",
            "Join continued to save after sync replaced its sheet");
    if (operation == 1)
      check(!f.actions.empty() && f.actions.back().kind == "save" &&
            f.alertText != TR("Connecting\xe2\x80\xa6"),
            "Join continued to announce connection after save replaced its sheet");
    screen.detach();
    lv_obj_del(page);
    lv_obj_del(replacement);
    pump(2);
  }

  scenario = "reentrant details operations";
  for (unsigned operation = 0; operation < 3; ++operation) {
    Fixture f;
    f.addSaved("Trusted Mesh", 4);
    Screen screen(f.host());
    f.screen = &screen;
    auto *page = body();
    screen.build(page, 220);
    click(button(page, "Trusted Mesh"));
    check(screen.sheetOpen(), "reentrant details sheet missing");
    auto *replacement = body();
    f.replacementBody = replacement;
    if (operation == 0) {
      f.replaceOnConnect = true;
      click(button(screen.focusRoot(), TR("Connect")));
    } else if (operation == 1) {
      f.replaceOnForget = true;
      click(button(screen.focusRoot(), TR("Forget network")));
    } else {
      f.replaceOnAutoJoin = true;
      auto *toggle = findType(screen.focusRoot(), &lv_switch_class);
      check(toggle, "reentrant auto-join switch missing");
      lv_obj_add_state(toggle, LV_STATE_CHECKED);
      lv_event_send(toggle, LV_EVENT_VALUE_CHANGED, nullptr);
    }
    check(!f.actions.empty() && f.actions.back().ssid == "Trusted Mesh" &&
          findLabel(replacement, "Trusted Mesh") != nullptr && !screen.sheetOpen(),
          "details action lost SSID or continued against replacement page");
    check(f.alertText != TR("Connecting\xe2\x80\xa6"),
          "old details action announced connection after replacement");
    screen.detach();
    lv_obj_del(page);
    lv_obj_del(replacement);
    pump(2);
  }

  scenario = "external sheet and body DELETE observers";
  {
    Fixture f;
    f.addScan("Delete Cafe");
    Screen screen(f.host());
    f.screen = &screen;
    auto *page = body();
    screen.build(page, 220);
    click(button(page, "Delete Cafe"));
    auto *sheet = screen.focusRoot();
    check(sheet, "external-delete sheet missing");
    unsigned first = 0, second = 0;
    lv_obj_add_event_cb(sheet, deleted, LV_EVENT_DELETE, &first);
    lv_obj_add_event_cb(sheet, deleted, LV_EVENT_DELETE, &second);
    lv_obj_del(sheet);
    check(first == 1 && second == 1 && !screen.sheetOpen() &&
          !screen.pageTitle()[0] && f.pageEnds == 1,
          "sheet DELETE skipped observers or retained title/page hook");
    screen.rebuildList();
    click(button(page, "Delete Cafe"));
    check(screen.sheetOpen(), "scan refresh failed after sheet DELETE");
    lv_obj_del(page);
    check(!screen.sheetOpen(), "body DELETE left a live sheet");
    screen.rebuildList();
    screen.refreshStatus();
    pump(2);
  }

  scenario = "destructor retires callbacks";
  {
    Fixture f;
    f.addSaved("Retired Mesh", 1);
    auto *screen = new Screen(f.host());
    f.screen = screen;
    auto *page = body();
    screen->build(page, 220);
    auto *oldRow = button(page, "Retired Mesh");
    delete screen;
    f.screen = nullptr;
    click(oldRow);
    check(f.pageBegins == 0 && f.actions.empty(),
          "destroyed owner left an actionable row callback");
    lv_obj_del(page);
    pump(2);
  }

  scenario = "snapshot count and unterminated SSID bounds";
  {
    Fixture f;
    f.state.savedCount = Screen::SavedCapacity + 7;
    f.state.scannedCount = Screen::ScanCapacity + 7;
    for (unsigned i = 0; i < Screen::SavedCapacity; ++i) {
      std::memset(f.state.saved[i].ssid, 'S', sizeof f.state.saved[i].ssid);
      f.state.saved[i].rank = i;
    }
    for (unsigned i = 0; i < Screen::ScanCapacity; ++i)
      std::memset(f.state.scanned[i], 'C', sizeof f.state.scanned[i]);
    Screen screen(f.host());
    f.screen = &screen;
    auto *page = body();
    screen.build(page, 220);
    std::vector<std::string> labels;
    collectLabels(page, labels);
    const std::string saved(32, 'S'), scanned(31, 'C');
    check(labelCount(labels, saved.c_str()) == Screen::SavedCapacity &&
          labelCount(labels, scanned.c_str()) == Screen::ScanCapacity,
          "snapshot count or unterminated SSID escaped bounded rendering");
    screen.detach();
    lv_obj_del(page);
    pump(2);
  }
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.h"
#include "screens/BluetoothSettingsScreen.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Screen = ui::screens::BluetoothSettingsScreen;
const char *scenario = "setup";

void check(bool okay, const char *message) {
  if (okay) return;
  char detail[320];
  std::snprintf(detail, sizeof detail, "Bluetooth settings '%s': %s", scenario, message);
  throw std::runtime_error(detail);
}

std::string labelText(lv_obj_t *label) {
  const auto mode = lv_label_get_long_mode(label);
  if (mode == LV_LABEL_LONG_DOT) lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  const char *text = lv_label_get_text(label);
  const std::string value = text ? text : "";
  if (mode == LV_LABEL_LONG_DOT) lv_label_set_long_mode(label, mode);
  return value;
}
lv_obj_t *findLabel(lv_obj_t *root, const char *text, bool substring = false) {
  if (!root) return nullptr;
  if (lv_obj_check_type(root, &lv_label_class)) {
    const std::string value = labelText(root);
    if (substring ? value.find(text) != std::string::npos : value == text) return root;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *match = findLabel(lv_obj_get_child(root, i), text, substring)) return match;
  return nullptr;
}
lv_obj_t *findType(lv_obj_t *root, const lv_obj_class_t *type) {
  if (!root) return nullptr;
  if (lv_obj_check_type(root, type)) return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *match = findType(lv_obj_get_child(root, i), type)) return match;
  return nullptr;
}
lv_obj_t *button(lv_obj_t *root, const char *text, bool substring = false) {
  lv_obj_update_layout(root);
  auto *label = findLabel(root, text, substring);
  check(label != nullptr, "expected button label missing");
  auto *control = lv_obj_get_parent(label);
  check(control && lv_obj_check_type(control, &lv_btn_class), "label parent is not a button");
  return control;
}
lv_obj_t *switchFor(lv_obj_t *root, const char *caption) {
  auto *label = findLabel(root, caption);
  check(label != nullptr, "switch label missing");
  return findType(lv_obj_get_parent(label), &lv_switch_class);
}
void click(lv_obj_t *control) {
  check(control != nullptr, "click target missing");
  lv_event_send(control, LV_EVENT_CLICKED, nullptr);
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
Screen::Device device(const char *name, uint8_t last, uint8_t type, bool keyboard = true) {
  Screen::Device value{};
  std::snprintf(value.name, sizeof value.name, "%s", name);
  value.addr[0] = 0xA1; value.addr[1] = 0xB2; value.addr[2] = 0xC3;
  value.addr[3] = 0xD4; value.addr[4] = 0xE5; value.addr[5] = last;
  value.addrType = type; value.rssi = -54; value.keyboard = keyboard;
  return value;
}
bool sameDevice(const Screen::Device &a, const Screen::Device &b) {
  return !std::memcmp(a.addr, b.addr, sizeof a.addr) && a.addrType == b.addrType;
}

struct Fixture {
  Screen *screen = nullptr;
  Screen::Snapshot state{};
  Screen::Device pairedCommand{}, forgottenCommand{}, confirmationDevice{};
  lv_obj_t *pin = nullptr, *replacement = nullptr;
  std::string syncValue, alertText;
  uint32_t confirmationToken = 0, savedPin = 0;
  unsigned reads = 0, syncs = 0, pinWrites = 0, radioCalls = 0, modeCalls = 0;
  unsigned layoutCalls = 0, captureOn = 0, captureOff = 0, scanOn = 0, scanOff = 0;
  unsigned pairCalls = 0, forgetCalls = 0, confirmations = 0, openPages = 0;
  unsigned closePages = 0, alerts = 0, refits = 0, navs = 0;
  bool radioResult = true, pinResult = true, modeResult = true, pairResult = true;
  bool forgetResult = true, blurDelete = false, pairWithCode = false;
  bool replaceOnSync = false, replaceOnScanOff = false, replaceOnCaptureOff = false;
  bool replaceOnClose = false, replaceOnRadio = false, replaceOnPair = false;

  Fixture() {
    state.bleCapable = true;
    state.bleActive = true;
    state.bleRequested = true;
    state.pin = 123456;
    state.state = Screen::State::Idle;
    state.generation = 1;
  }
  static Fixture &self(void *context) { return *static_cast<Fixture *>(context); }
  void replaceSettings() {
    if (!screen || !replacement) return;
    auto *next = replacement;
    replacement = nullptr;
    screen->buildSettings(next, 220);
  }
  static bool readSnapshot(void *context, Screen::Snapshot &out) {
    auto &f = self(context); ++f.reads; out = f.state; return true;
  }
  static bool requestRadio(void *context, bool enabled) {
    auto &f = self(context); ++f.radioCalls;
    if (f.radioResult) f.state.bleRequested = enabled;
    if (f.replaceOnRadio) { f.replaceOnRadio = false; f.replaceSettings(); }
    return f.radioResult;
  }
  static const char *radioFailureText(void *) { return "Radio unavailable"; }
  static bool savePin(void *context, uint32_t pin) {
    auto &f = self(context); ++f.pinWrites; f.savedPin = pin;
    if (f.pinResult) f.state.pin = pin;
    return f.pinResult;
  }
  static bool setKeyboardMode(void *context, bool keyboard) {
    auto &f = self(context); ++f.modeCalls;
    if (f.modeResult) f.state.keyboardMode = keyboard;
    return f.modeResult;
  }
  static bool setLayout(void *context, uint8_t layout) {
    auto &f = self(context); ++f.layoutCalls; f.state.layout = layout; return true;
  }
  static void captureBackKey(void *context, bool on) {
    auto &f = self(context);
    if (on) ++f.captureOn; else ++f.captureOff;
    f.state.capturingKey = on;
    if (!on && f.replaceOnCaptureOff) {
      f.replaceOnCaptureOff = false; f.replaceSettings();
    }
  }
  static void formatBackKey(void *, uint8_t usage, char *out, size_t capacity) {
    if (capacity) std::snprintf(out, capacity, "K%u", unsigned(usage));
  }
  static void scanKeyboards(void *context, bool on) {
    auto &f = self(context);
    if (on) ++f.scanOn; else ++f.scanOff;
    if (!on && f.replaceOnScanOff) {
      f.replaceOnScanOff = false; f.replaceSettings();
    }
  }
  static bool pairKeyboard(void *context, const Screen::Device &target, bool withCode) {
    auto &f = self(context); ++f.pairCalls; f.pairedCommand = target;
    f.pairWithCode = withCode;
    if (f.replaceOnPair) { f.replaceOnPair = false; f.replaceSettings(); }
    return f.pairResult;
  }
  static bool forgetKeyboardIfMatches(void *context, const Screen::Device &target) {
    auto &f = self(context); ++f.forgetCalls; f.forgottenCommand = target;
    if (!sameDevice(target, f.state.pairedDevice)) return false;
    if (f.forgetResult) f.state.paired = false;
    return f.forgetResult;
  }
  static void requestForgetConfirmation(void *context, uint32_t token,
                                        const Screen::Device &target) {
    auto &f = self(context); ++f.confirmations;
    f.confirmationToken = token; f.confirmationDevice = target;
  }
  static void openPairingPage(void *context) { ++self(context).openPages; }
  static void closePairingPage(void *context) {
    auto &f = self(context); ++f.closePages;
    if (f.replaceOnClose) { f.replaceOnClose = false; f.replaceSettings(); }
  }
  static void syncKeyboard(void *context) {
    auto &f = self(context); ++f.syncs;
    if (f.pin && !f.syncValue.empty())
      lv_textarea_set_text(f.pin, f.syncValue.c_str());
    if (f.replaceOnSync) { f.replaceOnSync = false; f.replaceSettings(); }
  }
  static bool blurFromDelete(void *context, lv_event_t *) {
    return self(context).blurDelete;
  }
  static void attachPinTextArea(void *context, lv_obj_t *field) {
    self(context).pin = field;
  }
  static void styleLayoutDropdown(void *, lv_obj_t *) {}
  static void alert(void *context, const char *text, unsigned) {
    auto &f = self(context); ++f.alerts; f.alertText = text ? text : "";
  }
  static void refitPage(void *context) { ++self(context).refits; }
  static void navDetach(void *) {}
  static void navDirty(void *context) { ++self(context).navs; }

  Screen::Host host() {
    Screen::Host h;
    h.context = this;
    h.readSnapshot = readSnapshot;
    h.requestRadio = requestRadio; h.radioFailureText = radioFailureText;
    h.savePin = savePin; h.setKeyboardMode = setKeyboardMode;
    h.setLayout = setLayout; h.captureBackKey = captureBackKey;
    h.formatBackKey = formatBackKey; h.scanKeyboards = scanKeyboards;
    h.pairKeyboard = pairKeyboard; h.forgetKeyboardIfMatches = forgetKeyboardIfMatches;
    h.requestForgetConfirmation = requestForgetConfirmation;
    h.openPairingPage = openPairingPage; h.closePairingPage = closePairingPage;
    h.syncKeyboard = syncKeyboard; h.blurFromDelete = blurFromDelete;
    h.attachPinTextArea = attachPinTextArea; h.styleLayoutDropdown = styleLayoutDropdown;
    h.alert = alert; h.refitPage = refitPage;
    h.navDetachBeforeTreeMutation = navDetach; h.navDirty = navDirty;
    return h;
  }
};

void cleanup(Screen &screen, lv_obj_t *first, lv_obj_t *second, void (*pump)(unsigned)) {
  screen.detach();
  if (first) lv_obj_del(first);
  if (second) lv_obj_del(second);
  pump(2);
}
} // namespace

void runBluetoothSettingsRegression(void (*pump)(unsigned)) {
  scenario = "capabilities and phone PIN";
  {
    Fixture f;
    Screen screen(f.host()); f.screen = &screen;
    auto *page = body();
    f.state.bleCapable = false;
    screen.buildSettings(page, 220);
    check(findLabel(page, TR("Bluetooth pairing isn't available on this device.\n\n"
                             "The radio chip's factory firmware doesn't support it.\n\n"
                             "Pair the phone app over Wi-Fi instead: connect this device to your "
                             "network, then add it in the app by its IP address (TCP, port 5000) — "
                             "shown under Settings > Network. USB works too.")) != nullptr &&
          findType(page, &lv_switch_class) == nullptr,
          "no-capability page offered a radio control");
    f.state.bleCapable = true;
    screen.buildSettings(page, 220);
    auto *radio = switchFor(page, TR("Enable Bluetooth"));
    auto *pin = findType(page, &lv_textarea_class);
    check(radio && pin && findLabel(page, TR("Pairing code")),
          "phone settings omitted PIN or radio");
    lv_textarea_set_text(pin, "12345");
    lv_event_send(pin, LV_EVENT_DEFOCUSED, nullptr);
    check(f.pinWrites == 0, "incomplete PIN was saved");
    f.blurDelete = true;
    lv_textarea_set_text(pin, "654321");
    lv_event_send(pin, LV_EVENT_DEFOCUSED, nullptr);
    check(f.pinWrites == 0, "DELETE blur saved PIN");
    f.blurDelete = false;
    f.syncValue = "654321";
    lv_event_send(pin, LV_EVENT_DEFOCUSED, nullptr);
    check(f.syncs == 2 && f.pinWrites == 1 && f.savedPin == 654321 &&
          f.alertText == TR("Pairing code saved — reboot to apply"),
          "complete PIN did not sync and save exactly once");
    lv_event_send(pin, LV_EVENT_DEFOCUSED, nullptr);
    check(f.pinWrites == 1, "same PIN was saved twice");
    cleanup(screen, page, nullptr, pump);
  }

  scenario = "radio source of truth and callback replacement";
  {
    Fixture f;
    Screen screen(f.host()); f.screen = &screen;
    auto *page = body();
    screen.buildSettings(page, 220);
    auto *radio = switchFor(page, TR("Enable Bluetooth"));
    check(lv_obj_has_state(radio, LV_STATE_CHECKED), "requested radio was not checked");
    f.radioResult = false;
    lv_obj_clear_state(radio, LV_STATE_CHECKED);
    lv_event_send(radio, LV_EVENT_VALUE_CHANGED, nullptr);
    check(f.radioCalls == 1 && lv_obj_has_state(radio, LV_STATE_CHECKED) &&
          f.alertText == "Radio unavailable", "rejected radio change ignored authoritative snapshot");
    f.state.bleActive = false;
    f.state.bleRequested = false;
    screen.refresh();
    check(!lv_obj_has_state(radio, LV_STATE_CHECKED), "radio remained on after external shutdown");
    f.state.bleRequested = true; // Pager can retain request while start rejects.
    screen.refresh();
    check(lv_obj_has_state(radio, LV_STATE_CHECKED), "pending requested radio was displayed as off");
    auto *replacement = body(); f.replacement = replacement; f.replaceOnRadio = true;
    f.radioResult = true;
    lv_obj_clear_state(radio, LV_STATE_CHECKED);
    lv_event_send(radio, LV_EVENT_VALUE_CHANGED, nullptr);
    check(f.radioCalls == 2 && switchFor(replacement, TR("Enable Bluetooth")),
          "radio reentry discarded replacement settings page");
    cleanup(screen, page, replacement, pump);
  }

  scenario = "keyboard controls and capture cleanup";
  {
    Fixture f; f.state.keyboardSupported = true; f.state.keyboardMode = false;
    f.state.keyboardActive = true; f.state.state = Screen::State::Connected;
    Screen screen(f.host()); f.screen = &screen;
    auto *page = body(); screen.buildSettings(page, 220);
    check(findLabel(page, TR("Phone app")) && findLabel(page, TR("Keyboard")) &&
          findLabel(page, TR("Pairing code")), "keyboard-capable page omitted mode segments");
    click(button(page, TR("Keyboard")));
    check(f.modeCalls == 1 && f.state.keyboardMode &&
          findLabel(page, TR("Keyboard layout")) && findLabel(page, TR("Back key")),
          "keyboard mode did not expose layout and back key");
    auto *dropdown = findType(page, &lv_dropdown_class);
    check(dropdown != nullptr, "keyboard layout dropdown missing");
    lv_dropdown_set_selected(dropdown, 3);
    lv_event_send(dropdown, LV_EVENT_VALUE_CHANGED, nullptr);
    check(f.layoutCalls == 1 && f.state.layout == 3, "layout change lost selected value");
    click(button(page, "Esc"));
    check(f.captureOn == 1 && findLabel(page, TR("Press a key...")),
          "back-key capture did not start");
    screen.detach();
    check(f.captureOff == 1, "detaching settings left back-key capture active");
    lv_obj_del(page); pump(2);
  }

  scenario = "pairing identity, choice, and stale completion";
  {
    Fixture f; f.state.keyboardSupported = true; f.state.keyboardMode = true;
    f.state.keyboardActive = true;
    const auto alpha = device("Alpha Keys", 1, 0);
    const auto beta = device("Beta Keys", 1, 1); // same address, different address type
    f.state.found[0] = alpha; f.state.found[1] = beta; f.state.foundCount = 2;
    f.state.paired = true; f.state.pairedDevice = beta;
    f.state.state = Screen::State::Connected; // prior peer is already connected
    Screen screen(f.host()); f.screen = &screen;
    auto *page = body(); screen.buildPairing(page, 220);
    check(screen.pairingOpen() && f.scanOn == 1 &&
          findLabel(page, "Alpha Keys", true) && findLabel(page, "Beta Keys", true),
          "pairing did not show both found identities");
    auto *alphaRow = button(page, "Alpha Keys", true);
    f.state.found[0] = beta; f.state.found[1] = alpha; // scan order changes after render
    click(alphaRow);
    char chosenLabel[96];
    std::snprintf(chosenLabel, sizeof chosenLabel, TR("Pair with %s:"), "Alpha Keys");
    check(findLabel(page, chosenLabel) != nullptr,
          "choice used current scan slot instead of captured device");
    click(button(page, TR("Pair with a code")));
    check(f.pairCalls == 1 && sameDevice(f.pairedCommand, alpha) && f.pairWithCode,
          "pair command lost copied address or address type");
    screen.refresh();
    check(f.closePages == 0, "old connected peer closed new pairing before attempt progress");
    f.state.state = Screen::State::Connecting; ++f.state.generation;
    screen.refresh();
    f.state.state = Screen::State::Pairing;
    f.state.pairingCode = 123456; ++f.state.generation;
    screen.refresh();
    check(findLabel(page, "123 456") &&
          findLabel(page, TR("Type this code on the keyboard, then press Enter:")),
          "pairing refresh did not show formatted code and instructions");
    f.state.pairingCode = 0;
    f.state.state = Screen::State::Connected; ++f.state.generation;
    screen.refresh();
    check(f.closePages == 0, "connected different peer closed pairing page");
    f.state.pairedDevice = alpha; ++f.state.generation;
    screen.refresh();
    check(f.closePages == 1, "observed matching attempt did not close its pairing page");
    cleanup(screen, page, nullptr, pump);
    check(f.scanOff >= 1, "closing pairing did not stop scan");
  }

  scenario = "queue failure, rescan, and external deletion";
  {
    Fixture f; f.state.keyboardSupported = true; f.state.keyboardActive = true;
    f.state.found[0] = device("Queue Keys", 9, 1);
    f.state.foundCount = 1; f.pairResult = false;
    Screen screen(f.host()); f.screen = &screen;
    auto *page = body(); screen.buildPairing(page, 220);
    auto *oldRow = button(page, "Queue Keys", true);
    unsigned rowDeletes = 0, pageDeletes = 0;
    lv_obj_add_event_cb(oldRow, deleted, LV_EVENT_DELETE, &rowDeletes);
    lv_obj_add_event_cb(page, deleted, LV_EVENT_DELETE, &pageDeletes);
    click(oldRow);
    click(button(page, TR("Pair without a code")));
    check(f.pairCalls == 1 && f.closePages == 0 && f.alerts == 1,
          "rejected queue command falsely completed pairing");
    click(button(page, TR("Search again")));
    check(f.scanOn == 2 && rowDeletes == 1, "rescan failed to replace stale row");
    lv_obj_del(page);
    check(pageDeletes == 1 && !screen.pairingOpen() && f.scanOff >= 1,
          "external DELETE skipped later observer or retained pairing state");
    pump(2);
  }

  scenario = "forget tokens and page replacement";
  {
    Fixture f; f.state.keyboardSupported = true; f.state.keyboardMode = true;
    f.state.keyboardActive = true; f.state.paired = true;
    f.state.pairedDevice = device("Bound Keys", 3, 1);
    Screen screen(f.host()); f.screen = &screen;
    auto *page = body(); screen.buildSettings(page, 220);
    click(button(page, TR("Forget keyboard")));
    const uint32_t first = f.confirmationToken;
    check(first && sameDevice(f.confirmationDevice, f.state.pairedDevice),
          "forget confirmation did not capture paired identity");
    f.state.pairedDevice = device("Other Keys", 3, 0);
    check(!screen.confirmForget(first) && f.forgetCalls == 0,
          "stale confirmation forgot a changed peer");
    click(button(page, TR("Forget keyboard")));
    const uint32_t second = f.confirmationToken;
    check(second != first && !screen.confirmForget(first),
          "new confirmation accepted old token");
    check(screen.confirmForget(second) && f.forgetCalls == 1 &&
          sameDevice(f.forgottenCommand, device("Other Keys", 3, 0)),
          "current confirmation did not send exact identity");
    f.state.paired = true;
    click(button(page, TR("Forget keyboard")));
    const uint32_t third = f.confirmationToken;
    auto *replacement = body(); screen.buildSettings(replacement, 220);
    check(!screen.confirmForget(third) && f.forgetCalls == 1,
          "page replacement retained an old forget token");
    cleanup(screen, page, replacement, pump);
  }

  scenario = "reentrant host cleanup and stale controls";
  {
    Fixture f; f.state.keyboardSupported = true; f.state.keyboardMode = true;
    f.state.keyboardActive = true; f.state.state = Screen::State::Connected;
    Screen screen(f.host()); f.screen = &screen;
    auto *page = body(); screen.buildSettings(page, 220);
    auto *oldBack = button(page, "Esc");
    click(oldBack);
    auto *replacement = body(); f.replacement = replacement;
    f.replaceOnCaptureOff = true;
    screen.detach();
    check(switchFor(replacement, TR("Enable Bluetooth")) && f.captureOff == 1,
          "capture-off callback erased reentrant replacement");
    click(oldBack);
    check(f.captureOn == 1, "retired back-key control remained active");
    cleanup(screen, page, replacement, pump);
  }

  scenario = "reentrant PIN sync retires old page";
  {
    Fixture f;
    Screen screen(f.host()); f.screen = &screen;
    auto *page = body(); screen.buildSettings(page, 220);
    auto *oldPin = findType(page, &lv_textarea_class);
    check(oldPin != nullptr, "PIN field missing before sync reentry");
    auto *replacement = body(); f.replacement = replacement;
    f.syncValue = "321654"; f.replaceOnSync = true;
    lv_event_send(oldPin, LV_EVENT_DEFOCUSED, nullptr);
    check(f.pinWrites == 0 && findType(replacement, &lv_textarea_class),
          "retired PIN blur saved or erased replacement page");
    lv_event_send(oldPin, LV_EVENT_DEFOCUSED, nullptr);
    check(f.syncs == 1 && f.pinWrites == 0,
          "old PIN callback survived replacement");
    cleanup(screen, page, replacement, pump);
  }

  scenario = "scan cleanup reenters settings without losing replacement";
  {
    Fixture f; f.state.keyboardSupported = true;
    f.state.keyboardActive = true;
    f.state.found[0] = device("Transient Keys", 5, 0);
    f.state.foundCount = 1;
    Screen screen(f.host()); f.screen = &screen;
    auto *page = body(); screen.buildPairing(page, 220);
    auto *oldRow = button(page, "Transient Keys", true);
    auto *replacement = body(); f.replacement = replacement;
    f.replaceOnScanOff = true;
    screen.detach();
    check(f.scanOff == 1 && switchFor(replacement, TR("Enable Bluetooth")),
          "scan-off reentry erased the new settings page");
    click(oldRow);
    char retiredChoice[96];
    std::snprintf(retiredChoice, sizeof retiredChoice, TR("Pair with %s:"), "Transient Keys");
    check(!findLabel(page, retiredChoice),
          "retired pairing row still opened a choice");
    cleanup(screen, page, replacement, pump);
  }

  scenario = "settings DELETE and destructor retire callbacks";
  {
    Fixture f; f.state.keyboardSupported = true;
    f.state.keyboardMode = true; f.state.state = Screen::State::Connected;
    Screen screen(f.host()); f.screen = &screen;
    auto *page = body(); screen.buildSettings(page, 220);
    click(button(page, "Esc"));
    unsigned observers = 0;
    lv_obj_add_event_cb(page, deleted, LV_EVENT_DELETE, &observers);
    lv_obj_del(page);
    check(observers == 1 && f.captureOff == 1,
          "settings external DELETE skipped observer or left key capture active");
    auto *replacement = body(); screen.buildSettings(replacement, 220);
    screen.detach(); lv_obj_del(replacement); pump(2);
  }
  {
    Fixture f;
    auto *page = body();
    auto *screen = new Screen(f.host()); f.screen = screen;
    screen->buildSettings(page, 220);
    auto *retiredRadio = switchFor(page, TR("Enable Bluetooth"));
    delete screen; f.screen = nullptr;
    lv_obj_clear_state(retiredRadio, LV_STATE_CHECKED);
    lv_event_send(retiredRadio, LV_EVENT_VALUE_CHANGED, nullptr);
    check(f.radioCalls == 0, "destroyed owner left active radio callback");
    lv_obj_del(page); pump(2);
  }
}

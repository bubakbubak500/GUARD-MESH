// SPDX-License-Identifier: GPL-3.0-or-later
#include "BluetoothSettingsScreen.h"

#include "../i18n.h"
#include "../platform/UiPlatform.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <new>

namespace ui { namespace screens {
using namespace theme;
using namespace widgets;

namespace {
void copyText(char *out, size_t capacity, const char *text) {
  if (capacity) std::snprintf(out, capacity, "%s", text ? text : "");
}
enum class Tone : uint8_t { Sub, Ok, Warn };
struct Status { const char *text; Tone tone; };
const char *errorText(BluetoothSettingsScreen::Error error) {
  using Error = BluetoothSettingsScreen::Error;
  switch (error) {
    case Error::NotRunning:    return TR("Bluetooth is not running.");
    case Error::Connect:       return TR("Could not connect. Is the keyboard in pairing mode?");
    case Error::PairingCode:   return TR("Pairing failed. Type the code on the keyboard, then press Enter.");
    case Error::PairingNoCode: return TR("Pairing failed. Try pairing with a code.");
    case Error::NotKeyboard:   return TR("That device does not offer keyboard input.");
    case Error::NeedsPairing:  return TR("The keyboard needs to be paired again.");
    default:                   return "";
  }
}
Status keyboardStatus(const BluetoothSettingsScreen::Snapshot &page) {
  using State = BluetoothSettingsScreen::State;
  using Error = BluetoothSettingsScreen::Error;
  switch (page.state) {
    case State::Off:        return {TR("Turn Bluetooth on to use a keyboard."), Tone::Warn};
    case State::Idle:       return {TR("Put the keyboard in pairing mode, then tap Pair a keyboard."), Tone::Sub};
    case State::Waiting:    return page.error == Error::NeedsPairing
      ? Status{errorText(Error::NeedsPairing), Tone::Warn}
      : Status{TR("Not connected. Press a key on the keyboard to wake it."), Tone::Sub};
    case State::Scanning:   return {TR("Looking for keyboards..."), Tone::Sub};
    case State::Connecting: return {TR("Connecting..."), Tone::Sub};
    case State::Pairing:    return {TR("Pairing..."), Tone::Sub};
    case State::Connected:  return {TR("Connected. Start typing."), Tone::Ok};
    case State::Failed:     return {errorText(page.error), Tone::Warn};
  }
  return {"", Tone::Sub};
}
uint32_t toneColor(Tone tone) {
  if (tone == Tone::Ok) return colors().COLOR_STATUS_OK_TEXT;
  if (tone == Tone::Warn) return colors().COLOR_STATUS_WARN_TEXT;
  return colors().COLOR_SUB;
}
bool setVisible(lv_obj_t *object, bool visible) {
  if (!object || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN) != visible) return false;
  if (visible) lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  return true;
}
lv_obj_t *column(lv_obj_t *parent, lv_coord_t gap) {
  auto *object = lv_obj_create(parent);
  lv_obj_remove_style_all(object);
  lv_obj_set_width(object, lv_pct(100));
  lv_obj_set_height(object, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(object, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(object, gap, LV_PART_MAIN);
  lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
  return object;
}
lv_obj_t *line(lv_obj_t *parent, const char *caption) {
  auto *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(row, SC(36), LV_PART_MAIN);
  lv_obj_set_style_pad_left(row, 2, LV_PART_MAIN);
  lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  auto *label = lv_label_create(row);
  useChainedFont(label);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(label, caption);
  lv_obj_set_style_text_color(label, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_flex_grow(label, 1);
  return row;
}
lv_obj_t *note(lv_obj_t *parent, const char *text) {
  auto *label = lv_label_create(parent);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, lv_pct(100));
  lv_obj_set_style_pad_left(label, 2, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_label_set_text(label, text);
  return label;
}
lv_obj_t *button(lv_obj_t *parent, const char *caption, lv_event_cb_t callback, void *user) {
  auto *result = lv_btn_create(parent);
  lv_obj_set_width(result, lv_pct(100));
  lv_obj_set_height(result, LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(result, SC(34), LV_PART_MAIN);
  styleButton(result);
  lv_obj_set_style_pad_hor(result, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(result, 6, LV_PART_MAIN);
  lv_obj_set_flex_flow(result, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(result, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  if (callback) lv_obj_add_event_cb(result, callback, LV_EVENT_CLICKED, user);
  auto *label = lv_label_create(result);
  useChainedFont(label);
  lv_label_set_text(label, caption);
  lv_obj_set_width(label, lv_pct(100));
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  return result;
}
void stylePrimary(lv_obj_t *object, bool primary) {
  if (!object) return;
  styleButton(object);
  uint32_t text = colors().COLOR_TEXT;
  if (primary) {
    lv_obj_set_style_bg_color(object, lv_color_hex(colors().COLOR_STATUS_OK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(object, lv_color_hex(colors().COLOR_STATUS_OK_PRESSED),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_opa(object, LV_OPA_0, LV_PART_MAIN);
    text = colors().COLOR_ON_STATUS_OK;
  }
  lv_obj_set_style_text_color(object, lv_color_hex(text), LV_PART_MAIN);
  if (auto *label = lv_obj_get_child(object, 0))
    lv_obj_set_style_text_color(label, lv_color_hex(text), LV_PART_MAIN);
}
void styleSegment(lv_obj_t *object, bool active) {
  if (!object) return;
  lv_obj_set_style_text_color(object,
      lv_color_hex(active ? colors().COLOR_ON_ACCENT : colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_bg_color(object, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
  if (active) {
    lv_obj_set_style_bg_opa(object, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_opa(object, LV_OPA_0, LV_PART_MAIN);
  } else {
    lv_obj_set_style_bg_opa(object, LV_OPA_10, LV_PART_MAIN);
    lv_obj_set_style_border_color(object, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_border_width(object, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(object, LV_OPA_40, LV_PART_MAIN);
  }
}
} // namespace

BluetoothSettingsScreen::BluetoothSettingsScreen(Host host) : _host(host) {}
BluetoothSettingsScreen::~BluetoothSettingsScreen() {
  _destroying = true;
  detach();
}
bool BluetoothSettingsScreen::sameIdentity(const Device &a, const Device &b) {
  return a.addrType == b.addrType && std::memcmp(a.addr, b.addr, sizeof a.addr) == 0;
}
BluetoothSettingsScreen::SettingsSignature BluetoothSettingsScreen::signature(const Snapshot &p) {
  SettingsSignature s;
  s.generation = p.generation; s.pin = p.pin;
  s.bleCapable = p.bleCapable; s.wifiOn = p.wifiOn;
  s.radioControllable = p.radioControllable;
  s.bleActive = p.bleActive; s.bleRequested = p.bleRequested;
  s.keyboardSupported = p.keyboardSupported; s.keyboardMode = p.keyboardMode;
  s.keyboardActive = p.keyboardActive; s.paired = p.paired;
  s.capturingKey = p.capturingKey; s.state = p.state; s.error = p.error;
  s.layout = p.layout; s.backKey = p.backKey; s.pairedDevice = p.pairedDevice;
  return s;
}
bool BluetoothSettingsScreen::sameSignature(const SettingsSignature &a, const SettingsSignature &b) {
  return a.generation == b.generation && a.pin == b.pin &&
    a.bleCapable == b.bleCapable && a.wifiOn == b.wifiOn &&
    a.radioControllable == b.radioControllable &&
    a.bleActive == b.bleActive && a.bleRequested == b.bleRequested &&
    a.keyboardSupported == b.keyboardSupported && a.keyboardMode == b.keyboardMode &&
    a.keyboardActive == b.keyboardActive && a.paired == b.paired &&
    a.capturingKey == b.capturingKey && a.state == b.state && a.error == b.error &&
    a.layout == b.layout && a.backKey == b.backKey &&
    (!a.paired || (sameIdentity(a.pairedDevice, b.pairedDevice) &&
                   std::strcmp(a.pairedDevice.name, b.pairedDevice.name) == 0));
}
bool BluetoothSettingsScreen::still(uint32_t generation) const {
  return !_destroying && _generation == generation;
}
bool BluetoothSettingsScreen::snapshot(Snapshot &out) {
  out = Snapshot{};
  if (!_host.readSnapshot || !_host.readSnapshot(_host.context, out)) return false;
  if (out.foundCount > FoundCapacity) out.foundCount = FoundCapacity;
  out.pairedDevice.name[sizeof out.pairedDevice.name - 1] = 0;
  for (auto &device : out.found) device.name[sizeof device.name - 1] = 0;
  return true;
}
void BluetoothSettingsScreen::alert(const char *message, unsigned duration) {
  if (_host.alert) _host.alert(_host.context, message, duration);
}
void BluetoothSettingsScreen::navDirty() {
  if (_host.navDirty) _host.navDirty(_host.context);
}
void BluetoothSettingsScreen::releaseRows() {
  if (_rows) {
    for (unsigned i = 0; i < _rowCount; ++i) _rows[i].~RowContext();
    platform::release(_rows);
  }
  _rows = nullptr;
  _rowCount = 0;
}
void BluetoothSettingsScreen::detachTree(lv_obj_t *object, bool deletingRoot) {
  if (!object) return;
  const uint32_t count = lv_obj_get_child_cnt(object);
  for (uint32_t i = 0; i < count; ++i) detachTree(lv_obj_get_child(object, i));
  if (deletingRoot) return; // Keep the executing DELETE descriptor for other observers.
  for (auto callback : {settingsDeleted, pairingDeleted, radioChanged, modeClicked,
                        pinBlurred, layoutChanged, backKeyClicked, openPairingClicked,
                        forgetClicked, pairClicked, choiceBackClicked,
                        rescanClicked})
    while (lv_obj_remove_event_cb_with_user_data(object, callback, this)) {}
  for (unsigned i = 0; i < _rowCount; ++i)
    while (lv_obj_remove_event_cb_with_user_data(object, deviceClicked, &_rows[i])) {}
}
void BluetoothSettingsScreen::clearSettings(bool deleting) {
  ++_generation;
  _hasForgetRequest = false;
  _haveSettingsSignature = false;
  if (!deleting) detachTree(_settingsBody.get());
  _settingsBody.set(nullptr); _radio.set(nullptr); _pin.set(nullptr);
  _phoneBox.set(nullptr); _keyboardBox.set(nullptr); _modeLine.set(nullptr);
  for (auto &segment : _segment) segment.set(nullptr);
  _name.set(nullptr); _icon.set(nullptr); _status.set(nullptr);
  _forget.set(nullptr); _pairButton.set(nullptr); _tip.set(nullptr); _backButton.set(nullptr);
  _width = 0;
  if (_capturingOwned) {
    _capturingOwned = false;
    if (_host.captureBackKey) _host.captureBackKey(_host.context, false);
  }
}
void BluetoothSettingsScreen::clearPairing(bool deleting) {
  ++_generation;
  ++_listGeneration;
  if (!deleting) detachTree(_pairBody.get());
  const bool wasOpen = _pairBody.get() != nullptr;
  _pairBody.set(nullptr); _pairStatus.set(nullptr); _pairCode.set(nullptr);
  _pairList.set(nullptr); _choice.set(nullptr); _choiceLabel.set(nullptr);
  _plainPair.set(nullptr); _codedPair.set(nullptr);
  releaseRows();
  _hasChoice = false; _pairStarted = false; _pairSawProgress = false;
  _seenKeyboardGeneration = UINT32_MAX;
  if (wasOpen && !deleting && _host.scanKeyboards)
    _host.scanKeyboards(_host.context, false);
}
void BluetoothSettingsScreen::detach() {
  const uint32_t expected = _generation + 1;
  clearPairing(false);
  if (_generation != expected) return; // Host scan-off opened a replacement.
  clearSettings(false);
}
void BluetoothSettingsScreen::settingsDeleted(lv_event_t *event) {
  auto *self = static_cast<BluetoothSettingsScreen *>(lv_event_get_user_data(event));
  if (!self || self->_destroying) return;
  self->detachTree(lv_event_get_current_target(event), true);
  self->clearSettings(true);
}
void BluetoothSettingsScreen::pairingDeleted(lv_event_t *event) {
  auto *self = static_cast<BluetoothSettingsScreen *>(lv_event_get_user_data(event));
  if (!self || self->_destroying) return;
  self->detachTree(lv_event_get_current_target(event), true);
  self->clearPairing(true);
  if (self->_host.scanKeyboards) self->_host.scanKeyboards(self->_host.context, false);
}

void BluetoothSettingsScreen::buildSettings(lv_obj_t *body, lv_coord_t width) {
  if (_destroying) return;
  const bool reuse = body &&
      (body == _settingsBody.get() || body == _pairBody.get());
  uint32_t expected = _generation + 1;
  clearPairing(false);
  if (!still(expected)) return;
  expected = _generation + 1;
  clearSettings(false);
  if (!body || !still(expected) || !lv_obj_is_valid(body)) return;
  // A dedicated settings content body may be rebuilt in place. Its old rows
  // have been detached, so remove only that body's former contents.
  if (reuse) {
    lv_obj_clean(body);
    if (!still(expected) || !lv_obj_is_valid(body)) return;
  }
  if (!_settingsBody.set(body)) return;
  lv_obj_add_event_cb(body, settingsDeleted, LV_EVENT_DELETE, this);
  _width = width;
  const uint32_t generation = _generation;
  Snapshot page;
  if (!snapshot(page) || !still(generation)) return;
  if (!page.bleCapable) {
    _settingsSignature = signature(page);
    _haveSettingsSignature = true;
    note(body, TR("Bluetooth pairing isn't available on this device.\n\n"
                  "The radio chip's factory firmware doesn't support it.\n\n"
                  "Pair the phone app over Wi-Fi instead: connect this device to your "
                  "network, then add it in the app by its IP address (TCP, port 5000) — "
                  "shown under Settings > Network. USB works too."));
    return;
  }
  _savedPin = page.pin;
  _requested = page.bleRequested || page.bleActive;
  _building = true;
  auto *pageColumn = column(body, SC(6));

  // Boards with keyboard support use the segmented Phone app / Keyboard page.
  // Without it the same owner renders the phone-only PIN/radio controls.
  if (!page.keyboardSupported) {
    auto *lineLabel = note(pageColumn, "");
    _modeLine.set(lineLabel);
  }
  auto addPin = [&](lv_obj_t *parent) -> bool {
    auto *pinRow = line(parent, TR("Pairing code"));
    auto *field = lv_textarea_create(pinRow);
    _pin.set(field);
    lv_obj_set_size(field, SC(96), SC(32));
    lv_textarea_set_one_line(field, true);
    lv_textarea_set_accepted_chars(field, "0123456789");
    lv_textarea_set_max_length(field, 6);
    char placeholder[12];
    if (page.pin >= 1 && page.pin <= 999999)
      std::snprintf(placeholder, sizeof placeholder, "%06lu", (unsigned long)page.pin);
    else copyText(placeholder, sizeof placeholder, "------");
    taSetPlaceholder(field, placeholder);
    lv_textarea_set_text(field, "");
    lv_obj_add_event_cb(field, pinBlurred, LV_EVENT_DEFOCUSED, this);
    if (_host.attachPinTextArea) _host.attachPinTextArea(_host.context, field);
    if (!still(generation) || _pin.get() != field) return false;
    note(parent, TR("Type a new 6-digit code \xC2\xB7 applies after a reboot"));
    return true;
  };
  auto addRadio = [&](lv_obj_t *parent) {
    auto *radioRow = line(parent, TR("Enable Bluetooth"));
    auto *radio = lv_switch_create(radioRow);
    _radio.set(radio);
    if (_requested) lv_obj_add_state(radio, LV_STATE_CHECKED);
    lv_obj_add_event_cb(radio, radioChanged, LV_EVENT_VALUE_CHANGED, this);
  };

  if (!page.keyboardSupported) {
    if (!addPin(pageColumn)) return;
    if (page.radioControllable) addRadio(pageColumn);
    _building = false;
    updateSettings(page);
    return;
  }

  if (page.radioControllable) addRadio(pageColumn);
  auto *useLabel = lv_label_create(pageColumn);
  useChainedFont(useLabel);
  lv_label_set_text(useLabel, TR("Use Bluetooth for"));
  lv_obj_set_style_text_color(useLabel, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_pad_left(useLabel, 2, LV_PART_MAIN);
  auto *segments = lv_obj_create(pageColumn);
  lv_obj_remove_style_all(segments);
  lv_obj_set_width(segments, lv_pct(100));
  lv_obj_set_height(segments, SC(38));
  lv_obj_set_flex_flow(segments, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(segments, 6, LV_PART_MAIN);
  lv_obj_clear_flag(segments, LV_OBJ_FLAG_SCROLLABLE);
  const char *segmentText[2] = {TR("Phone app"), TR("Keyboard")};
  for (unsigned i = 0; i < 2; ++i) {
    auto *segment = lv_btn_create(segments);
    _segment[i].set(segment);
    lv_obj_set_height(segment, lv_pct(100));
    lv_obj_set_flex_grow(segment, 1);
    styleButton(segment);
    lv_obj_set_style_pad_hor(segment, 3, LV_PART_MAIN);
    lv_obj_add_event_cb(segment, modeClicked, LV_EVENT_CLICKED, this);
    auto *label = lv_label_create(segment);
    useChainedFont(label);
    lv_label_set_text(label, segmentText[i]);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, (width - SC(26)) / 2);
    lv_obj_center(label);
  }
  auto *phone = column(pageColumn, SC(4)); _phoneBox.set(phone);
  auto *lineLabel = note(phone, ""); _modeLine.set(lineLabel);
  if (!addPin(phone)) return;

  auto *keyboard = column(pageColumn, SC(8)); _keyboardBox.set(keyboard);
  auto *card = column(keyboard, SC(4));
  lv_obj_set_style_bg_color(card, lv_color_hex(colors().COLOR_FIELD), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 8, LV_PART_MAIN);
  auto *head = line(card, LV_SYMBOL_KEYBOARD);
  _icon.set(lv_obj_get_child(head, 0));
  auto *name = lv_label_create(head); _name.set(name);
  lv_obj_set_flex_grow(name, 1);
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(name, &font14(), LV_PART_MAIN);
  auto *status = note(card, ""); _status.set(status);
  lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
  auto *pairButton = button(keyboard, TR("Pair a keyboard"), openPairingClicked, this);
  _pairButton.set(pairButton);
  auto *forget = button(keyboard, TR("Forget keyboard"), forgetClicked, this);
  _forget.set(forget);
  lv_obj_set_style_border_color(forget, lv_color_hex(colors().COLOR_STATUS_DANGER), LV_PART_MAIN);
  auto *layoutRow = line(keyboard, TR("Keyboard layout"));
  auto *dropdown = lv_dropdown_create(layoutRow);
  lv_obj_set_size(dropdown, SC(140), SC(34));
  lv_dropdown_set_options(dropdown, TR("US (QWERTY)\nUK (QWERTY)\nGerman (QWERTZ)\nFrench (AZERTY)\nBelgian (AZERTY)"));
  if (_host.styleLayoutDropdown) _host.styleLayoutDropdown(_host.context, dropdown);
  if (!still(generation)) return;
  lv_dropdown_set_selected(dropdown, page.layout < 5 ? page.layout : 0);
  lv_obj_add_event_cb(dropdown, layoutChanged, LV_EVENT_VALUE_CHANGED, this);
  note(keyboard, TR("Pick the layout printed on the keys, or letters and symbols come out wrong."));
  auto *backRow = line(keyboard, TR("Back key"));
  auto *back = lv_btn_create(backRow); _backButton.set(back);
  lv_obj_set_size(back, SC(140), SC(34));
  styleButton(back);
  lv_obj_add_event_cb(back, backKeyClicked, LV_EVENT_CLICKED, this);
  auto *backLabel = lv_label_create(back);
  useChainedFont(backLabel);
  lv_label_set_long_mode(backLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_width(backLabel, lv_pct(100));
  lv_obj_set_style_text_align(backLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_center(backLabel);
  note(keyboard, TR("Esc always goes back. If your keyboard's Esc key types a character instead, "
                    "tap the button, then press that key. Press Esc to undo."));
  _tip.set(note(keyboard, TR("Type into any text field. Arrow keys and Tab move around, "
                              "Enter opens, Esc goes back.")));
  if (!still(generation)) return;
  _building = false;
  _seenKeyboardGeneration = UINT32_MAX;
  updateSettings(page);
  navDirty();
}

void BluetoothSettingsScreen::buildPairing(lv_obj_t *body, lv_coord_t width) {
  if (_destroying) return;
  const bool reuse = body &&
      (body == _settingsBody.get() || body == _pairBody.get());
  uint32_t expected = _generation + 1;
  clearSettings(false);
  if (!still(expected)) return;
  expected = _generation + 1;
  clearPairing(false);
  if (!body || !still(expected) || !lv_obj_is_valid(body)) return;
  if (reuse) {
    lv_obj_clean(body);
    if (!still(expected) || !lv_obj_is_valid(body)) return;
  }
  if (!_pairBody.set(body)) return;
  lv_obj_add_event_cb(body, pairingDeleted, LV_EVENT_DELETE, this);
  _width = width;
  const uint32_t generation = _generation;
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, 6, LV_PART_MAIN);
  note(body, TR("Put the keyboard in pairing mode; it shows up below. "
                "Only Bluetooth Low Energy keyboards work."));
  auto *status = lv_label_create(body); _pairStatus.set(status);
  lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(status, lv_pct(100));
  lv_obj_set_style_text_font(status, &font14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(status, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  auto *code = lv_label_create(body); _pairCode.set(code);
  lv_obj_set_width(code, lv_pct(100));
  lv_obj_set_style_text_font(code, &lv_font_montserrat_28, LV_PART_MAIN);
  lv_obj_set_style_text_color(code, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_set_style_text_align(code, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_add_flag(code, LV_OBJ_FLAG_HIDDEN);
  auto *list = column(body, 4); _pairList.set(list);
  auto *choice = column(body, 6); _choice.set(choice);
  lv_obj_add_flag(choice, LV_OBJ_FLAG_HIDDEN);
  auto *choiceLabel = note(choice, ""); _choiceLabel.set(choiceLabel);
  auto *plain = button(choice, TR("Pair without a code"), pairClicked, this);
  _plainPair.set(plain);
  stylePrimary(plain, true);
  _codedPair.set(button(choice, TR("Pair with a code"), pairClicked, this));
  note(choice, TR("With a code, one appears here: type it on the keyboard and press Enter. "
                  "Use it when pairing without a code fails."));
  button(choice, TR("Back"), choiceBackClicked, this);
  button(body, TR("Search again"), rescanClicked, this);
  _hasChoice = false;
  _pairStarted = false;
  _seenKeyboardGeneration = UINT32_MAX;
  Snapshot page;
  if (snapshot(page) && still(generation)) {
    updatePairing(page, true);
    if (still(generation)) _seenKeyboardGeneration = page.generation;
  }
  if (!still(generation)) return;
  if (_host.scanKeyboards) _host.scanKeyboards(_host.context, true);
  if (still(generation)) navDirty();
}

void BluetoothSettingsScreen::rebuildDevices(const Snapshot &page) {
  auto *list = _pairList.get();
  if (!list) return;
  const uint32_t generation = _generation;
  if (_host.navDetachBeforeTreeMutation)
    _host.navDetachBeforeTreeMutation(_host.context);
  if (!still(generation) || _pairList.get() != list) return;
  // The old contexts remain valid until their rows and callbacks are gone.
  lv_obj_clean(list);
  if (!still(generation) || _pairList.get() != list) return;
  releaseRows();
  ++_listGeneration;
  if (page.foundCount) {
    _rows = static_cast<RowContext *>(
        platform::allocate(sizeof(RowContext) * FoundCapacity, true));
    if (!_rows)
      _rows = static_cast<RowContext *>(
          platform::allocate(sizeof(RowContext) * FoundCapacity, false));
    if (!_rows) {
      note(list, TR("Not enough memory"));
      navDirty();
      return;
    }
  }
  for (unsigned i = 0; i < page.foundCount; ++i) {
    auto &context = *new (&_rows[_rowCount++]) RowContext{};
    context.owner = this;
    context.generation = _listGeneration;
    context.device = page.found[i];
    context.device.name[sizeof context.device.name - 1] = 0;
    char text[80];
    std::snprintf(text, sizeof text, "%s%s  (%d dBm)",
                  context.device.keyboard ? LV_SYMBOL_KEYBOARD "  " : "",
                  context.device.name[0] ? context.device.name : TR("Unnamed device"),
                  (int)context.device.rssi);
    auto *row = button(list, text, deviceClicked, &context);
    if (auto *label = lv_obj_get_child(row, 0))
      lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
  }
  navDirty();
}

void BluetoothSettingsScreen::showChoice(bool show) {
  setVisible(_choice.get(), show);
  setVisible(_pairList.get(), !show);
  navDirty();
}

void BluetoothSettingsScreen::updatePairing(const Snapshot &page, bool rebuild) {
  if (!_pairBody.get() || !_pairStatus.get()) return;
  const uint32_t generation = _generation;
  const Status status = keyboardStatus(page);
  const char *text = (page.state == State::Pairing && page.pairingCode)
    ? TR("Type this code on the keyboard, then press Enter:") : status.text;
  lv_label_set_text(_pairStatus.get(), text);
  if (_pairCode.get()) {
    if (page.pairingCode) {
      char formatted[12];
      std::snprintf(formatted, sizeof formatted, "%03lu %03lu",
                    (unsigned long)(page.pairingCode / 1000),
                    (unsigned long)(page.pairingCode % 1000));
      lv_label_set_text(_pairCode.get(), formatted);
      lv_obj_clear_flag(_pairCode.get(), LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(_pairCode.get(), LV_OBJ_FLAG_HIDDEN);
  }
  if (!_hasChoice) {
    const bool busy = page.state == State::Connecting || page.state == State::Pairing;
    setVisible(_pairList.get(), !busy);
    if (!busy && rebuild) rebuildDevices(page);
    if (!still(generation)) return;
  }
  if (_pairStarted && page.generation != _pairBaselineGeneration &&
      (page.state == State::Connecting || page.state == State::Pairing))
    _pairSawProgress = true;
  // A preexisting link can still read Connected while the new pair command
  // waits on its worker. Require observed progress and the chosen identity.
  // If an attempt starts and completes between UI polls, there is no request
  // identifier to prove completion here; leave the page open for the user.
  if (_pairStarted && _pairSawProgress && page.state == State::Connected &&
      page.paired && sameIdentity(page.pairedDevice, _choiceDevice)) {
    _pairStarted = false;
    if (_host.closePairingPage) _host.closePairingPage(_host.context);
  } else if (_pairStarted && page.generation != _pairBaselineGeneration &&
             page.state == State::Failed) {
    _pairStarted = false;
  }
}

void BluetoothSettingsScreen::updateSettings(const Snapshot &page) {
  if (!_settingsBody.get() || !page.bleCapable) return;
  const uint32_t generation = _generation;
  // Record before Host calls: a reentrant refresh cannot repaint this page
  // repeatedly while the same state remains authoritative.
  _settingsSignature = signature(page);
  _haveSettingsSignature = true;
  _requested = page.bleRequested || page.bleActive;
  _savedPin = page.pin;
  if (_radio.get()) {
    if (_requested) lv_obj_add_state(_radio.get(), LV_STATE_CHECKED);
    else lv_obj_clear_state(_radio.get(), LV_STATE_CHECKED);
  }
  if (_modeLine.get()) {
    const char *text;
    if (page.bleActive)
      text = page.wifiOn ? "Mode: BLE on (+ Wi-Fi)" : "Mode: BLE on";
    else if (page.bleRequested)
      text = TR("Mode: BLE starting / low memory");
    else
      text = page.wifiOn ? "Mode: BLE off (Wi-Fi on)" : "Mode: BLE off";
    lv_label_set_text(_modeLine.get(), text);
  }
  if (!page.keyboardSupported) return;
  bool moved = false;
  moved |= setVisible(_phoneBox.get(), !page.keyboardMode);
  moved |= setVisible(_keyboardBox.get(), page.keyboardMode);
  for (unsigned i = 0; i < 2; ++i) styleSegment(_segment[i].get(), (i == 1) == page.keyboardMode);
  if (_name.get())
    lv_label_set_text(_name.get(), page.paired
      ? (page.pairedDevice.name[0] ? page.pairedDevice.name : TR("Keyboard"))
      : TR("No keyboard paired"));
  if (_icon.get())
    lv_obj_set_style_text_color(_icon.get(), lv_color_hex(
        page.state == State::Connected ? colors().COLOR_STATUS_OK_TEXT : colors().COLOR_SUB),
        LV_PART_MAIN);
  if (_status.get()) {
    const Status status = keyboardStatus(page);
    lv_label_set_text(_status.get(), status.text);
    lv_obj_set_style_text_color(_status.get(), lv_color_hex(toneColor(status.tone)), LV_PART_MAIN);
  }
  moved |= setVisible(_forget.get(), page.paired);
  if (_pairButton.get()) {
    const bool needsPairing = !page.paired || page.error == Error::NeedsPairing;
    const char *caption = !page.paired ? TR("Pair a keyboard")
      : (needsPairing ? TR("Pair it again") : TR("Pair another keyboard"));
    if (auto *label = lv_obj_get_child(_pairButton.get(), 0)) lv_label_set_text(label, caption);
    stylePrimary(_pairButton.get(), needsPairing);
    if (page.keyboardActive) lv_obj_clear_state(_pairButton.get(), LV_STATE_DISABLED);
    else lv_obj_add_state(_pairButton.get(), LV_STATE_DISABLED);
  }
  moved |= setVisible(_tip.get(), page.state == State::Connected);
  if (_backButton.get()) {
    char caption[32] = "";
    if (page.capturingKey) copyText(caption, sizeof caption, TR("Press a key..."));
    else if (!page.backKey) copyText(caption, sizeof caption, "Esc");
    else {
      char key[16] = "";
      if (_host.formatBackKey) _host.formatBackKey(_host.context, page.backKey, key, sizeof key);
      if (!still(generation) || !_backButton.get()) return;
      std::snprintf(caption, sizeof caption, "Esc + %s", key);
    }
    if (auto *label = lv_obj_get_child(_backButton.get(), 0)) lv_label_set_text(label, caption);
    if (page.state == State::Connected) lv_obj_clear_state(_backButton.get(), LV_STATE_DISABLED);
    else lv_obj_add_state(_backButton.get(), LV_STATE_DISABLED);
  }
  if (!page.capturingKey) _capturingOwned = false;
  if (moved && !_building && _host.refitPage) {
    _host.refitPage(_host.context);
    if (!still(generation)) return;
    navDirty();
  }
}

void BluetoothSettingsScreen::refresh() {
  if (!_settingsBody.get() && !_pairBody.get()) return;
  const uint32_t generation = _generation;
  Snapshot page;
  if (!snapshot(page) || !still(generation)) return;
  if (_settingsBody.get()) {
    const SettingsSignature current = signature(page);
    if (!_haveSettingsSignature || !sameSignature(current, _settingsSignature))
      updateSettings(page);
  }
  if (!still(generation)) return;
  if (_pairBody.get() && page.generation != _seenKeyboardGeneration) {
    _seenKeyboardGeneration = page.generation;
    updatePairing(page, true);
  }
}

void BluetoothSettingsScreen::radioChanged(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  auto *self = static_cast<BluetoothSettingsScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying)
    self->onRadio(lv_obj_has_state(lv_event_get_current_target(event), LV_STATE_CHECKED));
}
void BluetoothSettingsScreen::onRadio(bool enabled) {
  if (!_settingsBody.get() || !_radio.get() || enabled == _requested) return;
  const uint32_t generation = _generation;
  const bool accepted = _host.requestRadio && _host.requestRadio(_host.context, enabled);
  if (!still(generation) || !_radio.get()) return;
  Snapshot page;
  const bool havePage = snapshot(page);
  if (!still(generation)) return;
  if (havePage) updateSettings(page);
  else if (_radio.get()) {
    if (_requested) lv_obj_add_state(_radio.get(), LV_STATE_CHECKED);
    else lv_obj_clear_state(_radio.get(), LV_STATE_CHECKED);
  }
  if (!still(generation)) return;
  if (!accepted) {
    const char *message = _host.radioFailureText
      ? _host.radioFailureText(_host.context) : TR("Bluetooth is not running.");
    if (still(generation)) alert(message, 2600);
  } else alert(enabled ? TR("Bluetooth on") : TR("Bluetooth off"), 1000);
}
void BluetoothSettingsScreen::modeClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *self = static_cast<BluetoothSettingsScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying)
    self->onMode(lv_event_get_current_target(event) == self->_segment[1].get());
}
void BluetoothSettingsScreen::onMode(bool keyboard) {
  if (!_settingsBody.get()) return;
  const uint32_t generation = _generation;
  Snapshot before;
  if (!snapshot(before) || !still(generation) || !before.keyboardSupported ||
      keyboard == before.keyboardMode) return;
  const bool changed = _host.setKeyboardMode &&
      _host.setKeyboardMode(_host.context, keyboard);
  if (!still(generation)) return;
  refresh();
  if (!still(generation)) return;
  if (changed) alert(keyboard ? TR("Bluetooth now serves a keyboard")
                              : TR("Bluetooth now serves the phone app"), 1600);
}
void BluetoothSettingsScreen::pinBlurred(lv_event_t *event) {
  auto *self = static_cast<BluetoothSettingsScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying) self->onPinBlur(event);
}
void BluetoothSettingsScreen::onPinBlur(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_DEFOCUSED || !_pin.get()) return;
  const uint32_t generation = _generation;
  if (_host.blurFromDelete && _host.blurFromDelete(_host.context, event)) return;
  if (!still(generation) || !_pin.get()) return;
  if (_host.syncKeyboard) _host.syncKeyboard(_host.context);
  if (!still(generation) || !_pin.get()) return;
  const char *text = lv_textarea_get_text(_pin.get());
  if (!text || std::strlen(text) != 6) return;
  uint32_t pin = 0;
  for (unsigned i = 0; i < 6; ++i) {
    if (text[i] < '0' || text[i] > '9') return;
    pin = pin * 10 + (unsigned)(text[i] - '0');
  }
  if (pin == _savedPin) return;
  const bool saved = _host.savePin && _host.savePin(_host.context, pin);
  if (!still(generation) || !_pin.get()) return;
  if (saved) {
    _savedPin = pin;
    alert(TR("Pairing code saved — reboot to apply"), 2000);
  }
}
void BluetoothSettingsScreen::layoutChanged(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  auto *self = static_cast<BluetoothSettingsScreen *>(lv_event_get_user_data(event));
  if (!self || self->_destroying) return;
  const unsigned selected = lv_dropdown_get_selected(lv_event_get_current_target(event));
  if (selected < 5) self->onLayout((uint8_t)selected);
}
void BluetoothSettingsScreen::onLayout(uint8_t layout) {
  const uint32_t generation = _generation;
  if (_host.setLayout) _host.setLayout(_host.context, layout);
  if (still(generation)) refresh();
}
void BluetoothSettingsScreen::backKeyClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *self = static_cast<BluetoothSettingsScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying) self->onBackKey();
}
void BluetoothSettingsScreen::onBackKey() {
  if (!_backButton.get()) return;
  const uint32_t generation = _generation;
  Snapshot page;
  if (!snapshot(page) || !still(generation)) return;
  const bool next = !page.capturingKey;
  _capturingOwned = next;
  if (_host.captureBackKey) _host.captureBackKey(_host.context, next);
  if (still(generation)) refresh();
}
void BluetoothSettingsScreen::openPairingClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *self = static_cast<BluetoothSettingsScreen *>(lv_event_get_user_data(event));
  if (!self || self->_destroying || !self->_settingsBody.get()) return;
  const uint32_t generation = self->_generation;
  Snapshot page;
  if (!self->snapshot(page) || !self->still(generation)) return;
  if (!page.keyboardActive) {
    self->alert(TR("Turn Bluetooth on to use a keyboard."), 1600);
    return;
  }
  if (self->_host.openPairingPage) self->_host.openPairingPage(self->_host.context);
}

void BluetoothSettingsScreen::forgetClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *self = static_cast<BluetoothSettingsScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying) self->onForget();
}
void BluetoothSettingsScreen::onForget() {
  if (!_settingsBody.get()) return;
  const uint32_t generation = _generation;
  Snapshot page;
  if (!snapshot(page) || !still(generation) || !page.paired) return;
  _forgetDevice = page.pairedDevice;
  _forgetGeneration = generation;
  _hasForgetRequest = true;
  do { ++_nextForgetToken; } while (!_nextForgetToken);
  _forgetToken = _nextForgetToken;
  if (_host.requestForgetConfirmation)
    _host.requestForgetConfirmation(_host.context, _forgetToken, _forgetDevice);
}
bool BluetoothSettingsScreen::confirmForget(uint32_t token) {
  if (_destroying || !_settingsBody.get() || !_hasForgetRequest ||
      token != _forgetToken || _forgetGeneration != _generation) return false;
  const uint32_t generation = _generation;
  _hasForgetRequest = false;
  Snapshot page;
  if (!snapshot(page) || !still(generation) || !page.paired ||
      !sameIdentity(page.pairedDevice, _forgetDevice)) return false;
  const Device expected = _forgetDevice;
  const bool accepted = _host.forgetKeyboardIfMatches &&
      _host.forgetKeyboardIfMatches(_host.context, expected);
  if (!still(generation)) return accepted;
  if (accepted) {
    Snapshot after;
    if (snapshot(after) && still(generation) && !after.paired)
      alert(TR("Keyboard forgotten"), 1200);
    if (still(generation)) refresh();
  }
  return accepted;
}

void BluetoothSettingsScreen::deviceClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *context = static_cast<RowContext *>(lv_event_get_user_data(event));
  if (!context || !context->owner) return;
  auto *self = context->owner;
  if (!self->_destroying && self->_pairBody.get() &&
      context->generation == self->_listGeneration) self->onDevice(*context);
}
void BluetoothSettingsScreen::onDevice(const RowContext &row) {
  _choiceDevice = row.device; // immutable address + address type, never a scan index
  _hasChoice = true;
  if (_choiceLabel.get()) {
    char text[96];
    std::snprintf(text, sizeof text, TR("Pair with %s:"),
                  _choiceDevice.name[0] ? _choiceDevice.name : TR("this device"));
    lv_label_set_text(_choiceLabel.get(), text);
  }
  showChoice(true);
}
void BluetoothSettingsScreen::pairClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *self = static_cast<BluetoothSettingsScreen *>(lv_event_get_user_data(event));
  if (!self || self->_destroying) return;
  self->onPair(lv_event_get_current_target(event) == self->_codedPair.get());
}
void BluetoothSettingsScreen::onPair(bool withCode) {
  if (!_pairBody.get() || !_hasChoice) return;
  const uint32_t generation = _generation;
  const Device chosen = _choiceDevice;
  Snapshot before;
  if (!snapshot(before) || !still(generation)) return;
  _pairBaselineGeneration = before.generation;
  _pairSawProgress = false;
  _pairStarted = true;
  const bool accepted = _host.pairKeyboard &&
      _host.pairKeyboard(_host.context, chosen, withCode);
  if (!still(generation) || !_pairBody.get()) return;
  if (!accepted) {
    _pairStarted = false;
    alert(TR("Could not connect. Is the keyboard in pairing mode?"), 1600);
    return;
  }
  _hasChoice = false;
  showChoice(false);
  if (still(generation)) refresh();
}
void BluetoothSettingsScreen::choiceBackClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *self = static_cast<BluetoothSettingsScreen *>(lv_event_get_user_data(event));
  if (!self || self->_destroying || !self->_pairBody.get()) return;
  self->_hasChoice = false;
  self->_choiceDevice = Device{};
  self->showChoice(false);
}
void BluetoothSettingsScreen::rescanClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *self = static_cast<BluetoothSettingsScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying) self->onRescan();
}
void BluetoothSettingsScreen::onRescan() {
  if (!_pairBody.get()) return;
  const uint32_t generation = _generation;
  _hasChoice = false;
  _choiceDevice = Device{};
  showChoice(false);
  if (!still(generation)) return;
  if (_host.scanKeyboards) _host.scanKeyboards(_host.context, true);
  if (!still(generation)) return;
  Snapshot page;
  if (snapshot(page) && still(generation)) updatePairing(page, true);
}

} } // namespace ui::screens

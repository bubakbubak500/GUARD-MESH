// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../widgets/ObjectRef.h"
#include <cstddef>
#include <cstdint>

namespace ui { namespace screens {

// UI-thread owner for the Bluetooth settings and keyboard-pairing pages. The
// transport, preference store, confirmation popup, and settings navigation are
// supplied by Host; this module has no CAP_BLE_KEYBOARD dependency.
class BluetoothSettingsScreen {
public:
  static constexpr unsigned FoundCapacity = 12;
  enum class State : uint8_t {
    Off, Idle, Waiting, Scanning, Connecting, Pairing, Connected, Failed
  };
  enum class Error : uint8_t {
    None, NotRunning, Connect, PairingCode, PairingNoCode, NotKeyboard, NeedsPairing
  };
  struct Device {
    char name[32] = {};
    uint8_t addr[6] = {};
    uint8_t addrType = 0;
    int8_t rssi = 0;
    bool keyboard = false;
  };
  struct Snapshot {
    bool bleCapable = false, wifiOn = false, radioControllable = true;
    bool bleActive = false, bleRequested = false;
    bool keyboardSupported = false, keyboardMode = false, keyboardActive = false;
    bool paired = false, capturingKey = false;
    Device pairedDevice{};
    State state = State::Off;
    Error error = Error::None;
    uint32_t generation = 0, pairingCode = 0, pin = 0;
    uint8_t layout = 0, backKey = 0;
    unsigned foundCount = 0;
    Device found[FoundCapacity]{};
  };
  struct Host {
    void *context = nullptr;
    bool (*readSnapshot)(void *, Snapshot &) = nullptr;
    // A rejected Pager start may still leave bleRequested=true. Read the next
    // snapshot to decide the switch state; this return only controls the toast.
    bool (*requestRadio)(void *, bool enabled) = nullptr;
    const char *(*radioFailureText)(void *) = nullptr;
    bool (*savePin)(void *, uint32_t pin) = nullptr;
    bool (*setKeyboardMode)(void *, bool keyboard) = nullptr;
    bool (*setLayout)(void *, uint8_t layout) = nullptr;
    void (*captureBackKey)(void *, bool on) = nullptr;
    void (*formatBackKey)(void *, uint8_t usage, char *, size_t) = nullptr;
    void (*scanKeyboards)(void *, bool on) = nullptr;
    // These enqueue immutable device commands. true means accepted for work,
    // not that the link/bond operation has finished successfully.
    bool (*pairKeyboard)(void *, const Device &, bool withCode) = nullptr;
    bool (*forgetKeyboardIfMatches)(void *, const Device &) = nullptr;
    // Confirmation is owned by the host popup. It must call confirmForget(token)
    // on the current screen owner; stale tokens are rejected by this class.
    void (*requestForgetConfirmation)(void *, uint32_t token, const Device &) = nullptr;
    void (*openPairingPage)(void *) = nullptr;
    void (*closePairingPage)(void *) = nullptr;
    void (*syncKeyboard)(void *) = nullptr;
    bool (*blurFromDelete)(void *, lv_event_t *) = nullptr;
    void (*attachPinTextArea)(void *, lv_obj_t *) = nullptr;
    void (*styleLayoutDropdown)(void *, lv_obj_t *) = nullptr;
    void (*alert)(void *, const char *, unsigned durationMs) = nullptr;
    void (*refitPage)(void *) = nullptr;
    void (*navDetachBeforeTreeMutation)(void *) = nullptr;
    void (*navDirty)(void *) = nullptr;
  };

  explicit BluetoothSettingsScreen(Host host);
  ~BluetoothSettingsScreen();
  BluetoothSettingsScreen(const BluetoothSettingsScreen &) = delete;
  BluetoothSettingsScreen &operator=(const BluetoothSettingsScreen &) = delete;

  void buildSettings(lv_obj_t *body, lv_coord_t width);
  void buildPairing(lv_obj_t *body, lv_coord_t width);
  void refresh(); // UI loop; reads one bounded snapshot
  void detach();
  bool confirmForget(uint32_t token);
  bool pairingOpen() const { return _pairBody.get() != nullptr; }

private:
  struct RowContext {
    BluetoothSettingsScreen *owner;
    uint32_t generation;
    Device device;
  };
  struct SettingsSignature {
    uint32_t generation = 0, pin = 0;
    bool bleCapable = false, wifiOn = false, radioControllable = true, bleActive = false;
    bool bleRequested = false, keyboardSupported = false;
    bool keyboardMode = false, keyboardActive = false;
    bool paired = false, capturingKey = false;
    State state = State::Off;
    Error error = Error::None;
    uint8_t layout = 0, backKey = 0;
    Device pairedDevice{};
  };
  static SettingsSignature signature(const Snapshot &);
  static bool sameSignature(const SettingsSignature &, const SettingsSignature &);
  static bool sameIdentity(const Device &, const Device &);
  static void settingsDeleted(lv_event_t *);
  static void pairingDeleted(lv_event_t *);
  static void radioChanged(lv_event_t *);
  static void modeClicked(lv_event_t *);
  static void pinBlurred(lv_event_t *);
  static void layoutChanged(lv_event_t *);
  static void backKeyClicked(lv_event_t *);
  static void openPairingClicked(lv_event_t *);
  static void forgetClicked(lv_event_t *);
  static void deviceClicked(lv_event_t *);
  static void pairClicked(lv_event_t *);
  static void choiceBackClicked(lv_event_t *);
  static void rescanClicked(lv_event_t *);

  bool snapshot(Snapshot &);
  bool still(uint32_t) const;
  void detachTree(lv_obj_t *, bool deletingRoot = false);
  void clearSettings(bool deleting);
  void clearPairing(bool deleting);
  void releaseRows();
  void updateSettings(const Snapshot &);
  void updatePairing(const Snapshot &, bool rebuild);
  void rebuildDevices(const Snapshot &);
  void showChoice(bool);
  void alert(const char *, unsigned = 1200);
  void navDirty();
  void onRadio(bool);
  void onMode(bool);
  void onPinBlur(lv_event_t *);
  void onLayout(uint8_t);
  void onBackKey();
  void onForget();
  void onDevice(const RowContext &);
  void onPair(bool withCode);
  void onRescan();

  Host _host;
  widgets::ObjectRef _settingsBody, _pairBody, _radio, _pin;
  widgets::ObjectRef _phoneBox, _keyboardBox, _modeLine, _segment[2];
  widgets::ObjectRef _name, _icon, _status, _forget, _pairButton, _tip, _backButton;
  widgets::ObjectRef _pairStatus, _pairCode, _pairList, _choice, _choiceLabel;
  widgets::ObjectRef _plainPair, _codedPair;
  RowContext *_rows = nullptr;
  unsigned _rowCount = 0;
  Device _choiceDevice{}, _forgetDevice{};
  bool _hasChoice = false, _pairStarted = false, _destroying = false;
  bool _pairSawProgress = false;
  bool _hasForgetRequest = false, _capturingOwned = false, _building = false;
  bool _requested = false;
  uint32_t _generation = 0, _listGeneration = 0, _seenKeyboardGeneration = UINT32_MAX;
  uint32_t _pairBaselineGeneration = 0;
  uint32_t _nextForgetToken = 0, _forgetToken = 0, _forgetGeneration = 0;
  uint32_t _savedPin = 0;
  SettingsSignature _settingsSignature{};
  bool _haveSettingsSignature = false;
  lv_coord_t _width = 0;
};

} } // namespace ui::screens

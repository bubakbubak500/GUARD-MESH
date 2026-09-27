// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../widgets/ObjectRef.h"
#include <cstddef>
#include <cstdint>

namespace ui { namespace screens {

// UI-thread owner for the touch admin console and the two explicit room login
// flows. Mesh reply callbacks contain a contact, not a request identifier: an
// old and a new login to the same contact remain indistinguishable on the wire.
class AdminSessionScreen {
public:
  struct Contact {
    uint8_t key[32] = {};
    char name[40] = {};
    bool room = false;
  };
  struct Host {
    void *context = nullptr;
    int (*statusHeight)(void *) = nullptr;
    void (*closeRoot)(void *, lv_obj_t **) = nullptr;
    bool (*resolve)(void *, const uint8_t[32], Contact &) = nullptr;
    bool (*sendLogin)(void *, const uint8_t[32], const char *) = nullptr;
    bool (*sendRoomRelogin)(void *, const uint8_t[32]) = nullptr;
    bool (*sendCommand)(void *, const uint8_t[32], const char *) = nullptr;
    bool (*openRoomChat)(void *, const uint8_t[32]) = nullptr;
    bool (*loadPassword)(void *, const uint8_t[32], char *, size_t) = nullptr;
    void (*storePassword)(void *, const uint8_t[32], const char *) = nullptr;
    void (*alert)(void *, const char *, unsigned) = nullptr;
    void (*markMeshRequest)(void *) = nullptr;
    void (*deviceClockLabel)(void *, char *, size_t) = nullptr;
    void (*sanitize)(void *, const lv_font_t *, char *, size_t, const char *) = nullptr;
    void (*attachTextArea)(void *, lv_obj_t *) = nullptr;
    void (*attachSymbolButton)(void *, lv_obj_t *) = nullptr;
    bool (*hasKeyboard)(void *) = nullptr;
    void (*bindKeyboard)(void *, lv_obj_t *) = nullptr;
    void (*syncKeyboard)(void *) = nullptr;
    // Mirrors the existing shared keyboard guard while the owner's deferred
    // dismissal is queued; the owner cancels that callback on prompt close.
    void (*setKeyboardDismissPending)(void *, bool) = nullptr;
    void (*hideKeyboard)(void *) = nullptr;
  };

  explicit AdminSessionScreen(Host host);
  ~AdminSessionScreen();
  AdminSessionScreen(const AdminSessionScreen &) = delete;
  AdminSessionScreen &operator=(const AdminSessionScreen &) = delete;

  void openAdmin(const Contact &);
  void openJoin(const Contact &, bool afterReloginFailed = false);
  void openJoinByKey(const uint8_t key[32], bool afterReloginFailed = false);
  void beginRoomRelogin(const uint8_t key[32]);
  void onLoginResult(const uint8_t key[32], bool room, bool success, uint8_t perms);
  void onCommandReply(const uint8_t key[32], const char *text);

  void closePicker();
  void closePrompt();
  void closeConsole();
  bool pickerOpen() const { return _pickerRoot.get() != nullptr; }
  bool promptOpen() const { return _promptRoot.get() != nullptr; }
  bool consoleOpen() const { return _consoleRoot.get() != nullptr; }
  bool interactiveLoginPending() const { return _pending != Pending::None; }
  lv_obj_t *logScrollBody() const { return _logBox.get(); }

private:
  enum class Pending : uint8_t { None, Password, RoomRelogin };
  enum class PromptKind : uint8_t { Admin, Join };
  enum class RootKind : uint8_t { Prompt, Console, Picker };
  struct RowContext { AdminSessionScreen *owner; unsigned index; };
  struct RootContext { AdminSessionScreen *owner; RootKind kind; };

  static void deleted(lv_event_t *);
  static void closeClicked(lv_event_t *);
  static void submitClicked(lv_event_t *);
  static void sendClicked(lv_event_t *);
  static void pickerClicked(lv_event_t *);
  static void rowClicked(lv_event_t *);
  static void timeout(lv_timer_t *);
  static void dismissDeferred(void *);
  void closeRoot(RootKind);
  void externalDelete(RootKind, lv_obj_t *);
  void detachTree(lv_obj_t *);
  void openPrompt(const Contact &, PromptKind, bool);
  void openConsole(const Contact &);
  void openPicker();
  void submit();
  void sendCommand();
  void appendLog(const char *, const char *);
  void armTimer();
  void cancelTimer();
  void cancelDismiss();
  void clearPending();
  void alert(const char *, unsigned);
  bool current(const uint8_t key[32]) const;
  bool pendingKey(const uint8_t key[32]) const;

  Host _host;
  widgets::ObjectRef _promptRoot, _promptText, _remember;
  widgets::ObjectRef _consoleRoot, _logBox, _logLabel, _commandText;
  widgets::ObjectRef _pickerRoot;
  Contact _target{};
  uint8_t _pendingKey[32] = {};
  char _password[16] = {};
  char *_log = nullptr; // Lazily allocated in PSRAM where available.
  size_t _logLength = 0;
  size_t _rowCount = 0;
  RowContext _rows[32] = {};
  RootContext _rootContexts[3] = {};
  lv_timer_t *_timer = nullptr;
  Pending _pending = Pending::None;
  PromptKind _promptKind = PromptKind::Admin;
  bool _targetValid = false;
  bool _rememberPassword = false;
  bool _dismissPending = false;
  bool _destroying = false;
  uint32_t _generation = 0;
};

} } // namespace ui::screens

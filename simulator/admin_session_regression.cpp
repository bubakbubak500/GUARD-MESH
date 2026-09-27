// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.h"
#include "screens/AdminSessionScreen.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Screen = ui::screens::AdminSessionScreen;

const char *scenario = "initialization";

void check(bool condition, const char *message) {
  if (!condition) {
    char detail[320];
    std::snprintf(detail, sizeof detail, "Admin session '%s': %s", scenario, message);
    throw std::runtime_error(detail);
  }
}

bool sameKey(const uint8_t left[32], const uint8_t right[32]) {
  return std::memcmp(left, right, 32) == 0;
}

Screen::Contact contact(uint8_t seed, const char *name, bool room = false) {
  Screen::Contact result;
  for (unsigned i = 0; i < 32; ++i)
    result.key[i] = static_cast<uint8_t>(seed + i);
  std::snprintf(result.name, sizeof result.name, "%s", name);
  result.room = room;
  return result;
}

struct Fixture {
  struct Entry {
    Screen::Contact value;
    bool alive = true;
    std::string saved;
  };
  Entry entries[3];
  Screen *screen = nullptr;
  unsigned logins = 0, relogins = 0, commands = 0, chats = 0;
  unsigned stores = 0, alerts = 0, marks = 0, closes = 0, hideCalls = 0;
  uint8_t loginKey[32] = {}, reloginKey[32] = {}, commandKey[32] = {}, chatKey[32] = {};
  std::string loginPassword, commandText, storedPassword, lastAlert;
  bool sendOk = true, chatOk = true;
  bool reopenOnClose = false, replaceOnSend = false, closeOnStore = false;
  bool replaceOnAlert = false, replaceOnSync = false;
  bool dismissPending = false;
  Screen::Contact replacement;

  Fixture() {
    entries[0].value = contact(0x10, "Repeater Alpha");
    entries[1].value = contact(0x50, "Repeater Bravo");
    entries[2].value = contact(0x90, "Room Cedar", true);
    replacement = entries[1].value;
  }

  Entry *find(const uint8_t key[32]) {
    for (auto &entry : entries)
      if (entry.alive && sameKey(entry.value.key, key))
        return &entry;
    return nullptr;
  }

  static Fixture &self(void *context) { return *static_cast<Fixture *>(context); }
  static int statusHeight(void *) { return 24; }
  static void closeRoot(void *context, lv_obj_t **root) {
    auto &f = self(context);
    if (!root || !*root)
      return;
    auto *old = *root;
    *root = nullptr;
    ++f.closes;
    lv_obj_del_async(old);
    if (f.reopenOnClose && f.screen) {
      f.reopenOnClose = false;
      f.screen->openAdmin(f.replacement);
    }
  }
  static bool resolve(void *context, const uint8_t key[32], Screen::Contact &out) {
    auto *entry = self(context).find(key);
    if (!entry)
      return false;
    out = entry->value;
    return true;
  }
  static bool sendLogin(void *context, const uint8_t key[32], const char *password) {
    auto &f = self(context);
    ++f.logins;
    std::memcpy(f.loginKey, key, 32);
    f.loginPassword = password ? password : "";
    if (f.replaceOnSend && f.screen) {
      f.replaceOnSend = false;
      f.screen->openAdmin(f.replacement);
    }
    return f.sendOk;
  }
  static bool sendRoomRelogin(void *context, const uint8_t key[32]) {
    auto &f = self(context);
    ++f.relogins;
    std::memcpy(f.reloginKey, key, 32);
    return f.sendOk;
  }
  static bool sendCommand(void *context, const uint8_t key[32], const char *command) {
    auto &f = self(context);
    ++f.commands;
    std::memcpy(f.commandKey, key, 32);
    f.commandText = command ? command : "";
    if (f.replaceOnSend && f.screen) {
      f.replaceOnSend = false;
      f.screen->openAdmin(f.replacement);
    }
    return f.sendOk;
  }
  static bool openRoomChat(void *context, const uint8_t key[32]) {
    auto &f = self(context);
    ++f.chats;
    std::memcpy(f.chatKey, key, 32);
    return f.find(key) && f.chatOk;
  }
  static bool loadPassword(void *context, const uint8_t key[32], char *out, size_t capacity) {
    auto *entry = self(context).find(key);
    if (!entry || entry->saved.empty() || !capacity)
      return false;
    std::snprintf(out, capacity, "%s", entry->saved.c_str());
    return true;
  }
  static void storePassword(void *context, const uint8_t key[32], const char *password) {
    auto &f = self(context);
    ++f.stores;
    f.storedPassword = password ? password : "";
    auto *entry = f.find(key);
    if (entry)
      entry->saved = f.storedPassword;
    if (f.closeOnStore && f.screen) {
      f.closeOnStore = false;
      f.screen->closePrompt();
    }
  }
  static void alert(void *context, const char *message, unsigned) {
    auto &f = self(context);
    ++f.alerts;
    f.lastAlert = message ? message : "";
    if (f.replaceOnAlert && f.screen) {
      f.replaceOnAlert = false;
      f.screen->openAdmin(f.replacement);
    }
  }
  static void markMeshRequest(void *context) { ++self(context).marks; }
  static void deviceClockLabel(void *, char *out, size_t capacity) {
    if (capacity)
      std::snprintf(out, capacity, "%s", "12:34");
  }
  static void sanitize(void *, const lv_font_t *, char *out, size_t capacity, const char *in) {
    if (capacity)
      std::snprintf(out, capacity, "%s", in ? in : "");
  }
  static void syncKeyboard(void *context) {
    auto &f = self(context);
    if (f.replaceOnSync && f.screen) {
      f.replaceOnSync = false;
      f.screen->openAdmin(f.replacement);
    }
  }
  static void setKeyboardDismissPending(void *context, bool pending) {
    self(context).dismissPending = pending;
  }
  static void hideKeyboard(void *context) { ++self(context).hideCalls; }

  Screen::Host host() {
    Screen::Host h;
    h.context = this;
    h.statusHeight = statusHeight;
    h.closeRoot = closeRoot;
    h.resolve = resolve;
    h.sendLogin = sendLogin;
    h.sendRoomRelogin = sendRoomRelogin;
    h.sendCommand = sendCommand;
    h.openRoomChat = openRoomChat;
    h.loadPassword = loadPassword;
    h.storePassword = storePassword;
    h.alert = alert;
    h.markMeshRequest = markMeshRequest;
    h.deviceClockLabel = deviceClockLabel;
    h.sanitize = sanitize;
    h.syncKeyboard = syncKeyboard;
    h.setKeyboardDismissPending = setKeyboardDismissPending;
    h.hideKeyboard = hideKeyboard;
    return h;
  }
};

lv_obj_t *lastRoot() {
  const auto count = lv_obj_get_child_cnt(lv_layer_top());
  return count ? lv_obj_get_child(lv_layer_top(), count - 1) : nullptr;
}

void click(lv_obj_t *object) {
  check(object != nullptr, "click target missing");
  lv_event_send(object, LV_EVENT_CLICKED, nullptr);
}

std::string labelText(lv_obj_t *label) {
  const auto mode = lv_label_get_long_mode(label);
  if (mode == LV_LABEL_LONG_DOT)
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  const char *raw = lv_label_get_text(label);
  std::string value = raw ? raw : "";
  if (mode == LV_LABEL_LONG_DOT)
    lv_label_set_long_mode(label, mode);
  return value;
}

bool containsLabel(lv_obj_t *root, const char *needle) {
  if (!root)
    return false;
  if (lv_obj_check_type(root, &lv_label_class) &&
      labelText(root).find(needle) != std::string::npos)
    return true;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (containsLabel(lv_obj_get_child(root, i), needle))
      return true;
  return false;
}

lv_obj_t *button(lv_obj_t *root, const char *caption) {
  if (!root)
    return nullptr;
  lv_obj_update_layout(root);
  if (lv_obj_check_type(root, &lv_btn_class) && containsLabel(root, caption))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = button(lv_obj_get_child(root, i), caption))
      return found;
  return nullptr;
}

lv_obj_t *pickerCommandRow(lv_obj_t *root, const char *caption) {
  if (!root)
    return nullptr;
  lv_obj_update_layout(root);
  if (lv_obj_check_type(root, &lv_list_btn_class)) {
    const char *text = lv_list_get_btn_text(lv_obj_get_parent(root), root);
    if (text && !std::strcmp(text, caption))
      return root;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = pickerCommandRow(lv_obj_get_child(root, i), caption))
      return found;
  return nullptr;
}

lv_obj_t *byClass(lv_obj_t *root, const lv_obj_class_t *type) {
  if (!root)
    return nullptr;
  if (lv_obj_check_type(root, type))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = byClass(lv_obj_get_child(root, i), type))
      return found;
  return nullptr;
}

lv_obj_t *textarea(lv_obj_t *root) {
  return byClass(root, &lv_textarea_class);
}

lv_obj_t *checkbox(lv_obj_t *root) {
  return byClass(root, &lv_checkbox_class);
}

lv_obj_t *loginButton(lv_obj_t *root, bool room) {
  auto *found = button(root, room ? "Join" : "Login");
  check(found, "login/Join button missing");
  return found;
}

void enter(lv_obj_t *root, const char *password, bool room) {
  auto *field = textarea(root);
  check(field, "password field missing");
  lv_textarea_set_text(field, password);
  click(loginButton(root, room));
}

std::vector<lv_timer_t *> timers() {
  std::vector<lv_timer_t *> result;
  for (auto *timer = lv_timer_get_next(nullptr); timer; timer = lv_timer_get_next(timer))
    result.push_back(timer);
  return result;
}

lv_timer_t *newTimer(const std::vector<lv_timer_t *> &before) {
  for (auto *timer : timers())
    if (std::find(before.begin(), before.end(), timer) == before.end() &&
        timer->period == 20000)
      return timer;
  return nullptr;
}

struct TimerIdentity {
  lv_timer_cb_t callback;
  void *owner;
  uint32_t period;
};

TimerIdentity timerIdentity(const lv_timer_t *timer) {
  return {timer->timer_cb, timer->user_data, timer->period};
}

bool timerLive(const TimerIdentity &expected) {
  for (auto *timer : timers())
    if (timer->timer_cb == expected.callback &&
        timer->user_data == expected.owner &&
        timer->period == expected.period)
      return true;
  return false;
}

void cleanup(Screen &screen, void (*pump)(unsigned)) {
  screen.closePicker();
  screen.closePrompt();
  screen.closeConsole();
  pump(40);
}

void countDelete(lv_event_t *event) {
  ++*static_cast<int *>(lv_event_get_user_data(event));
}

} // namespace

void runAdminSessionRegression(void (*pump)(unsigned)) {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  const auto language = i18nGetLang();
  i18nSetLang(LANG_EN);
  {
    Fixture f;
    Screen screen(f.host());
    f.screen = &screen;
    scenario = "copied-key-and-pending";
    auto input = f.entries[0].value;
    const auto expected = input;
    screen.openAdmin(input);
    auto *prompt = lastRoot();
    std::memset(input.key, 0xEE, sizeof input.key);
    std::snprintf(input.name, sizeof input.name, "%s", "mutated");
    enter(prompt, "firstpw", false);
    check(f.logins == 1 && sameKey(f.loginKey, expected.key) &&
              f.loginPassword == "firstpw" && screen.interactiveLoginPending(),
          "login did not snapshot its target key and password");
    click(loginButton(prompt, false));
    check(f.logins == 1, "duplicate submit sent another active login");
    screen.onLoginResult(f.entries[1].value.key, false, true, 0);
    check(screen.interactiveLoginPending(), "unrelated reply canceled active login");
    screen.onLoginResult(expected.key, false, true, 0);
    check(!screen.interactiveLoginPending() && screen.consoleOpen() && f.stores == 1 &&
              f.entries[0].saved == "firstpw", "successful reply lost submitted password");
    cleanup(screen, pump);

    scenario = "unchecked-remember";
    screen.openAdmin(f.entries[0].value);
    prompt = lastRoot();
    auto *remember = checkbox(prompt);
    check(remember && !std::strcmp(lv_textarea_get_text(textarea(prompt)), "firstpw"),
          "saved password did not prefill the next prompt");
    lv_obj_clear_state(remember, LV_STATE_CHECKED);
    enter(prompt, "secondpw", false);
    screen.onLoginResult(expected.key, false, true, 0);
    check(f.stores == 2 && f.entries[0].saved.empty(),
          "unchecked Remember did not clear the saved password");
    cleanup(screen, pump);

    scenario = "reentrant-store";
    screen.openAdmin(f.entries[0].value);
    enter(lastRoot(), "reentrant", false);
    const auto oldStores = f.stores;
    f.closeOnStore = true;
    screen.onLoginResult(expected.key, false, true, 0);
    check(f.stores == oldStores + 1 && !screen.consoleOpen() && !screen.promptOpen(),
          "store callback closed prompt but old success opened console");
    cleanup(screen, pump);

    scenario = "reentrant-result-close";
    screen.openAdmin(f.entries[0].value);
    enter(lastRoot(), "replacement", false);
    f.reopenOnClose = true;
    screen.onLoginResult(expected.key, false, true, 0);
    check(screen.promptOpen() && !screen.consoleOpen() &&
              containsLabel(lastRoot(), "Repeater Bravo"),
          "old login result displaced prompt opened by Host closeRoot");
    cleanup(screen, pump);
  }

  {
    Fixture f;
    Screen screen(f.host());
    f.screen = &screen;
    scenario = "join-key-not-slot";
    auto room = f.entries[2].value;
    screen.openJoinByKey(room.key);
    check(screen.promptOpen(), "room Join prompt did not open by key");
    // Reorder the fixture after rendering. The stored key must still route the
    // result to this room rather than to whichever contact now occupies its slot.
    std::swap(f.entries[0], f.entries[2]);
    enter(lastRoot(), "roompw", true);
    screen.onLoginResult(room.key, true, true, 0);
    check(f.chats == 1 && sameKey(f.chatKey, room.key) && !screen.consoleOpen(),
          "room Join used a stale contact index or opened admin console");
    cleanup(screen, pump);

    scenario = "deleted-room";
    screen.openJoinByKey(room.key);
    check(screen.promptOpen(), "second Join prompt missing");
    enter(lastRoot(), "roompw", true);
    auto *entry = f.find(room.key);
    check(entry, "room fixture missing");
    entry->alive = false;
    screen.onLoginResult(room.key, true, true, 0);
    check(f.chats == 1 && !screen.consoleOpen(),
          "deleted room opened another contact's chat or admin console");
    cleanup(screen, pump);
  }

  {
    Fixture f;
    Screen screen(f.host());
    f.screen = &screen;
    auto room = f.entries[2].value;
    scenario = "manual-relogin-success-failure";
    screen.beginRoomRelogin(room.key);
    check(f.relogins == 1 && sameKey(f.reloginKey, room.key) &&
              screen.interactiveLoginPending(), "manual room re-login did not start");
    screen.onLoginResult(f.entries[0].value.key, false, false, 0);
    check(screen.interactiveLoginPending(), "unrelated reply canceled room re-login");
    screen.onLoginResult(room.key, true, true, 0);
    check(!screen.interactiveLoginPending() && !screen.promptOpen(),
          "successful manual re-login escalated to Join");
    screen.beginRoomRelogin(room.key);
    screen.onLoginResult(room.key, true, false, 0);
    check(!screen.interactiveLoginPending() && screen.promptOpen() &&
              containsLabel(lastRoot(), "forgotten"),
          "failed manual re-login did not explain Join escalation");
    cleanup(screen, pump);

    scenario = "manual-relogin-timeout";
    const auto before = timers();
    screen.beginRoomRelogin(room.key);
    auto *timer = newTimer(before);
    check(timer, "20-second room re-login watchdog missing");
    const auto watchdogIdentity = timerIdentity(timer);
    lv_timer_ready(timer);
    lv_timer_handler();
    check(!screen.interactiveLoginPending() && screen.promptOpen() &&
              containsLabel(lastRoot(), "forgotten"),
          "room re-login timeout did not escalate to Join");
    check(!timerLive(watchdogIdentity), "expired watchdog remained live");
    cleanup(screen, pump);

    scenario = "reentrant-relogin-alert";
    const auto timersBeforeAlert = timers();
    f.replaceOnAlert = true;
    screen.beginRoomRelogin(room.key);
    check(screen.promptOpen() && !screen.interactiveLoginPending() &&
              !newTimer(timersBeforeAlert),
          "reentrant alert retained old room watchdog over replacement prompt");
    cleanup(screen, pump);

    scenario = "explicit-login-retires-manual";
    screen.beginRoomRelogin(room.key);
    screen.openJoinByKey(room.key);
    enter(lastRoot(), "explicit", true);
    const auto prompts = lv_obj_get_child_cnt(lv_layer_top());
    screen.onLoginResult(f.entries[0].value.key, false, false, 0);
    check(screen.interactiveLoginPending() && lv_obj_get_child_cnt(lv_layer_top()) == prompts,
          "unrelated reply changed explicit Join");
    screen.onLoginResult(room.key, true, true, 0);
    check(f.chats == 1 && !screen.promptOpen() && !screen.interactiveLoginPending(),
          "explicit Join did not supersede manual re-login");
    cleanup(screen, pump);
  }

  {
    Fixture f;
    Screen screen(f.host());
    f.screen = &screen;
    scenario = "close-and-late-reply";
    screen.openAdmin(f.entries[0].value);
    auto *prompt = lastRoot();
    auto *oldButton = loginButton(prompt, false);
    enter(prompt, "closedpw", false);
    screen.closePrompt();
    click(oldButton);
    screen.onLoginResult(f.entries[0].value.key, false, true, 0);
    check(f.logins == 1 && f.stores == 0 && !screen.consoleOpen() &&
              !screen.interactiveLoginPending(),
          "retired prompt sent login or late reply persisted/opened console");
    pump(40);

    scenario = "close-cancels-watchdog";
    screen.openAdmin(f.entries[0].value);
    const auto timersBeforeClose = timers();
    enter(lastRoot(), "watchdog", false);
    auto *watchdog = newTimer(timersBeforeClose);
    check(watchdog, "password login watchdog missing");
    const auto passwordWatchdog = timerIdentity(watchdog);
    int deletedPrompt = 0;
    lv_obj_add_event_cb(lastRoot(), countDelete, LV_EVENT_DELETE, &deletedPrompt);
    screen.closePrompt();
    check(!screen.interactiveLoginPending() && !timerLive(passwordWatchdog),
          "closing prompt left password watchdog armed");
    pump(40);
    check(deletedPrompt == 1, "closed prompt was not deleted asynchronously");

    scenario = "replacement-cancels-deferred-keyboard-hide";
    screen.openAdmin(f.entries[0].value);
    enter(lastRoot(), "deferred", false);
    check(f.dismissPending, "submit did not register deferred keyboard dismissal");
    screen.openAdmin(f.entries[1].value);
    const auto hidesAfterReplacement = f.hideCalls;
    check(!f.dismissPending, "replacing prompt left keyboard dismissal pending");
    pump(40);
    check(f.hideCalls == hidesAfterReplacement && screen.promptOpen(),
          "old deferred keyboard callback hid replacement prompt keyboard");
    cleanup(screen, pump);

    scenario = "replacement-and-external-delete";
    screen.openAdmin(f.entries[0].value);
    prompt = lastRoot();
    oldButton = loginButton(prompt, false);
    const auto sendsBeforeReplacement = f.logins;
    screen.openAdmin(f.entries[1].value);
    click(oldButton);
    check(f.logins == sendsBeforeReplacement && screen.promptOpen(),
          "old prompt control submitted replacement contact");
    pump(40);
    prompt = lastRoot();
    int deletes = 0;
    for (int i = 0; i < 3; ++i)
      lv_obj_add_event_cb(prompt, countDelete, LV_EVENT_DELETE, &deletes);
    lv_obj_del(prompt);
    check(deletes == 3 && !screen.promptOpen(),
          "external prompt DELETE skipped observers or retained owner");
    screen.onLoginResult(f.entries[1].value.key, false, true, 0);
    check(f.stores == 0 && !screen.consoleOpen(),
          "reply after external DELETE changed session");
    cleanup(screen, pump);

    scenario = "destroyed-owner";
    lv_obj_t *orphan = nullptr;
    {
      Screen local(f.host());
      f.screen = &local;
      local.openAdmin(f.entries[0].value);
      orphan = loginButton(lastRoot(), false);
      enter(lastRoot(), "destroyed", false);
      check(f.dismissPending, "local submit did not queue deferred dismissal");
    }
    const auto sent = f.logins;
    const auto hidesAfterDestruction = f.hideCalls;
    click(orphan);
    check(f.logins == sent, "button retained destroyed screen owner");
    check(!f.dismissPending, "destroyed owner retained pending keyboard dismissal");
    f.screen = &screen;
    pump(40);
    check(f.hideCalls == hidesAfterDestruction,
          "deferred keyboard callback invoked Host after screen destruction");

    scenario = "reentrant-close-replacement";
    screen.openAdmin(f.entries[0].value);
    prompt = lastRoot();
    oldButton = loginButton(prompt, false);
    const auto sendsBeforeClose = f.logins;
    f.reopenOnClose = true;
    screen.closePrompt();
    click(oldButton);
    check(screen.promptOpen() && f.logins == sendsBeforeClose &&
              containsLabel(lastRoot(), "Repeater Bravo"),
          "reentrant Host close let a retired button act on replacement");
    cleanup(screen, pump);

    scenario = "reentrant-open-replacement";
    screen.openAdmin(f.entries[0].value);
    f.reopenOnClose = true;
    screen.openAdmin(f.entries[0].value);
    check(screen.promptOpen() && containsLabel(lastRoot(), "Repeater Bravo"),
          "opening a prompt overrode Host replacement during prior close");
    cleanup(screen, pump);
  }

  {
    Fixture f;
    Screen screen(f.host());
    f.screen = &screen;
    scenario = "bounded-log-and-command-target";
    screen.openAdmin(f.entries[0].value);
    enter(lastRoot(), "consolepw", false);
    screen.onLoginResult(f.entries[0].value.key, false, true, 0);
    check(screen.consoleOpen(), "admin console did not open");
    auto *console = lastRoot();
    auto *body = screen.logScrollBody();
    check(body, "admin log scroll body missing");
    std::string large(6000, 'R');
    screen.onCommandReply(f.entries[1].value.key, large.c_str());
    check(!containsLabel(body, "RRRRRRRRRRRRRRRRRRRR"),
          "unrelated command reply reached the open console");
    screen.onCommandReply(f.entries[0].value.key, large.c_str());
    screen.onCommandReply(f.entries[0].value.key, "TAIL-MARKER");
    check(containsLabel(body, "TAIL-MARKER"), "log failed after a very long reply");
    auto *logLabel = byClass(body, &lv_label_class);
    check(logLabel && std::strlen(lv_label_get_text(logLabel)) < 1024,
          "admin log lost NUL termination or exceeded fixed capacity");
    auto *command = textarea(console);
    check(command, "console command field missing");
    lv_textarea_set_text(command, "status");
    auto *send = button(console, LV_SYMBOL_RIGHT);
    check(send, "console Send control missing");
    click(send);
    check(f.commands == 1 && sameKey(f.commandKey, f.entries[0].value.key) &&
              f.commandText == "status", "console command used wrong target or text");
    scenario = "removed-command-contact";
    f.entries[0].alive = false;
    lv_textarea_set_text(command, "ver");
    const auto commandsBeforeMissing = f.commands;
    click(send);
    check(f.commands == commandsBeforeMissing &&
              !std::strcmp(lv_textarea_get_text(command), "ver") &&
              containsLabel(body, "contact missing"),
          "removed contact sent command, lost draft or omitted error log");
    f.entries[0].alive = true;
    scenario = "picker-stale-row";
    auto *pickerButton = button(console, LV_SYMBOL_LIST);
    check(pickerButton, "command picker control missing");
    click(pickerButton);
    check(screen.pickerOpen(), "command picker did not open");
    auto *picker = lastRoot();
    auto *oldRow = pickerCommandRow(picker, "ver - firmware version");
    check(oldRow, "picker command row missing");
    screen.openAdmin(f.entries[1].value);
    click(oldRow);
    check(!screen.pickerOpen() && screen.promptOpen(),
          "retired picker row acted on replacement prompt");
    pump(40);
    enter(lastRoot(), "newpw", false);
    screen.onLoginResult(f.entries[1].value.key, false, true, 0);
    check(screen.consoleOpen(), "replacement admin console did not open");
    console = lastRoot();
    command = textarea(console);
    check(command && !std::strcmp(lv_textarea_get_text(command), ""),
          "retired command row filled replacement console field");

    scenario = "picker-select-and-external-delete";
    pickerButton = button(console, LV_SYMBOL_LIST);
    click(pickerButton);
    picker = lastRoot();
    click(pickerCommandRow(picker, "ver - firmware version"));
    check(!screen.pickerOpen() && !std::strcmp(lv_textarea_get_text(command), "ver"),
          "picker selection did not fill current command field");
    pump(40);
    click(pickerButton);
    picker = lastRoot();
    int pickerDeletes = 0;
    for (int i = 0; i < 3; ++i)
      lv_obj_add_event_cb(picker, countDelete, LV_EVENT_DELETE, &pickerDeletes);
    lv_obj_del(picker);
    check(pickerDeletes == 3 && !screen.pickerOpen(),
          "external picker DELETE skipped observers or left picker active");

    scenario = "reentrant-command-send";
    lv_textarea_set_text(command, "clock");
    f.replaceOnSend = true;
    const auto oldCommands = f.commands;
    click(button(console, LV_SYMBOL_RIGHT));
    check(f.commands == oldCommands + 1 && sameKey(f.commandKey, f.entries[1].value.key) &&
              screen.promptOpen() && !screen.consoleOpen(),
          "reentrant command send corrupted replacement session");
    cleanup(screen, pump);

    scenario = "reentrant-command-keyboard-sync";
    screen.openAdmin(f.entries[0].value);
    enter(lastRoot(), "syncpw", false);
    screen.onLoginResult(f.entries[0].value.key, false, true, 0);
    check(screen.consoleOpen(), "sync fixture console missing");
    console = lastRoot();
    command = textarea(console);
    lv_textarea_set_text(command, "clock sync");
    f.replaceOnSync = true;
    const auto commandsBeforeSync = f.commands;
    click(button(console, LV_SYMBOL_RIGHT));
    check(f.commands == commandsBeforeSync && screen.promptOpen() &&
              containsLabel(lastRoot(), "Repeater Bravo"),
          "keyboard sync submitted or dereferenced retired console field");
    cleanup(screen, pump);
  }

  i18nSetLang(language);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots,
        "admin session leaked a top-layer root");
  puts("Admin session: copied keys, login/Join results, password persistence, room watchdog, "
       "stale controls, log bounds and command target passed.");
}

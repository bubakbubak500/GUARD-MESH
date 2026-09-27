#pragma once
#include "models/LocationModel.h"
#include "models/MessageStore.h"
#include "application/ChatSession.h"
#include "application/MessageIngress.h"
#include "application/ThreadRefreshPolicy.h"
#include "application/ScreenPolicy.h"

#if defined(GUARD_SIMULATOR)
#include "SimTypes.h"
#else
#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/BaseSerialInterface.h>
#include <Arduino.h>
#include <helpers/sensors/LPPDataHelpers.h>

#ifndef LED_STATE_ON
  #define LED_STATE_ON 1
#endif

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif
#ifdef PIN_VIBRATION
  #include <helpers/ui/GenericVibration.h>
#endif

#include "../AbstractUITask.h"
#include "../NodePrefs.h"
#endif

struct ContactInfo;

/** Maps bottom tabs (Home, Chats, Contacts, Set — no separate Net tab). */
enum class TouchUiScreen : uint8_t { Home = 0, ChatInbox = 1, Contacts = 2, Settings = 3 };

void sdMountDiagBegin();
void sdMountDiagAttempt(uint32_t hz, bool begin_ok, bool card_ready);
void sdMountDiagSetMounted(bool mounted, uint32_t hz);

class UITask : public AbstractUITask, public ui::MessageTypes {
private:
  DisplayDriver* _display;
  SensorManager* _sensors;
#ifdef PIN_BUZZER
  genericBuzzer buzzer;
#endif
#ifdef PIN_VIBRATION
  GenericVibration vibration;
#endif
  unsigned long _next_refresh, _auto_off;
  // Owns lock, screen and deadline decisions; this task applies board effects.
  ui::ScreenPolicy _screen;
  NodePrefs* _node_prefs;
  // GPS auto-location: once a fix is seen, keep the node location (node_lat/lon,
  // used by the profile + adverts) synced to GPS and persist it occasionally
  // (rate-limited) so it survives a reboot. Updated each loop via updateGpsLocation().
  ui::LocationModel _location;
  void updateGpsLocation(unsigned long now);
  char _alert[80];
  unsigned long _alert_expiry;
  int _msgcount;
  ui::MessageStore _messages;
  ui::ChatSession _chat;
  // Compatibility for old notify()+newMsg* callers. Mesh reception uses an
  // explicit UIMessageEvent and never derives its kind from this state.
  UIEventType _legacy_message_kind = UIEventType::contactMessage;
  unsigned long _next_thread_seed;
  bool allocMessageStore();
  unsigned long ui_started_at, next_batt_chck;
  int next_backlight_btn_check = 0;
#ifdef PIN_STATUS_LED
  int led_state = 0;
  int next_led_change = 0;
  int last_led_increment = 0;
#endif

#ifdef PIN_USER_BTN_ANA
  unsigned long _analogue_pin_read_millis = millis();
#endif

  UIScreen* splash;
  UIScreen* home;
  UIScreen* chats;
  UIScreen* channels;
  UIScreen* thread_view;
  UIScreen* network;
  UIScreen* settings;
  UIScreen* curr;
  int _thread_scroll;
  bool _composer_mode;
  int _composer_char_idx;
  int _composer_action_idx;
  char _compose_buf[MAX_MSG_TEXT + 1];   // full LoRa text length; was 128 -> silently chopped sends at 127 bytes (GH #119)
  ui::ThreadRefreshPolicy _mesh_refresh;
  TouchUiScreen _touch_screen;

  void userLedHandler();

  // Button action handlers
  char checkDisplayOn(char c);
  char handleLongPress(char c);
  char handleDoubleClick(char c);
  char handleTripleClick(char c);
  int findOrCreateThread(const char* name, bool channel);
  bool looksLikeKnownChannel(const char* name) const;
public:
  bool refreshThreadsFromMesh();
  uint32_t meshDirectoryRefreshCount() const { return _mesh_refresh.passes(); }
  /** Drop UI thread `idx` and any cached messages tied to it. Returns false
   *  if the index is out of range or the slot wasn't in use. Used by the
   *  long-press → Delete chat action; for channel threads, the caller is
   *  expected to also free the the_mesh channel slot. */
  bool removeThread(int idx);
public:
  // Resolve the contact backing the currently-open conversation thread (used by
  // e.g. the message Info popup's "Trace route"). Returns false for channels or
  // when no DM thread is active.
  bool lookupActiveContact(ContactInfo& out) const;
private:
  void syncThreadMeshSlots(const char* thread_name, bool channel);
  int findThreadByName(const char* name, bool channel) const;
  int appendMessage(const char* thread, const char* sender, const char* text, bool channel, bool outgoing, bool mark_unread, uint32_t ack_hash = 0, uint8_t deliv_state = DELIV_NONE,
                    uint8_t meta_flags = 0, uint8_t path_len = 0, int8_t snr_q4 = 0, int8_t rssi = 0,
                    const uint8_t* in_path = nullptr, uint8_t in_path_n = 0, uint32_t sent_fp = 0,
                    uint16_t in_scope = 0);
  void handleIncomingMessage(const UIMessageEvent& input, bool notifyAccepted);
  // Adapter for old callers without packet-scoped metadata.
  void newMsgImpl(uint8_t path_len, const char* from_name, const char* text, int msgcount,
                  uint8_t meta_flags, int8_t snr_q4, int8_t rssi,
                  const char* sender_override = nullptr, const uint8_t* from_pub = nullptr);
public:
  /** Match an arriving ACK (4-byte hash) against the last few outgoing DMs
   *  and flip their delivery state to DELIV_DELIVERED so the chat detail
   *  shows the double-check. No-op if no match. */
  void onMessageAcked(uint32_t ack_hash);
  /** Trace-ping reply landed: open a modal with the bidirectional SNR
   *  numbers. Called by MyMesh::onTraceRecv when the trace's tag matches
   *  the one we issued from the contact action sheet's Trace Ping button. */
  void onTracePingResult(uint32_t tag, int8_t their_snr, int8_t our_snr,
                         uint8_t extra_hops, const int8_t* extra_snrs) override;
private:
  void setActiveThread(int idx, bool channel_mode);
  void resetComposer();
  void appendComposerChar(char c);
  void appendComposerText(const char* text);
  void backspaceComposerChar();
  bool sendComposerToActiveThread(const char* override_text);
  void markThreadsDirty(unsigned long delay_ms = 200);
  void markMsgsDirty(unsigned long delay_ms = 2000);
  void flushHistoryIfDue(unsigned long now);
  bool loadHistoryFromStorage();
  bool saveThreadsToStorage();
  bool saveMsgsToStorage();

  void setCurrScreen(UIScreen* c);

public:

  UITask(mesh::MainBoard* board, BaseSerialInterface* serial)
      : AbstractUITask(board, serial), _display(NULL), _sensors(NULL), _touch_screen(TouchUiScreen::Home) {
    next_batt_chck = _next_refresh = 0;
    ui_started_at = 0;
    curr = NULL;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  /** Diagnostic: update the top-layer touch badge (and force a repaint) so the
   *  user can see boot progress even if Arduino loop() hasn't started yet. */
  void setBootPhase(const char* label);

  void gotoHomeScreen() { _composer_mode = false; setCurrScreen(home); }
  void gotoChatsScreen() { setCurrScreen(chats); }
  void gotoChannelsScreen() { setCurrScreen(channels); }
  void gotoThreadScreen() { setCurrScreen(thread_view); }
  /** Legacy name: network UI lives under Settings tab on touch. */
  void gotoNetworkScreen() { setCurrScreen(settings); }
  void gotoSettingsScreen() { setCurrScreen(settings); }
  void showAlert(const char* text, int duration_millis);
  int  getMsgCount() const { return _msgcount; }
  int  getUnreadTotal() const;
  int  getUnreadMentionCount() const;   // # of threads with an unread @mention of me
  void markThreadRead(int idx);   // clear one thread's unread count (persisted)
  // Console mode: list threads with their unread counts (read-only), and clear
  // one deliberately. Kept separate so the monitor cannot mark anything read.
  int  consoleThreadAt(int idx, char* name, size_t cap, int* unread, bool* is_channel);
  bool consoleMarkThreadRead(const char* name);
  int  consoleHistoryAt(const char* thread, int back, char* sender, size_t sc,
                        char* text, size_t tc, uint32_t* ts, bool* outgoing);
  void markActiveThreadRead();    // clear the currently-open thread's unread (viewing == read)
  void markAllThreadsRead();      // clear every thread's unread count
  bool threadHasMention(int idx) const;   // unread @mention of me in this thread
  int  getThreadCount(bool channel_mode, int out_indexes[], int max_out) const;
  /** Inbox list: channels (any used) + DMs that have at least one stored message, sorted by recency. */
  int  getCombinedInboxCount(int out_indexes[], int max_out) const;
  bool threadHasMessageHistory(int thread_idx) const;
  bool getThreadInfo(int idx, bool& channel, uint16_t& unread, uint32_t& ts, char* name, size_t name_len) const;
  int  getActiveThreadMessageCount(int out_indexes[], int max_out, bool newest_first) const;
  bool getMessageByIndex(int msg_idx, UIMessage& out) const;
  int getNewestUnread(int slots[], int threads[], int capacity) const;
  bool isUnreadMessage(int slot) const;
  int  getThreadMessageIndexes(int thread_idx, int out_indexes[], int max_out, bool newest_first) const;   // read any thread's message ring slots (no active-thread side effect)
  bool deleteMessageBySlot(int msg_idx);          // tombstone one ring slot (long-press Delete)
  int  clearThreadHistory(int thread_idx);        // tombstone every message of a thread; returns count
  const char* getComposerBuffer() const { return _compose_buf; }
  bool isComposerMode() const { return _composer_mode; }
  int  getComposerCharIndex() const { return _composer_char_idx; }
  int  getComposerActionIndex() const { return _composer_action_idx; }
  void stepComposerChar(int delta);
  void stepComposerAction(int delta);
  void setComposerMode(bool enabled) { _composer_mode = enabled; }
  void composerReset() { resetComposer(); }
  void composerAppendChar(char c) { appendComposerChar(c); }
  void composerAppendText(const char* text) { appendComposerText(text); }
  void composerBackspace() { backspaceComposerChar(); }
  // override_text != nullptr resends that exact text (msg action menu "Resend"); nullptr = composer draft.
  bool composerSend(const char* override_text = nullptr) { return sendComposerToActiveThread(override_text); }
  void composerTypingMode() { _composer_action_idx = -1; }
  /** Open DM thread for mesh contact index (e.g. Chats thread list / external hooks). */
  void openMeshContactDm(uint32_t mesh_contact_index);
  /** Reset outbound path for active DM thread (maps to CMD_RESET_PATH companion use-case). */
  void resetActiveDmPath();
  /** LVGL: persist tab index and run light per-screen hooks. */
  void onLvTabChanged(int tab_index);
  void appendDiag(const char* message) override;
  TouchUiScreen touchScreen() const { return _touch_screen; }
  void enterThread(bool channel_mode, int idx);
  /** Block the sender of the active thread's tapped message: resolve to a pubkey
   *  (DM = the thread's contact; channel = sender-name → contact lookup) and add
   *  it to the persisted ignore list. False if no pubkey could be resolved. */
  bool ignoreSenderInActiveThread(const char* sender_name);
  bool hasActiveThread() const { return _chat.activeIndex() >= 0; }
  bool activeThreadIsChannel() const { return _chat.activeChannel(); }
  // Active channel's mesh slot (-1 when the open thread isn't a channel). For the
  // status-bar channel-settings gear (per-channel region scope).
  int16_t activeChannelSlot() const {
    return _chat.activeChannel() ? threadMeshChannelSlot(_chat.activeIndex()) : -1;
  }
  int  activeThreadIdx() const { return _chat.activeIndex(); }
  /** Any thread's pinned mesh-channel slot (-1 when not a channel / out of range).
   *  chatDeleteApply prefers this (name-validated) over a pure name scan so
   *  deleting a channel actually drops its mesh-table entry — otherwise
   *  refreshThreadsFromMesh() recreates the thread from the surviving channel. */
  int16_t threadMeshChannelSlot(int idx) const {
    const auto& thread = _messages.thread(idx);
    return thread.used && thread.channel ? thread.mesh_channel_slot : -1;
  }
  int msgCap() const { return _messages.capacity(); }
  bool getThreadContactPub(int idx, uint8_t out[32]) const {
    const auto& thread = _messages.thread(idx);
    if (!out || !thread.used || thread.channel) return false;
    uint8_t any = 0;
    for (int i = 0; i < 32; ++i) { out[i] = thread.mesh_contact_pub[i]; any |= out[i]; }
    return any != 0;
  }
  bool getThreadLastMessage(int idx, char* sender, size_t sender_cap,
                            char* text, size_t text_cap, bool* outgoing) const {
    UIMessage message{};
    if (!_messages.lastThreadMessage(idx, message)) return false;
    if (sender && sender_cap) { strncpy(sender, message.sender, sender_cap - 1); sender[sender_cap - 1] = 0; }
    if (text && text_cap) { strncpy(text, message.text, text_cap - 1); text[text_cap - 1] = 0; }
    if (outgoing) *outgoing = message.outgoing;
    return true;
  }
  int  threadScroll() const { return _thread_scroll; }
  void setThreadScroll(int v) { _thread_scroll = v; }
  bool hasDisplay() const { return _display != NULL; }
  int displayWidth() const { return _display ? _display->width() : 240; }
  int displayHeight() const { return _display ? _display->height() : 320; }
  bool isButtonPressed() const;

  bool isBuzzerQuiet() {
#ifdef PIN_BUZZER
    return buzzer.isQuiet();
#else
    // No piezo buzzer pin (touch boards). The touch UI plays I2S tones on the
    // T-Deck and gates them on the persisted buzzer_quiet pref, so reflect that
    // here rather than always reporting "quiet".
    return _node_prefs ? (_node_prefs->buzzer_quiet != 0) : true;
#endif
  }

  void toggleBuzzer();
  bool getGPSState();
  void toggleGPS();
#if defined(HAS_EXPANSION_KIT)
  // Heltec V4 Expansion Kit: snapshot of the locally-attached sensor rail
  // (battery, BME280, GXHTV3/SHT4X) plus the GPS/buzzer module presence.
  struct LocalEnvSnapshot {
    bool query_ok = false;
    bool have_batt = false;
    float batt_v = 0.0f;
    bool have_bme_temp = false;
    bool have_bme_hum = false;
    bool have_bme_pressure = false;
    bool have_bme_alt = false;
    float bme_temp_c = 0.0f;
    float bme_hum_pct = 0.0f;
    float bme_pressure_hpa = 0.0f;
    int16_t bme_alt_m = 0;
    bool have_gxhtv3_temp = false;
    bool have_gxhtv3_hum = false;
    float gxhtv3_temp_c = 0.0f;
    float gxhtv3_hum_pct = 0.0f;
    bool gps_present = false;
    bool gps_enabled = false;
    bool gps_fix = false;
    int  gps_sats = -1;
    bool buzzer_available = false;
    bool buzzer_quiet = true;
  };
  bool getLocalEnvSnapshot(LocalEnvSnapshot& out) const;
  bool getLocalEnvSummary(char* buf, size_t cap) const;
#endif
  /** True if the GPS currently reports a valid fix. */
  bool getGpsFix();
  /** Satellites currently in view, or -1 if unknown / no GPS hardware. */
  int  getGpsSats();
  /** UTC epoch the GPS itself has decoded, or 0 if it has none yet. A receiver decodes TIME from
   *  the satellite stream well before it can solve a POSITION, so a valid time with no fix is
   *  positive proof the module is alive and tracking, not dead. That is the distinction the GPS
   *  page could not previously show, and it is what "acquiring..." was hiding. */
  uint32_t getGpsTime();
  /** Altitude in metres from the last fix (0 when there is no fix). */
  int  getGpsAltitude();
  /** True once a valid fix has been seen this session. */
  bool getGpsHadFix() const { return _location.hadFix(); }
  double getNodeLat() const { return _sensors ? _sensors->node_lat : 0.0; }
  double getNodeLon() const { return _sensors ? _sensors->node_lon : 0.0; }
  const char* getNodeNameCstr() const { return (_node_prefs && _node_prefs->node_name[0]) ? _node_prefs->node_name : ""; }

  /** Settings: persist node display name (max 31 chars + NUL). */
  bool setNodeName(const char* s);
  /** Settings: persist advert lat/lon (stored with prefs). */
  bool setPosition(double lat, double lon);
  /** Settings: radio params; clamps to firmware limits, savePrefs, applies RF without reboot when possible. */
  bool setRadioParams(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr, int8_t tx_dbm, float airtime_factor);
  /** Bitmask uses same bits as firmware `AUTO_ADD_*` in MyMesh.cpp. */
  void setAutoAddConfig(uint8_t mask, uint8_t max_hops, uint8_t manual_add);
  void setAdvertLocationPolicy(uint8_t policy);
  void setPathHashMode(uint8_t mode);
  void setExperimentalFlags(uint8_t multi_acks, uint8_t client_repeat, uint8_t rx_boosted);
  void setTelemetryAllow(bool on);   // answer mesh telemetry requests (battery+env; location stays separate)
  void setLocationTelemetryMode(uint8_t mode);   // TELEM_MODE_* — position on request (#266)
  /** Meshcomod CLI on device: `wifi on` / `wifi off`. */
  bool setWifiRadio(bool on);
  bool isTcpEnabled() const { return _serial && _serial->isTcpEnabled(); }
  void enableTcp() { if (_serial) _serial->enableTcp(); }
  void disableTcp() { if (_serial) _serial->disableTcp(); }
  bool hasBleCapability() const { return _serial && _serial->hasBleCapability(); }
  bool isBleEnabled() const { return _serial && _serial->isBleEnabled(); }
  // Live BLE enable. The concrete transport applies its heap guard only for a
  // cold NimBLE allocation; re-enabling a pre-created stack is allocation-free.
  // Returns false when a required cold start cannot be made safely.
  bool enableBle();
  void disableBle() { if (_serial) _serial->disableBle(); }
  // The companion transport, for board code that knows its concrete type.
  BaseSerialInterface* serialInterface() const { return _serial; }
  int getWsConnectedCount() const { return _serial ? _serial->getWsConnectedCount() : 0; }
  /** Push the ESP32 system clock into the mesh RTC. false = never synced, mesh clock untouched. */
  bool setDeviceTimeFromSystemClock();
  /** Mark recent user input — call when touch / hw button is detected. */
  void noteUserInput();
  /** Get / set the screen-off-after-idle timeout (0 = never). Persists in NVS. */
  uint16_t getScreenTimeoutSecs() const;
  bool setScreenTimeoutSecs(uint16_t seconds);
  /** Force the panel back on and update the activity deadline. */
  void wakeScreen();
  /** Turn the panel off and manually lock it (touch ignored until a deliberate
   *  unlock — a trackball/BOOT-button press). Same state the V4 lock button sets. */
  void lockScreen();
  /** Reveal the lock screen (light the panel) without unlocking — for a key /
   *  trackball press while hard-locked. No-op unless manually locked. */
  void lockscreenReveal();
  /** Release a manual lock: hide the lock screen and turn the panel back on. */
  void unlockScreen();
  /** True while the panel backlight is off (idle-dimmed or manually locked). */
  bool isScreenOff() const { return _screen.screenOff(); }
  bool isManualLocked() const { return _screen.manualLocked(); }   // hard screen lock engaged
  void toggleScreenLock();                                // Tanmatsu Vol- long-press: lock <-> unlock
  void sleepScreen();                                     // soft screen sleep (backlight off, not locked)
  /** True while hard-locked (manual lock engaged), whether lit or dark. */
  bool isManualLock() const { return _screen.manualLocked(); }
  bool sendAdvertNow();         // legacy: zero-hop
  bool sendAdvertFlood();       // multi-hop flood
  bool sendAdvertZeroHop();     // explicit zero-hop (same as sendAdvertNow)
  bool sendSignalProbe();       // trace-ping the nearest repeater (non-flooding); falls back to zero-hop advert
  void rebootDevice();
  // Synchronously persist chat history to flash. Call before any path that
  // restarts the device (Wi-Fi/BLE mode switch, etc.) so recent chat isn't
  // lost — the periodic flush is off-thread and rate-capped. Overrides the
  // AbstractUITask hook so the companion CMD_REBOOT path flushes too.
  void persistHistoryNow() override;
  void flushHistorySoon();   // arm an immediate OFF-THREAD flush (worker) — no min-delay clamp, no UI stall

  // from AbstractUITask
  void msgRead(int msgcount) override;
  void newMsgFromPub(uint8_t path_len, const uint8_t* from_pub, const char* from_name, const char* text, int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override;
  // Mesh reception owns the complete event, including notification eligibility.
  void receiveMessage(const UIMessageEvent& event) override;
  // Compatibility adapters for callers supplying only hops/SNR/RSSI.
  void newMsgFromPubWithMeta(uint8_t path_len, bool is_flood,
                              const uint8_t* from_pub, const char* from_name,
                              const char* text, int msgcount,
                              int8_t snr_q4, int8_t rssi) override;
  void newRoomMsgFromPubWithMeta(uint8_t path_len, bool is_flood,
                                 const uint8_t* from_pub, const char* from_name,
                                 const char* author_name,
                                 const char* text, int msgcount,
                                 int8_t snr_q4, int8_t rssi) override;
  void appSentMsgToContact(const uint8_t* to_pub, const char* to_name, const char* text,
                           uint32_t ack_hash, uint32_t sent_fp = 0) override;
  void appSentMsgToChannel(const char* channel_name, const char* text, uint32_t sent_fp = 0) override;
  void notify(UIEventType t = UIEventType::none) override;
  void logRxFrame(float snr, float rssi, const uint8_t* raw, int len) override;
  void discoveredContact(const ContactInfo& contact, bool is_new, uint8_t path_len) override;
  void onPingReply(const ContactInfo& contact, const uint8_t* data, size_t len) override;
  void onTelemetryReply(const ContactInfo& contact, const uint8_t* data, size_t len) override;
  void onAdminLoginResult(const ContactInfo& contact, bool success, uint8_t perms) override;
  void onServerClock(const ContactInfo& contact, uint32_t server_epoch) override;
  void onAdminCommandReply(const ContactInfo& contact, const char* text) override;
  void onThreadsChanged() override;
  void loop() override;

  void shutdown(bool restart = false);
};

// True while the touch-UI "Spectrum" RF-analyzer app owns the radio. main.cpp's
// loop() checks this and SKIPS the_mesh.loop() while it's true, so the mesh never
// re-tunes / re-arms RX on the home channel while the analyzer sweeps the band.
// Defined in UITask.cpp (returns a static flag set on open / cleared after the
// radio is restored to the mesh config on close).
bool spectrumOwnsRadio();

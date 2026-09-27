// SPDX-License-Identifier: GPL-3.0-or-later
#include "LuaIntegration.h"
#include "../platform/UiDevice.h"
#include "../services/AudioService.h"
#include "../theme/Fonts.h"
using namespace ui::theme;
namespace { ui::lua::Host luaHost{}; UITask* luaTask() { return luaHost.task ? luaHost.task() : nullptr; } }
void ui::lua::configure(Host host) { luaHost = host; }
#if CAP_LUA_APPS
// ---- Lua app host bridges (LuaAppHost.cpp externs) ----
// The host lives in its own TU; these three shims are its only view of UITask.
// A Lua app's timer keeps firing while the screen is off — lv_timer_handler runs
// unconditionally — so an app polling a sensor at 10 Hz went on doing it into a
// dark screen. Nothing it draws can be seen, so the host skips the tick; the app
// resumes on wake and sees the gap through sys.millis() like any other pause.
static bool s_lua_keep_awake = false;
// wada.sys.keep_awake(true): an app that is MEASURING rather than displaying —
// a calibration sweep, a timed capture — must keep running and must not have
// the screen blank underneath it. Pausing app ticks with the screen (below)
// truncated exactly such a sweep: the M9's default screen timeout is 20 s and
// GPS Compass calibrates for 20 s, so collection stopped at the same instant
// the countdown was supposed to end. This keeps BOTH alive, and is cleared
// when the app closes so it cannot leak into the next one.
void luaHostKeepAwake(bool on) {
  s_lua_keep_awake = on;
  if (on && luaTask()) luaTask()->noteUserInput();   // push the idle timer out now
}
bool luaHostScreenOn() {
  if (s_lua_keep_awake) {
    if (luaTask()) luaTask()->noteUserInput();       // and keep pushing it, tick by tick
    return true;
  }
  return luaTask() ? !luaTask()->isScreenOff() : true;
}

const lv_font_t* luaHostFontForSize(int size_class) {
  return size_class <= 12 ? &font12() : size_class <= 14 ? &font14() : &font16();
}
// Lua wada.sys.beep() (#245). Routes to the SAME per-board chime the UI uses, so
// a Lua app cannot be louder or different from the rest of the firmware, and it
// honours the user's sound setting rather than overriding it. Returns whether a
// sound was actually produced: boards without a sounder, and a muted device,
// both report false so an app can show something instead.
bool luaHostBeep() {
  if (!luaTask() || luaTask()->isBuzzerQuiet()) return false;
  ui::audio::playSlot(TOUCH_SND_MSG);
  return true;
}

void luaHostToast(const char* msg, int ms) {
  if (luaTask()) luaTask()->showAlert(msg, ms);
}
fs::FS* luaHostAppFs() { return luaHost.filesystem ? luaHost.filesystem() : nullptr; }
void luaHostAppPath(char* out, size_t cap, const char* rel) {
  if (luaHost.path) luaHost.path(out, cap, rel); else if (cap) out[0] = 0;   // SD-rooted stores prefix /meshcomod
}


#if CAP_LUA_SD_LIST
fs::FS* luaHostSdFs(bool* busy) { return luaHost.sdFilesystem ? luaHost.sdFilesystem(busy) : nullptr; }
bool luaHostSdReadFailed() { return luaHost.sdReadFailed && luaHost.sdReadFailed(); }
bool luaHostSdClearAttributes(const char* path) { return luaHost.sdClearAttributes && luaHost.sdClearAttributes(path); }
#endif
// ---- wada.mesh read-only bridges (no mesh types cross into the host TU) ----
// Hex-encode the first n bytes of a public key. 4 bytes (8 hex chars) is what the
// rest of the UI uses to name a node, and it is what an app needs to line up a
// contact with a discovery hit or an overheard advert.
static void luaPubkeyHex(const uint8_t* pk, int n, char* out, size_t cap) {
  static const char* H = "0123456789abcdef";
  size_t o = 0;
  for (int i = 0; i < n && o + 2 < cap; i++) {
    out[o++] = H[(pk[i] >> 4) & 0x0F];
    out[o++] = H[pk[i] & 0x0F];
  }
  if (cap) out[o < cap ? o : cap - 1] = '\0';
}

int luaHostContactAt(int idx, char* name, size_t name_cap, int* type, uint32_t* secs_ago,
                     double* lat, double* lon, char* pk_hex, size_t pk_cap,
                     int32_t* lat_e6, int32_t* lon_e6) {
  ContactInfo ci;
  if (idx < 0 || !the_mesh.getContactByIdx((uint32_t)idx, ci)) return 0;
  snprintf(name, name_cap, "%s", ci.name);
  *type = ci.type;
  if (pk_hex && pk_cap) luaPubkeyHex(ci.id.pub_key, 4, pk_hex, pk_cap);
  *lat = ci.gps_lat / 1.0e6;
  *lon = ci.gps_lon / 1.0e6;
  if (lat_e6) *lat_e6 = ci.gps_lat;   // stored as micro-degrees already
  if (lon_e6) *lon_e6 = ci.gps_lon;
  // One clock read per contacts() walk, not one per contact: on boards whose
  // clock is an I2C RTC (the M9's PCF8563 on Wire) getCurrentTime() is a bus
  // transaction, and an app listing 100 contacts would otherwise stall the UI
  // loop for ~100 ms each call. meshContacts() always walks from idx 0.
  static uint32_t s_walk_now = 0;
  if (idx == 0) s_walk_now = the_mesh.getRTCClock() ? the_mesh.getRTCClock()->getCurrentTime() : 0;
  const uint32_t now = s_walk_now;
  *secs_ago = (ci.last_advert_timestamp && now > ci.last_advert_timestamp)
                  ? now - ci.last_advert_timestamp : 0;
  return 1;
}
int luaHostRxLogAt(int idx, uint32_t* ms_ago, int* ptype, int* rssi, float* snr, int* hops,
                   int* route, int* len, int* org_kind, char* org_hex, size_t org_cap,
                   uint32_t* at_ms) {
  MyMesh::UiRxRec r;
  if (idx < 0 || !the_mesh.uiRxLogGet((uint8_t)idx, r)) return 0;
  if (at_ms) *at_ms = r.ms;   // the record's own timestamp: a stable delivery key
  *ms_ago = millis() - r.ms;
  *ptype  = r.ptype;
  *rssi   = r.rssi;
  *snr    = (float)r.snr_q4 / 4.0f;
  *hops   = r.hops;
  *route  = r.route;
  *len    = r.len;
  *org_kind = r.org_kind;
  // kind 1 = a real public-key prefix (advert); kind 2 = the one-byte
  // destination and source hashes an addressed frame carries. Encoded to the
  // width the frame actually justifies, so 8 hex chars always means a key.
  if (org_hex && org_cap) luaPubkeyHex(r.org, r.org_kind == 1 ? 4 : 2, org_hex, org_cap);
  return 1;
}
void luaHostRadioStats(float* rssi, float* noise, uint32_t* rx_air_s, uint32_t* tx_air_s,
                       uint32_t* rx_pkts, uint32_t* rx_err, int* budget_ms) {
  *rssi     = radio_driver.getCurrentRSSI();
  *noise    = radio_driver.getNoiseFloor();
  *rx_air_s = (uint32_t)(the_mesh.getReceiveAirTime() / 1000UL);
  *tx_air_s = (uint32_t)(the_mesh.getTotalAirTime() / 1000UL);
  *rx_pkts  = radio_driver.getPacketsRecv();
  *rx_err   = radio_driver.getPacketsRecvErrors();
  *budget_ms = (int)the_mesh.getRemainingTxBudget();
}
void luaHostRadioStats2(uint32_t* rx_evt, uint32_t* rx_drop, uint32_t* tx_pkts,
                        float* freq, float* bw, int* sf, int* duty_pct) {
  *rx_evt  = radio_driver.getRxEvents();
  *rx_drop = radio_driver.getRxQueueDrops();
  *tx_pkts = radio_driver.getPacketsSent();
  NodePrefs* p = the_mesh.getNodePrefs();
  *freq = p->freq;
  *bw   = p->bw;
  *sf   = p->sf;
  // airtime_factor -> the duty ceiling the dispatcher enforces (see #161)
  *duty_pct = p->airtime_factor > 0 ? (int)(100.0f / (1.0f + p->airtime_factor) + 0.5f) : 100;
}
// Widened for console mode: CAP_LUA_SDK_EXT is OFF on the V4, which is exactly
// the board console mode exists for, and the console needs the same send path.
// Reusing it rather than duplicating matters most for luaHostMeshSendChannel:
// it matches the channel BY NAME at transmit time, and a cached slot index is
// how messages went out encrypted to the wrong channel before.
#if CAP_LUA_SDK_EXT || CAP_CONSOLE
// ---- Lua app permissions: mesh send ----------------------------------------
// wada.mesh.send() is the first WRITE path a store app has into the mesh, and
// anything it sends goes out under the user's own node name -- to readers it is
// indistinguishable from the user typing it. That is a different category from
// an app writing a file, so it needs explicit, informed, per-app consent.
//
// The grant is bound to ONE app id. Approving a beacon app grants nothing to any
// other app, and re-installing under a different id asks again. Stored as a tiny
// text file next to the app data so it survives reboots and can be inspected.
//
// The app cannot bypass this: it never sees a prompt API. wada.mesh.send simply
// returns false until a grant exists, and the FIRST refusal raises the prompt.
// Read by the confirm callback. Only one app runs at a time (apps are
// full-screen) and only one dialog can be open, so a single slot is enough.
static char s_lua_perm_target[24] = {0};

static void luaPermPath(char* out, size_t cap) { luaHostAppPath(out, cap, "/apps/perms.kv"); }

// Permission bits. The on-disk value is this mask as a decimal, which keeps every
// file written before read permission existed valid: "=1" already meant send.
//   0 = asked, nothing allowed    1 = send    2 = read    3 = both
// Entry PRESENCE is what records "we asked" -- absent is not the same as refused,
// and the settings page shows that difference.
int luaHostAppPerms(const char* app_id, bool* asked) {
  if (asked) *asked = false;
  if (!app_id || !*app_id || !luaHostAppFs()) return 0;
  char path[80]; luaPermPath(path, sizeof path);
  File f = luaHostAppFs()->open(path, "r");
  if (!f) return 0;
  int mask = 0;
  char line[64];
  while (f.available()) {
    const size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = '\0';
    char* eq = strchr(line, '=');
    if (!eq) continue;
    *eq = '\0';
    if (strcmp(line, app_id) == 0) {
      if (asked) *asked = true;
      mask = atoi(eq + 1);
      break;
    }
  }
  f.close();
  return mask;
}

// 1 = granted, -1 = refused, 0 = never asked.
int luaHostSendPerm(const char* app_id) {
  bool asked = false;
  const int mask = luaHostAppPerms(app_id, &asked);
  if (!asked) return 0;
  return (mask & LUA_PERM_SEND) ? 1 : -1;
}
// True when the app may be shown incoming messages. No prompt is raised from
// here: unlike send, this fires on someone ELSE's traffic arriving, so a dialog
// would appear unbidden. An app asks for it in the settings page instead.
bool luaHostReadPerm(const char* app_id) {
  bool asked = false;
  return (luaHostAppPerms(app_id, &asked) & LUA_PERM_READ) != 0;
}

// Private conversations are their own grants. Posting to a channel the user is
// already in is a different act from writing to one person as them, and reading
// channel traffic is different from reading their DMs — so an app that needs one
// does not silently acquire the other.
int luaHostDmSendPerm(const char* app_id) {
  bool asked = false;
  const int mask = luaHostAppPerms(app_id, &asked);
  if (!asked) return 0;
  return (mask & LUA_PERM_DM_SEND) ? 1 : -1;
}
bool luaHostDmReadPerm(const char* app_id) {
  bool asked = false;
  return (luaHostAppPerms(app_id, &asked) & LUA_PERM_DM_READ) != 0;
}

// Set or clear ONE permission bit, leaving the app's other grants alone. The
// prompt path used to write a whole mask, so asking for send silently revoked a
// read grant the user had given in Settings — harmless with one bit, wrong the
// moment there is more than one.
static void luaPermWrite(const char* app_id, int mask);
static void luaPermSetBit(const char* app_id, int bit, bool on) {
  bool asked = false;
  int mask = luaHostAppPerms(app_id, &asked);
  if (on) mask |= bit; else mask &= ~bit;
  luaPermWrite(app_id, mask);
}

static void luaPermWrite(const char* app_id, int mask) {
  if (!app_id || !*app_id || !luaHostAppFs()) return;
  // Rewrite whole-file: the table is a handful of short lines, and an in-place
  // edit would have to deal with a shrinking record.
  char path[80]; luaPermPath(path, sizeof path);
  String keep;
  File in = luaHostAppFs()->open(path, "r");
  if (in) {
    char line[64];
    while (in.available()) {
      const size_t n = in.readBytesUntil('\n', line, sizeof(line) - 1);
      line[n] = '\0';
      if (!line[0]) continue;
      char* eq = strchr(line, '=');
      if (eq) { *eq = '\0'; const bool same = (strcmp(line, app_id) == 0); *eq = '='; if (same) continue; }
      keep += line; keep += '\n';
    }
    in.close();
  }
  char rec[40];
  snprintf(rec, sizeof rec, "%s=%d\n", app_id, mask);
  keep += rec;
  WdtHeavyGuard guard;                       // small, but it is still a flash write
  char appsdir[80]; luaHostAppPath(appsdir, sizeof appsdir, "/apps");
  luaHostAppFs()->mkdir(appsdir);
  File out = luaHostAppFs()->open(path, "w");
  if (!out) return;
  out.write((const uint8_t*)keep.c_str(), keep.length());
  out.close();
}

// Raised from wada.mesh.send's first refusal. Deliberately blunt about what is
// being granted -- "send messages" understates it; the point the user needs is
// that the messages carry THEIR name.
void luaHostRequestSendPerm(const char* app_id, const char* app_name) {
  if (!app_id || !*app_id) return;
  snprintf(s_lua_perm_target, sizeof s_lua_perm_target, "%s", app_id);
  // Record the refusal FIRST, so the answer is "no" unless the user says
  // otherwise -- a power cut mid-dialog must not leave a grant. It also stops
  // this app re-prompting: its next send sees -1 and fails without a dialog.
  luaPermSetBit(app_id, LUA_PERM_SEND, false);   // refused unless the user says otherwise
  const char* label = (app_name && *app_name) ? app_name : app_id;
  char msg[220];
  snprintf(msg, sizeof msg,
           TR("\"%s\" wants to send messages on the mesh.\n\n"
              "They will be sent as %s and cannot be told apart from messages you typed.\n\n"
              "Only allow this for an app you trust."),
           label, the_mesh.getNodePrefs()->node_name);
  // Cancel needs no handler: the refusal is already on disk. The user can revisit
  // a decision by deleting /apps/perms.kv.
  luaHost.confirm(msg, TR("Allow"), +[]() { luaPermSetBit(s_lua_perm_target, LUA_PERM_SEND, true); });
}

// Send on a channel matched BY NAME. Matching by name rather than a cached slot
// index is deliberate: a stale slot transmits on the WRONG key, which is exactly
// the bug behind the channel-send fix in the touch composer.
// Send a direct message to a contact BY NAME, or post to a room server (a room is
// just a contact of type ADV_TYPE_ROOM, and the send is identical — which is why one
// call covers both). Mirrors into the on-device chat thread and registers the expected
// ACK exactly as a composer send does, so an app-sent message is a first-class message
// rather than something that vanishes off the radio with no local trace.
bool luaHostMeshSendDM(const char* to_name, const char* text, bool* was_room) {
  if (was_room) *was_room = false;
  if (!to_name || !*to_name || !text || !*text) return false;
  ContactInfo* by = nullptr;
  const uint32_t n = the_mesh.getNumContacts();
  for (uint32_t i = 0; i < n; ++i) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(i, c)) continue;
    if (strcmp(c.name, to_name) != 0) continue;
    by = the_mesh.lookupContactByPubKey(c.id.pub_key, PUB_KEY_SIZE);
    break;
  }
  if (!by) return false;                      // no contact with that name
  if (was_room) *was_room = (by->type == ADV_TYPE_ROOM);

  ContactInfo rcpt = *by;
  rcpt.out_path_len = OUT_PATH_UNKNOWN;       // flood, same as the Chats composer does
  const uint32_t ts = the_mesh.getRTCClock()->getCurrentTimeUnique();
  uint32_t expected_ack = 0, est_timeout = 0;
  const int rr = the_mesh.sendMessage(rcpt, ts, 0, (char*)text, expected_ack, est_timeout);
  if (rr == MSG_SEND_FAILED) return false;
  the_mesh.uiRegisterExpectedAck(expected_ack, by->id.pub_key);
  if (luaTask())
    luaTask()->appSentMsgToContact(by->id.pub_key, by->name, text, expected_ack, the_mesh.uiLastSentFp());
  return true;
}

// Names of the channels configured on this device, so an app can discover the
// private ones rather than having to be told their names. Names only: the channel
// SECRET is never exposed to Lua, so an app can post to a channel the user already
// has but can never derive one or hand it to anybody.
int luaHostMeshChannelNames(char out[][32], int max_n) {
  int n = 0;
  for (int i = 0; i < MAX_GROUP_CHANNELS && n < max_n; i++) {
    ChannelDetails cd;
    if (!the_mesh.getChannel(i, cd) || !cd.name[0]) continue;
    snprintf(out[n], 32, "%s", cd.name);
    n++;
  }
  return n;
}

// Raised from wada.mesh.send_dm's first refusal. Worded harder than the channel
// prompt on purpose: a channel post is visible to everyone on that channel and is
// obviously "from" the node, while a direct message arrives in one person's private
// thread looking exactly like something the user typed to them.
void luaHostRequestDmSendPerm(const char* app_id, const char* app_name) {
  if (!app_id || !*app_id) return;
  snprintf(s_lua_perm_target, sizeof s_lua_perm_target, "%s", app_id);
  luaPermSetBit(app_id, LUA_PERM_DM_SEND, false);   // refused unless the user says otherwise
  const char* label = (app_name && *app_name) ? app_name : app_id;
  char msg[240];
  snprintf(msg, sizeof msg,
           TR("\"%s\" wants to send PRIVATE messages as you.\n\n"
              "It could write to any of your contacts, or post to a room, and the "
              "message will look exactly like one you typed.\n\n"
              "Only allow this for an app you trust."),
           label);
  luaHost.confirm(msg, TR("Allow"), +[]() { luaPermSetBit(s_lua_perm_target, LUA_PERM_DM_SEND, true); });
}

bool luaHostMeshSendChannel(const char* chan_name, const char* text) {
  if (!chan_name || !*chan_name || !text || !*text) return false;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails cd;
    if (!the_mesh.getChannel(i, cd) || !cd.name[0]) continue;
    if (strcmp(cd.name, chan_name) != 0) continue;
    uint32_t ts = the_mesh.getRTCClock()->getCurrentTimeUnique();
    const char* sender = the_mesh.getNodePrefs()->node_name;
    if (!the_mesh.sendGroupMessage(ts, cd.channel, sender, (char*)text, (int)strlen(text))) return false;
    if (luaTask()) luaTask()->appSentMsgToChannel(cd.name, text, the_mesh.uiLastSentFp());
    return true;
  }
  return false;   // no such channel on this device
}
// Repeat tracking for a message an app sent. The firmware already counts how
// many repeaters were heard rebroadcasting an outgoing flood -- it is the
// refresh glyph on a sent bubble -- keyed on a fingerprint taken at transmit
// time. These two expose that to the SDK: the send returns the fingerprint, and
// the app reads the count back whenever it likes. A DM has no meaningful count,
// since it is not flooded.
// The height an AppPage's title bar ACTUALLY occupies. The Lua host had this as
// a hardcoded 44, which is right only where STATUSBAR_H is its 22 default: it is
// SB_TOP_PAD + SB_ROW*2 on the round-corner phone panel, and SC(22) everywhere
// else, so it grows with the UI scale. Wherever it was taller than 44 the app's
// first line was drawn under the bar (#236 on the T-Display P4, and the same on
// any board at Large or Huge scale, which the Tanmatsu uses by default).
lv_coord_t luaHostAppBarH() { return luaHost.barHeight(); }

uint32_t luaHostLastSentFp() { return the_mesh.uiLastSentFp(); }
uint8_t  luaHostRepeatsForFp(uint32_t fp) { return fp ? the_mesh.uiRepeatsForFp(fp) : 0; }
#endif  // CAP_LUA_SDK_EXT || CAP_CONSOLE

#if CAP_LUA_SDK_EXT || CAP_CONSOLE   // console mode needs it on the V4 too
// wada.sys.battery(). Reads the SMOOTHED millivolts the status bar uses, not a
// raw ADC sample, so a Lua chart cannot show noise the rest of the UI hides.
void luaHostBattery(uint16_t* mv, int* pct, bool* charging) {
  if (luaHost.battery) luaHost.battery(mv, pct, charging);
}
// wada.sys.gps(). False when there is no fix -- the caller then gets nil rather
// than the last known position, which wada.mesh.self() already provides. An app
// plotting a track needs to tell a live fix from a stale one. Also false while
// the user has GPS switched off (same gate updateGpsLocation() applies), so a
// last sentence parsed before the toggle cannot masquerade as a live fix.
// Altitude (getGpsAltitude(), metres, the accessor the Device page uses) is
// included because height dominates LoRa range: two samples a metre apart on
// the map can be a hilltop and a hollow, and a coverage survey that records
// only lat/lon cannot tell them apart afterwards. `time` is satellite time
// (0 until the receiver has decoded the date), which is the only clock a
// logger can trust before the device has been near a network.
// Speed/course exist only where the board's target.cpp provides wadaGpsMotion()
// on top of WadaNmeaLocationProvider (HAS_GPS_MOTION); the core provider keeps
// its RMC fields private, so other boards report NAN and the binding omits the
// fields.
#if defined(HAS_GPS_MOTION)
extern bool wadaGpsMotion(float* speed_kmh, float* course_deg);
#endif
bool luaHostGps(double* lat, double* lon, int* sats, int* alt_m, uint32_t* fix_time,
                int32_t* lat_e6, int32_t* lon_e6, float* speed_kmh, float* course_deg) {
  // Pre-set the motion fields BEFORE the fix gate: a caller that bails on a
  // false return still reads defined values, and boards without HAS_GPS_MOTION
  // report NAN so the binding can omit the fields rather than publish a 0.
  if (speed_kmh)  *speed_kmh  = NAN;
  if (course_deg) *course_deg = NAN;
  if (!luaTask() || !luaTask()->getGPSState() || !luaTask()->getGpsFix()) return false;
  const double la = luaTask()->getNodeLat(), lo = luaTask()->getNodeLon();
  if (lat)  *lat  = la;
  if (lon)  *lon  = lo;
  if (sats) *sats = luaTask()->getGpsSats();
  if (alt_m)    *alt_m    = luaTask()->getGpsAltitude();
  if (fix_time) *fix_time = luaTask()->getGpsTime();
  // Lua is built LUA_32BITS, so its floats are SINGLE precision: a latitude
  // keeps roughly a metre. Fine to display, not fine to log -- so the exact
  // micro-degrees go across as integers too, which int32 holds without loss
  // (180e6 is well inside its range). A track logger should write these.
  if (lat_e6) *lat_e6 = (int32_t)llround(la * 1.0e6);
  if (lon_e6) *lon_e6 = (int32_t)llround(lo * 1.0e6);
#if defined(HAS_GPS_MOTION)
  {
    float spd = NAN, crs = NAN;
    if (wadaGpsMotion(&spd, &crs)) {
      if (speed_kmh)  *speed_kmh  = spd;
      if (course_deg) *course_deg = crs;
    }
  }
#endif
  return true;
}
#endif  // CAP_LUA_SDK_EXT || CAP_CONSOLE

#if CAP_COMPASS
// wada.sys.compass(). Raw field vector from the board's magnetometer; the only
// one wired so far is the M9's QMC6309 (variants/thinknode_m9/M9Compass.*).
// Runs on the UI thread like every other luaHost* bridge; the driver's I2C
// reads are short and the bus (Wire) has no other task on it.
#if CAP_IMU
// wada.sys.accel(). Acceleration in g, sensor frame, as the chip reports it —
// the mapping to the device's own axes is the consumer's, for the same reason
// as the magnetometer: it is not documented for this board and had to be
// measured.
bool luaHostAccel(float* x, float* y, float* z) {
#if defined(HAS_M9_IMU)
  return m9ImuRead(x, y, z);
#else
  (void)x; (void)y; (void)z;
  return false;
#endif
}
#endif  // CAP_IMU

bool luaHostCompass(float* x, float* y, float* z, bool* overflow) {
#if defined(HAS_M9_COMPASS)
  return m9CompassRead(x, y, z, overflow);
#else
  (void)x; (void)y; (void)z; (void)overflow;
  return false;
#endif
}
#endif  // CAP_COMPASS

#if CAP_LUA_SDK_EXT || CAP_CONSOLE   // console mode needs it on the V4 too

void ui::lua::writePermissions(const char* id, int mask) { luaPermWrite(id, mask); }
int ui::lua::permissionIds(char (*ids)[24], int capacity) {
  if (!ids || capacity <= 0) return 0;
  int count = 0;
  if (luaHost.installedId) for (int i = 0; count < capacity; ++i) {
    const char* id = luaHost.installedId(i);
    if (!id) break;
    snprintf(ids[count++], 24, "%s", id);
  }
  fs::FS* fs = luaHostAppFs();
  if (!fs) return count;
  char path[80]; luaPermPath(path, sizeof path);
  File file = fs->open(path, "r");
  if (!file) return count;
  char line[64];
  while (file.available() && count < capacity) {
    const size_t got = file.readBytesUntil('\n', line, sizeof(line) - 1);
    line[got] = 0;
    char* eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    bool duplicate = false;
    for (int i = 0; i < count; ++i) if (!strcmp(ids[i], line)) { duplicate = true; break; }
    if (!duplicate) snprintf(ids[count++], 24, "%s", line);
  }
  file.close();
  return count;
}

// ---- wada.mesh.discover: the active half of discovery -----------------------
// A probe is a zero-hop broadcast that asks every node in earshot to answer, so
// one call costs airtime across the whole neighbourhood, not just ours. That is
// also exactly what makes it worth having: a reply PROVES the link works from
// where you are standing, which no amount of listening can establish.
//
// Its own permission rather than reusing "send": posting a message and making
// every neighbour transmit are different impositions on other people's radios,
// and a survey app has no business acquiring the ability to speak as the user.
int luaHostProbePerm(const char* app_id) {
  bool asked = false;
  const int mask = luaHostAppPerms(app_id, &asked);
  if (!asked) return 0;
  return (mask & LUA_PERM_PROBE) ? 1 : -1;
}
void luaHostRequestProbePerm(const char* app_id, const char* app_name) {
  if (!app_id || !*app_id) return;
  snprintf(s_lua_perm_target, sizeof s_lua_perm_target, "%s", app_id);
  luaPermSetBit(app_id, LUA_PERM_PROBE, false);   // refused unless the user says otherwise
  const char* label = (app_name && *app_name) ? app_name : app_id;
  char msg[240];
  snprintf(msg, sizeof msg,
           TR("\"%s\" wants to send discovery probes.\n\n"
              "Each probe asks every node in range to reply, so it uses airtime "
              "on the whole local mesh, not just yours.\n\n"
              "Nothing is sent under your name and no message is transmitted."),
           label);
  luaHost.confirm(msg, TR("Allow"), +[]() { luaPermSetBit(s_lua_perm_target, LUA_PERM_PROBE, true); });
}
// type_filter is a bitmask over ADV_TYPE_*; 0 means every node type.
// Returns the scan tag (non-zero) if the probe went out.
uint32_t luaHostMeshDiscover(int type_filter) {
  return the_mesh.uiStartDiscoverScan((uint8_t)(type_filter & 0xFF));
}
#endif  // CAP_LUA_SDK_EXT || CAP_CONSOLE

void luaHostSelfInfo(char* name, size_t name_cap, double* lat, double* lon,
                    char* pk_hex, size_t pk_cap, int32_t* lat_e6, int32_t* lon_e6) {
  snprintf(name, name_cap, "%s", the_mesh.getNodePrefs()->node_name);
  *lat = luaTask() ? luaTask()->getNodeLat() : 0.0;
  *lon = luaTask() ? luaTask()->getNodeLon() : 0.0;
  if (lat_e6) *lat_e6 = (int32_t)llround(*lat * 1.0e6);
  if (lon_e6) *lon_e6 = (int32_t)llround(*lon * 1.0e6);
  if (pk_hex && pk_cap) luaPubkeyHex(the_mesh.self_id.pub_key, 4, pk_hex, pk_cap);
}

// ---- Discovery results (read side) -----------------------------------------
// The table the Discover app fills. Reading it is passive -- these are replies
// the radio already received -- so it is ungated, like contacts() and rx_log().
// FIRING a probe is the gated half; see luaHostMeshDiscover below.
int luaHostDiscoverCount() { return the_mesh.discoverCount(); }
void luaHostDiscoverClear() { the_mesh.discoverClear(); }
int luaHostDiscoverAt(int idx, char* pk_hex, size_t pk_cap, char* name, size_t name_cap,
                      int* type, int* rssi, float* snr, float* their_snr, int* hops,
                      uint32_t* first_ms_ago, uint32_t* last_ms_ago, int* heard) {
  MyMesh::DiscoverHit h;
  if (idx < 0 || !the_mesh.discoverGet((uint8_t)idx, h)) return 0;
  luaPubkeyHex(h.pubkey, 4, pk_hex, pk_cap);
  // A responder is only named if the user already has it as a contact. A
  // discovery reply carries no name of its own, and inventing one would let an
  // app present an unknown node as a known one.
  ContactInfo* c = the_mesh.lookupContactByPubKey(h.pubkey, PUB_KEY_SIZE);
  snprintf(name, name_cap, "%s", c ? c->name : "");
  *type      = h.node_type;
  *rssi      = h.our_rssi;
  *snr       = (float)h.our_snr_q4 / 4.0f;
  // The reverse link: how well THEY heard US. This is the half a wardriving
  // survey cannot get any other way, and it is why a probe beats listening.
  *their_snr = (float)h.their_snr_q4 / 4.0f;
  *hops      = h.path_len;
  const uint32_t now = millis();
  *first_ms_ago = now - h.first_ms;
  *last_ms_ago  = now - h.last_ms;
  *heard        = h.heard;
  return 1;
}

// ---- wada.ui.input: one modal text field, reusing the file-manager prompt ----
// Exactly one callback per call: the text on OK, nullptr on any other close, so
// an app can always re-enable itself.
static void (*s_lua_prompt_cb)(const char*) = nullptr;
static void luaPromptDeliver(const char* text) {
  void (*cb)(const char*) = s_lua_prompt_cb;
  s_lua_prompt_cb = nullptr;
  if (cb) cb(text);
}
void luaHostTextPromptCancel() {          // closed some other way: report a cancel
  void (*cb)(const char*) = s_lua_prompt_cb;
  s_lua_prompt_cb = nullptr;
  if (cb) cb(nullptr);
}
#if CAP_SENSORS
// wada.sys.env(). Reports per-field presence rather than a value with a magic
// "missing" number, so an app can distinguish an absent sensor from a real
// reading of zero. The snapshot is the same one the Sensors tab renders.
void luaHostEnv(bool* ok, bool* have_t, float* temp_c, bool* have_h, float* hum_pct,
                bool* have_p, float* press_hpa, bool* have_alt, int* alt_m) {
  *ok = false;
  if (!luaTask()) return;
  UITask::LocalEnvSnapshot snap;
  if (!luaTask()->getLocalEnvSnapshot(snap) || !snap.query_ok) return;
  // Two possible sources for temperature and humidity: the BME280 on the
  // Expansion Kit and the GXHTC3 on the board. Prefer the BME, fall back.
  *have_t = snap.have_bme_temp || snap.have_gxhtv3_temp;
  *temp_c = snap.have_bme_temp ? snap.bme_temp_c : snap.gxhtv3_temp_c;
  *have_h = snap.have_bme_hum || snap.have_gxhtv3_hum;
  *hum_pct = snap.have_bme_hum ? snap.bme_hum_pct : snap.gxhtv3_hum_pct;
  *have_p  = snap.have_bme_pressure;
  *press_hpa = snap.bme_pressure_hpa;
  *have_alt  = snap.have_bme_alt;
  *alt_m     = snap.bme_alt_m;
  *ok = (*have_t || *have_h || *have_p);
}
#endif

// App teardown. Drops the callback FIRST so the close below cannot re-enter a
// Lua state that is being torn down, then closes the dialog -- it lives on
// lv_layer_top and would otherwise outlive the app that opened it.
void luaHostTextPromptDismiss() {
  s_lua_prompt_cb = nullptr;
  luaHost.closePrompt();
}
void luaHostTextPrompt(const char* title, const char* initial, void (*cb)(const char*)) {
  luaHostTextPromptCancel();              // a second prompt cancels the first
  // Register the callback AFTER the dialog exists. fmTextPrompt opens with its
  // own luaHost.closePrompt(), which reports a cancel to whatever is pending -- so
  // setting cb first made every prompt fire cb(nil) before the user saw it.
  luaHost.prompt(title && *title ? title : TR("Enter text"), initial, luaPromptDeliver);
  s_lua_prompt_cb = cb;
}
#endif

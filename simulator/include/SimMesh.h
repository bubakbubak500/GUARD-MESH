// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Deterministic, offline mesh model. Stateful methods support UI editing and
// local sends; the remaining service endpoints report no data/unavailable.
// This does not run the RF protocol or make network/serial connections.
// Contact/channel layouts come from the pinned MeshCore headers; discovery/RX
// records below mirror src/MyMesh.h. Keep these adapters outside firmware builds.
#include <vector>
#include <ctime>
#define MAX_CONTACTS 100
#define MAX_GROUP_CHANNELS 8
#define MAX_TEXT_LEN 160
#define MAX_LORA_TX_POWER 22
#define MSG_SEND_FAILED 0
#define MSG_SEND_SENT_FLOOD 1
#define MSG_SEND_SENT_DIRECT 2
#define REGION_SLOT_NONE 0
#define REGION_SLOT_MAX 14
#define REGION_SLOT_AMBIGUOUS 15
class RegionRegistry {
public:
  int ensureRegion(const char *) { return 0; }
  bool isFull() { return false; }
  void retire(int) {}
  int count() { return 0; }
  bool isActive(int) { return false; }
  const char *nameForSlot(int) { return ""; }
};
class SimClock : public mesh::RTCClock {
public:
  uint32_t getCurrentTime() override { return (uint32_t)time(nullptr); }
  void setCurrentTime(uint32_t) override {}
  uint32_t getFloor() { return getCurrentTime(); }
  bool timeIsCurrent() { return true; }
  const char *sourceName() { return "Simulator host"; }
  static constexpr uint32_t MIN_VALID_EPOCH = 1609459200;
};
using ClockFloorRTC = SimClock;
inline SimClock rtc_clock;
class SimBoard : public mesh::MainBoard {
public:
  uint16_t getBattMilliVolts() override { return 4000; }
  const char *getManufacturerName() const override { return "LilyGo T-Deck simulator"; }
  void reboot() override { simRequestRestart(); }
  uint8_t getStartupReason() const override { return 0; }
};
inline SimBoard board;
struct RecentlyHeardName {
  uint8_t pubkey_prefix[7];
  char name[32];
  uint32_t recv_seq;
  uint8_t type;
};
class MyMesh {
public:
  enum class UiReqKind { None, Status, Telemetry };
  static constexpr int DISCOVER_MAX = 64;
  NodePrefs prefs{};
  std::vector<ContactInfo> contacts;
  ChannelDetails channels[MAX_GROUP_CHANNELS]{};
  uint32_t sent = 0, received = 0, lastFingerprint = 0, pendingAck = 0;
  unsigned long ackDue = 0;
  mesh::Identity self_id;
  RegionRegistry regions;
  NodePrefs *getNodePrefs() { return &prefs; }
  mesh::RTCClock *getRTCClock() { return &rtc_clock; }
  const uint8_t *getSelfPubKey() { return self_id.pub_key; }
  RegionRegistry &regionRegistry() { return regions; }
  static void setTerminalSink(void (*)(const char *)) {}
  struct DiscoverHit {
    uint8_t pubkey[32];  // responder identity (full key — the REQ sets prefix_only=0)
    uint8_t node_type;   // ADV_TYPE_* (RESP payload[0] low nibble): repeater/chat/room/sensor
    int8_t our_snr_q4;   // our RX SNR*4 of their reply (forward link)
    int8_t our_rssi;     // our RX RSSI dBm of their reply
    int8_t their_snr_q4; // their RX SNR*4 of our request (reverse link — RESP payload[1])
    uint8_t path_len;    // hops the reply travelled (0 = heard directly, i.e. in RF range)
    uint32_t first_ms;   // millis() first heard this session
    uint32_t last_ms;    // millis() last heard
    uint16_t heard;      // reply count
  };
  struct UiRxRec {
    uint32_t ms;   // millis() at RX
    int8_t rssi;   // dBm
    int8_t snr_q4; // SNR_dB * 4
    uint8_t ptype; // payload type  (raw[0]>>2)&0x0F
    uint8_t route; // route type    raw[0]&0x03
    uint8_t hops;  // path length carried (0 = heard direct from origin)
    uint8_t len;   // frame length (clamped to 255)
    // Who sent it, as far as the frame actually says. MeshCore only carries a
    // full identity on an ADVERT (the payload opens with the 32-byte public
    // key); the addressed types carry one-byte destination/source hashes, and
    // the rest carry nothing at all. Recorded honestly rather than guessed, so
    // a caller can tell "node X was here" from "something was here".
    //   org_kind 0 = nothing identifying in this frame
    //            1 = org[0..3] is the first 4 bytes of the origin's public key
    //            2 = org[0] is the destination hash, org[1] the source hash
    uint8_t org_kind;
    uint8_t org[4];
  };
  template <class... A> int addContact(A &&...) { return 0; }
  template <class... A> int advert(A &&...) { return 0; }
  template <class... A> int applyRadioFromPrefs(A &&...) { return 0; }
  template <class... A> int cancelUIDeferredLogin(A &&...) { return 0; }
  template <class... A> int cancelUIPingPending(A &&...) { return 0; }
  template <class... A> int discoverClear(A &&...) { return 0; }
  template <class... A> int discoverCount(A &&...) { return 0; }
  template <class... A> int discoverGet(A &&...) { return 0; }
  int findFirstEmptyChannelSlot() {
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++)
      if (!channels[i].name[0])
        return i;
    return -1;
  }
  template <class... A> int flushContactsIfDirty(A &&...) { return 0; }
  bool getChannel(int i, ChannelDetails &out) {
    if (i < 0 || i >= MAX_GROUP_CHANNELS)
      return false;
    out = channels[i];
    return true;
  }
  bool getContactByIdx(uint32_t i, ContactInfo &out) {
    if (i >= contacts.size())
      return false;
    out = contacts[i];
    return true;
  }
  template <class... A> int getLastTxtTxHash4(A &&...) { return 0; }
  template <class... A> int getNextCompanionRetryWakeDelay(A &&...) { return 0; }
  int getNumChannels() {
    int n = 0;
    for (auto &c : channels)
      if (c.name[0])
        n++;
    return n;
  }
  int getNumContacts() { return (int)contacts.size(); }
  uint32_t getNumRecvDirect() { return received; }
  template <class... A> int getNumRecvFlood(A &&...) { return 0; }
  uint32_t getNumSentDirect() { return sent; }
  template <class... A> int getNumSentFlood(A &&...) { return 0; }
  template <class... A> int getOrphanedBlobs(A &&...) { return 0; }
  template <class... A> int getProtoNumClients(A &&...) { return 0; }
  template <class... A> int getReceiveAirTime(A &&...) { return 0; }
  template <class... A> int getRecentlyHeard(A &&...) { return 0; }
  template <class... A> int getRemainingTxBudget(A &&...) { return 0; }
  template <class... A> int getStore(A &&...) { return 0; }
  template <class... A> int getTotalAirTime(A &&...) { return 0; }
  template <class... A> int hasPendingWork(A &&...) { return 0; }
  template <class... A> int isRadioReceiving(A &&...) { return 0; }
  template <class... A> int lastRxPath(A &&...) { return 0; }
  template <class... A> int lastRxScope(A &&...) { return 0; }
  template <class... A> int lastRxScopeIsHome(A &&...) { return 0; }
  template <class... A> int lastRxScopeSlot(A &&...) { return 0; }
  ContactInfo *lookupContactByPubKey(const uint8_t *pub, size_t n = PUB_KEY_SIZE) {
    for (auto &c : contacts)
      if (memcmp(c.id.pub_key, pub, n) == 0)
        return &c;
    return nullptr;
  }
  template <class... A> int loop(A &&...) { return 0; }
  template <class... A> int persistSyncHistoryNow(A &&...) { return 0; }
  template <class... A> int popChannelScope(A &&...) { return 0; }
  template <class... A> int pushChannelScope(A &&...) { return 0; }
  template <class... A> int resetPathTo(A &&...) { return 0; }
  template <class... A> int runLocalCli(A &&...) { return 0; }
  bool savePrefs() {
    SdNvsPrefs store;
    store.begin("sim");
    return store.putBytes("node", &prefs, sizeof prefs) == sizeof prefs;
  }
  template <class... A> int sendAdvert(A &&...) { return 0; }
  bool sendGroupMessage(uint32_t, const mesh::GroupChannel &, const char *, const char *, int) {
    ++sent;
    lastFingerprint = sent;
    return true;
  }
  int sendMessage(const ContactInfo &, uint32_t, uint8_t, const char *, uint32_t &ack,
                  uint32_t &timeout, uint32_t *hash = nullptr) {
    ++sent;
    lastFingerprint = sent;
    ack = sent;
    timeout = 1000;
    if (hash)
      *hash = sent;
    pendingAck = ack;
    ackDue = millis() + 700;
    return MSG_SEND_SENT_DIRECT;
  }
  template <class... A> int sendTelemetryRequestWithGuestLoginForUI(A &&...) { return 0; }
  template <class... A> int setBLEPin(A &&...) { return 0; }
  template <class... A> int setCompanionRetryEnabled(A &&...) { return 0; }
  template <class... A> int setDefaultFloodScope(A &&...) { return 0; }
  template <class... A> int setScopeDirectFloods(A &&...) { return 0; }
  template <class... A> int takeEchoDirty(A &&...) { return 0; }
  template <class... A> int uiAddDiscoveredContact(A &&...) { return 0; }
  bool uiAddManualContact(const uint8_t *pub, const char *name) {
    if (contacts.size() >= MAX_CONTACTS)
      return false;
    ContactInfo c{};
    memcpy(c.id.pub_key, pub, PUB_KEY_SIZE);
    strlcpy(c.name, name, sizeof c.name);
    c.type = ADV_TYPE_CHAT;
    c.out_path_len = OUT_PATH_UNKNOWN;
    contacts.push_back(c);
    return true;
  }
  bool uiAddOrUpdateChannel(int i, const char *name, const uint8_t *secret) {
    if (i < 0 || i >= MAX_GROUP_CHANNELS)
      return false;
    auto &c = channels[i];
    strlcpy(c.name, name, sizeof c.name);
    memset(c.channel.secret, 0, sizeof c.channel.secret);
    memcpy(c.channel.secret, secret, 16);
    return true;
  }
  template <class... A> int uiConsumeLastSenderTs(A &&...) { return 0; }
  int uiContactIdxByPubKey(const uint8_t *pub) {
    for (size_t i = 0; i < contacts.size(); i++)
      if (!memcmp(pub, contacts[i].id.pub_key, PUB_KEY_SIZE))
        return (int)i;
    return -1;
  }
  bool uiDeleteChannel(int i) {
    if (i < 0 || i >= MAX_GROUP_CHANNELS)
      return false;
    channels[i] = {};
    return true;
  }
  template <class... A> int uiExportBackup(A &&...) { return 0; }
  template <class... A> int uiFactoryReset(A &&...) { return 0; }
  template <class... A> int uiGetContactTelemetryPerm(A &&...) { return 0; }
  template <class... A> int uiHasConnectionTo(A &&...) { return 0; }
  template <class... A> int uiHopName(A &&...) { return 0; }
  template <class... A> int uiHopPos(A &&...) { return 0; }
  template <class... A> int uiImportBackup(A &&...) { return 0; }
  template <class... A> int uiIsMeshcomodRecipient(A &&...) { return 0; }
  bool uiJoinPublicChannel() {
    for (auto &c : channels)
      if (!strcmp(c.name, "Public"))
        return true;
    int i = findFirstEmptyChannelSlot();
    if (i < 0)
      return false;
    strcpy(channels[i].name, "Public");
    return true;
  }
  uint32_t uiLastSentFp() { return lastFingerprint; }
  template <class... A> int uiPersistContacts(A &&...) { return 0; }
  template <class... A> int uiRegisterExpectedAck(A &&...) { return 0; }
  bool uiRemoveContact(const ContactInfo &c) {
    int i = uiContactIdxByPubKey(c.id.pub_key);
    if (i < 0)
      return false;
    contacts.erase(contacts.begin() + i);
    return true;
  }
  template <class... A> int uiRepeatHop(A &&...) { return 0; }
  template <class... A> int uiRepeatHopCount(A &&...) { return 0; }
  template <class... A> int uiRepeatsForFp(A &&...) { return 0; }
  template <class... A> int uiResetContactPath(A &&...) { return 0; }
  template <class... A> int uiRoomRelogin(A &&...) { return 0; }
  template <class... A> int uiRxLogGet(A &&...) { return 0; }
  template <class... A> int uiSendAdminCommand(A &&...) { return 0; }
  template <class... A> int uiSendAdminLogin(A &&...) { return 0; }
  template <class... A> int uiSendRequestAfterGuestLogin(A &&...) { return 0; }
  template <class... A> int uiSendSignalProbe(A &&...) { return 0; }
  template <class... A> int uiSendTracePing(A &&...) { return 0; }
  template <class... A> int uiSendTraceRoute(A &&...) { return 0; }
  bool uiSetContactFavorite(const uint8_t *pub, bool v) {
    auto c = lookupContactByPubKey(pub);
    if (!c)
      return false;
    c->flags = v ? (c->flags | 1) : (c->flags & ~1);
    return true;
  }
  template <class... A> int uiSetContactGps(A &&...) { return 0; }
  template <class... A> int uiSetContactTelemetryPerm(A &&...) { return 0; }
  template <class... A> int uiShareContact(A &&...) { return 0; }
  template <class... A> int uiSignalMs(A &&...) { return 0; }
  template <class... A> int uiSignalRssi(A &&...) { return 0; }
  template <class... A> int uiSignalSnrQ4(A &&...) { return 0; }
  template <class... A> int uiStartDiscoverScan(A &&...) { return 0; }
};
inline MyMesh the_mesh;
struct SimRadio {
  template <class... A> int getCurrentRSSI(A &&...) { return 0; }
  template <class... A> int getEstAirtimeFor(A &&...) { return 0; }
  template <class... A> int getNoiseFloor(A &&...) { return 0; }
  template <class... A> int getPacketsRecv(A &&...) { return 0; }
  template <class... A> int getPacketsRecvErrors(A &&...) { return 0; }
  template <class... A> int getPacketsSent(A &&...) { return 0; }
  template <class... A> int getRxEvents(A &&...) { return 0; }
  template <class... A> int getRxQueueDrops(A &&...) { return 0; }
  template <class... A> int isInRecvMode(A &&...) { return 0; }
  template <class... A> int isSendComplete(A &&...) { return 0; }
  template <class... A> int powerOff(A &&...) { return 0; }
  template <class... A> int radioAcquire(A &&...) { return 0; }
  template <class... A> int radioRelease(A &&...) { return 0; }
  template <class... A> int recvRaw(A &&...) { return 0; }
  template <class... A> int rxQueueEnable(A &&...) { return 0; }
  template <class... A> int rxQueueEnabled(A &&...) { return 0; }
  template <class... A> int rxQueueSuspend(A &&...) { return 0; }
  template <class... A> int setParams(A &&...) { return 0; }
  template <class... A> int setRxBoostedGainMode(A &&...) { return 0; }
  template <class... A> int setTxPower(A &&...) { return 0; }
};
inline SimRadio radio_driver;
struct SimWebMirror {
  template <class... A> int active(A &&...) { return 0; }
  template <class... A> int begin(A &&...) { return 0; }
  template <class... A> int clients(A &&...) { return 0; }
  template <class... A> int empty(A &&...) { return 0; }
  template <class... A> int popKey(A &&...) { return 0; }
  template <class... A> int popTermCmd(A &&...) { return 0; }
  template <class... A> int pushFrame(A &&...) { return 0; }
  template <class... A> int pushTermData(A &&...) { return 0; }
  template <class... A> int pushTermReply(A &&...) { return 0; }
  template <class... A> int readPointer(A &&...) { return 0; }
  template <class... A> int ringBytes(A &&...) { return 0; }
  template <class... A> int setEnabled(A &&...) { return 0; }
  template <class... A> int setKbFocused(A &&...) { return 0; }
  template <class... A> int setLocked(A &&...) { return 0; }
  template <class... A> int setRemote(A &&...) { return 0; }
  template <class... A> int setScreenSize(A &&...) { return 0; }
  template <class... A> int setTerminalOn(A &&...) { return 0; }
  template <class... A> int takeExit(A &&...) { return 0; }
  template <class... A> int takeFullRepaint(A &&...) { return 0; }
  template <class... A> int takeOrient(A &&...) { return 0; }
  template <class... A> int takeUnlock(A &&...) { return 0; }
  template <class... A> int terminalOn(A &&...) { return 0; }
};
inline SimWebMirror g_web_mirror;

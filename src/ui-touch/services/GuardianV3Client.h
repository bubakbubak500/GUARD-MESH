// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../models/GuardianV3.h"
#include "GuardianV3Storage.h"
#include <deque>
namespace guardian { namespace v3 {
struct Intent {
  std::string token, action, fields, command_id;
  uint8_t radio=0;
  bool unresolved=false;
  Intent() = default;
  Intent(std::string t,std::string a,std::string f,std::string id,uint8_t r,bool u) : token(std::move(t)),action(std::move(a)),fields(std::move(f)),command_id(std::move(id)),radio(r),unresolved(u) {}
};
// UI-thread coordinator. The BLE callbacks only move bounded wire packets.
class Client {
public:
  Station station;
  Capabilities capabilities;
  std::vector<AlertType> types;
  AlertsPage history;
  Command command;
  Intent intent;
  std::string error;
  bool historyLoaded=false, historyTruncated=false, storageFailed=false;
  uint32_t revision=0;
  void configure(StorageHooks hooks);
  void tick(uint32_t now,bool allowRpc=true,bool stationVisible=false);
  bool busy() const { return pending; }
  bool stationFresh(uint32_t now) const;
  bool controlsReady(uint32_t now) const;
  const Radio* radio(unsigned number) const;
  const RadioCapabilities* radioCapabilities(unsigned number) const;
  const ActionAvailability* action(unsigned number,const std::string& name) const;
  bool execute(unsigned radio,const std::string& action,const std::string& fields,uint32_t now);
  bool retryIntent(uint32_t now); // explicit user action, never called by tick
  bool discardIntent(uint32_t now); // explicit decision after refreshed station
  bool seen(const std::string& alertId);
  void requestHistory(uint64_t offset=0,const std::string& revision="");
  void refreshStation() { haveStation=false; haveCaps=false; commandAt=0; }
  const Alert* emergency() const;
  std::string alertLabel(unsigned code) const;
  std::string commandLabel() const;
  uint64_t cursor() const { return eventCursor; }
  const std::string& stream() const { return eventStream; }
  uint64_t loadedHistoryOffset() const { return loadedOffset; }
  bool historyPending() const { return historyRequested || (pending && operation=="alerts.list"); }
private:
  DurableStore storage;
  StorageHooks storageHooks;
  bool storageConfigured=false, stateCorrupt=false;
  bool initialized=false, connected=false, pending=false, haveStation=false, haveCaps=false, haveTypes=false;
  bool recover=true, ack=false, sendIntent=false, sendRetry=false, historyRequested=false;
  uint32_t serial=0, stationAt=0, capAt=0, commandAt=0, retryAt=0;
  uint32_t storageRetryAt=0;
  uint64_t eventCursor=0, historyOffset=0;
  uint64_t requestHistoryOffset=0;
  uint64_t loadedOffset=0;
  uint32_t historyGeneration=0, requestHistoryGeneration=0;
  std::string eventStream, requestId, operation, historyRevision;
  std::string requestAlertId;
  std::vector<Alert> alerts;
  std::deque<std::string> eventIds, seenIds;
  std::deque<Event> heldEvents;
  std::deque<Command> earlyCommands;
  bool earlyCommandsFull=false;
  bool load();
  bool save();
  bool start(const char* op,JsonDocument& doc,uint32_t now);
  void response(const std::string& raw,const std::string& failure,uint32_t now);
  bool acceptEvent(const Event& event,bool allowGap=false);
  bool upsertAlert(const Alert& alert);
  bool locallySeen(const std::string& id) const;
  void protocolError();
};
Client& client();
} }

// SPDX-License-Identifier: GPL-3.0-or-later
#include "GuardianV3Client.h"
#include "GuardianRpcLink.h"
#include "GuardianLink.h"
#include "GuardianDraft.h"
#include "../i18n.h"
#include <algorithm>
namespace guardian { namespace v3 {
namespace {
bool emergencyCode(unsigned code) { return code==1 || code==2 || code==3; }
void alertJson(JsonObject obj,const Alert& a) {
  obj["alert_id"]=a.alert_id; obj["code"]=a.code; obj["note"]=a.note; obj["source"]=a.source;
  if(a.created.known) obj["created"]=a.created.value; else obj["created"]=nullptr;
  if(a.radio.known) obj["radio"]=a.radio.value; else obj["radio"]=nullptr;
  if(a.rf_id.known) obj["rf_id"]=a.rf_id.value; else obj["rf_id"]=nullptr;
  obj["forwarded"]=a.forwarded; obj["seen"]=a.seen;
  auto origin=obj["origin"].to<JsonObject>(); origin["network"]=a.origin.network;
  if(!a.origin.device.empty()) origin["device"]=a.origin.device;
  if(a.origin.local.known) origin["local"]=a.origin.local.value;
}
const char* stateName(CommandState state) {
  switch(state) {
    case CommandState::Executing:return "executing"; case CommandState::Pending:return "pending";
    case CommandState::Completed:return "completed"; case CommandState::Queued:return "queued";
    case CommandState::Failed:return "failed"; default:return "unknown";
  }
}
void commandJson(JsonObject obj,const Command& c) {
  obj["command_id"]=c.command_id; obj["action"]=c.action; obj["radio"]=c.radio;
  obj["accepted"]=c.accepted; obj["state"]=stateName(c.state); obj["duplicate"]=c.duplicate;
  obj["worker"]=c.worker;
  if(c.created.known) obj["created"]=c.created.value; else obj["created"]=nullptr;
  if(!c.error.empty()) { obj["error"]["code"]=c.error.code; obj["error"]["message"]=c.error.message; }
}
}
Client& client() { static Client value; return value; }
void Client::configure(StorageHooks hooks) { storageHooks=hooks; storageConfigured=true; storage.configure(hooks); load(); initialized=!storage.unavailable(); }
bool Client::load() {
  std::string raw;
  if(!storage.load(raw)) {
    storageFailed=storage.unavailable() || storage.corrupt();
    if(storageFailed) error="storage_error";
    return false;
  }
  JsonDocument doc;
  if(raw.size()>DurableStore::MaxBytes || !guardian::utf8(raw) || deserializeJson(doc,raw) ||
     !doc.is<JsonObject>() || !doc["version"].is<unsigned>() || doc["version"].as<unsigned>()!=1 || !doc["cursor"].is<uint64_t>() ||
     doc["cursor"].as<uint64_t>()>MaxCursor) { stateCorrupt=true; storageFailed=true; error="storage_error"; return false; }
  const std::string stream=doc["stream"] | "";
  if(!stream.empty() && !uuid(stream)) { stateCorrupt=true; storageFailed=true; error="storage_error"; return false; }
  std::vector<Alert> saved;
  const auto invalid=[this]() { stateCorrupt=true; storageFailed=true; error="storage_error"; return false; };
  if(!doc["alerts"].is<JsonArray>() || doc["alerts"].size()>MaxItems) return invalid();
  for(auto item:doc["alerts"].as<JsonArrayConst>()) { Alert a; if(!parseAlert(item,a)) return invalid(); saved.push_back(std::move(a)); }
  Intent old;
  if(doc["intent"].is<JsonObject>()) {
    const auto i=doc["intent"].as<JsonObjectConst>(); old.token=i["token"] | ""; old.action=i["action"] | "";
    old.fields=i["fields"] | "{}"; old.command_id=i["command_id"] | ""; old.radio=i["radio"] | 0u;
    old.unresolved=i["unresolved"] | false;
    JsonDocument fields;
    if(!uuid(old.token) || (old.radio!=1 && old.radio!=2) || !guardian::parseObject(fields,old.fields) ||
       (!old.command_id.empty() && !uuid(old.command_id))) return invalid();
  }
  std::deque<std::string> ids,seen;
  if(!doc["event_ids"].is<JsonArray>() || !doc["seen_ids"].is<JsonArray>()) return invalid();
  for(auto item:doc["event_ids"].as<JsonArrayConst>()) { const std::string id=item | ""; if(!uuid(id) || ids.size()>=64) return invalid(); ids.push_back(id); }
  for(auto item:doc["seen_ids"].as<JsonArrayConst>()) { const std::string id=item | ""; if(!uuid(id) || seen.size()>=MaxItems) return invalid(); seen.push_back(id); }
  Command savedCommand;
  if(!doc["command"].isNull() && !parseCommand(doc["command"],savedCommand)) return invalid();
  std::deque<Command> early;
  if(!doc["early_commands"].isNull()) {
    if(!doc["early_commands"].is<JsonArray>() || doc["early_commands"].size()>MaxItems) return invalid();
    for(auto item:doc["early_commands"].as<JsonArrayConst>()) {
      Command candidate;
      if(!parseCommand(item,candidate) || !old.unresolved || !old.command_id.empty() ||
         candidate.action!=old.action || candidate.radio!=old.radio) return invalid();
      for(const auto& existing:early) if(existing.command_id==candidate.command_id) return invalid();
      early.push_back(std::move(candidate));
    }
  }
  alerts.swap(saved); eventStream=stream; eventCursor=doc["cursor"].as<uint64_t>();
  intent=std::move(old); eventIds.swap(ids); seenIds.swap(seen); command=std::move(savedCommand);
  earlyCommands.swap(early);
  earlyCommandsFull=earlyCommands.size()>=MaxItems;
  if(earlyCommandsFull) requestHistory();
  if(intent.unresolved && intent.command_id.empty()) { command.state=CommandState::Unknown; command.action=intent.action; command.radio=intent.radio; }
  storageFailed=false; ++revision; return true;
}
bool Client::save() {
  if(!initialized || stateCorrupt) { storageFailed=true; error="storage_error"; return false; }
  JsonDocument doc; doc["version"]=1; doc["stream"]=eventStream; doc["cursor"]=eventCursor;
  auto list=doc["alerts"].to<JsonArray>(); for(const auto& a:alerts) alertJson(list.add<JsonObject>(),a);
  auto ids=doc["event_ids"].to<JsonArray>(); for(const auto& id:eventIds) ids.add(id);
  auto seen=doc["seen_ids"].to<JsonArray>(); for(const auto& id:seenIds) seen.add(id);
  if(!intent.token.empty()) {
    auto i=doc["intent"].to<JsonObject>(); i["token"]=intent.token; i["action"]=intent.action;
    i["fields"]=intent.fields; i["command_id"]=intent.command_id; i["radio"]=intent.radio; i["unresolved"]=intent.unresolved;
  }
  if(uuid(command.command_id)) commandJson(doc["command"].to<JsonObject>(),command);
  auto early=doc["early_commands"].to<JsonArray>();
  for(const auto& candidate:earlyCommands) commandJson(early.add<JsonObject>(),candidate);
  const bool ok=storage.save(guardian::jsonText(doc));
  storageFailed=!ok; if(!ok) error="storage_error";
  return ok;
}
bool Client::stationFresh(uint32_t now) const {
  return haveStation && guardian::snapshot(now).session.fresh(now) && uint32_t(now-stationAt)<15000;
}
bool Client::controlsReady(uint32_t now) const {
  return haveCaps && capabilities.api_version==3 && capabilities.control_allowed &&
    stationFresh(now) && uint32_t(now-capAt)<15000 && !storageFailed;
}
const Radio* Client::radio(unsigned number) const { for(const auto& r:station.radios) if(r.radio==number) return &r; return nullptr; }
const RadioCapabilities* Client::radioCapabilities(unsigned number) const { for(const auto& r:capabilities.radios) if(r.radio==number) return &r; return nullptr; }
const ActionAvailability* Client::action(unsigned number,const std::string& name) const {
  const auto* r=radioCapabilities(number); return r?r->action(name):nullptr;
}
bool Client::execute(unsigned number,const std::string& name,const std::string& fields,uint32_t now) {
  static const char* allowed[]={"radio.connect","radio.disconnect","modem.connect","modem.disconnect","control.start","control.stop","beacon.send","radio.tune","profile.apply","calibration.start","calibration.cancel"};
  const auto* a=action(number,name);
  if(number<1 || number>2 || !controlsReady(now) || !a || !a->supported || !a->available ||
     intent.unresolved || std::find(std::begin(allowed),std::end(allowed),name)==std::end(allowed)) {
    error=intent.unresolved?"command_unresolved":!capabilities.control_allowed?"control_denied":"unavailable"; ++revision; return false;
  }
  JsonDocument f; if(!guardian::parseObject(f,fields)) { error="invalid_request"; ++revision; return false; }
  auto obj=f.as<JsonObjectConst>();
  const auto* rc=radioCapabilities(number);
  if(name=="radio.tune") {
    const uint64_t frequency=f["frequency_hz"] | uint64_t(0); const std::string mode=f["mode"] | "";
    if(obj.size()!=2 || !f["frequency_hz"].is<uint64_t>() || frequency<1 || frequency>MaxFrequency ||
       !rc || std::find(rc->tune_modes.begin(),rc->tune_modes.end(),mode)==rc->tune_modes.end() ||
       (rc->tune_mode=="frequency_only" && (mode!="FM" || frequency%10))) { error="invalid_request"; ++revision; return false; }
  } else if(name=="profile.apply") {
    const std::string profile=f["profile"] | "";
    if(obj.size()!=1 || !rc || std::find(rc->profiles.begin(),rc->profiles.end(),profile)==rc->profiles.end()) { error="invalid_request"; ++revision; return false; }
  } else if(name=="calibration.start") {
    const std::string peer=f["peer"] | "", mode=f["mode"] | "";
    if(obj.size()!=2 || peer.empty() || peer.size()>16 || (mode!="quick" && mode!="full") ||
       peer.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/-")!=std::string::npos) { error="invalid_request"; ++revision; return false; }
  } else if(obj.size()) { error="invalid_request"; ++revision; return false; }
  const auto previous=intent; const auto previousCommand=command;
  intent={guardian::newToken(),name,fields,"",uint8_t(number),true};
  command=Command{}; command.action=name; command.radio=uint8_t(number); command.state=CommandState::Pending;
  if(!save()) { intent=previous; command=previousCommand; ++revision; return false; }
  error.clear(); sendIntent=true; sendRetry=false; ++revision; return true;
}
bool Client::retryIntent(uint32_t now) {
  const auto* a=action(intent.radio,intent.action);
  if(!intent.unresolved || !intent.command_id.empty() || !controlsReady(now) || !a || !a->supported) return false;
  if(!save()) return false;
  sendIntent=true; sendRetry=true; command.state=CommandState::Pending; error.clear(); ++revision; return true;
}
bool Client::discardIntent(uint32_t now) {
  if(!stationFresh(now) || !intent.unresolved || command.state!=CommandState::Unknown) return false;
  const auto previous=intent; const auto previousEarly=earlyCommands; intent=Intent{}; earlyCommands.clear();
  if(!save()) { intent=previous; earlyCommands=previousEarly; return false; }
  earlyCommandsFull=false;
  sendIntent=false; ++revision; return true;
}
bool Client::locallySeen(const std::string& id) const { return std::find(seenIds.begin(),seenIds.end(),id)!=seenIds.end(); }
bool Client::seen(const std::string& id) {
  if(!uuid(id)) return false;
  if(locallySeen(id)) return true;
  if(seenIds.size()>=MaxItems) return false;
  const auto previous=alerts; seenIds.push_back(id);
  for(auto& a:alerts) if(a.alert_id==id) a.seen=true;
  if(!save()) { seenIds.pop_back(); alerts=previous; ++revision; return false; }
  for(auto& a:history.items) if(a.alert_id==id) a.seen=true;
  ++revision; return true;
}
bool Client::upsertAlert(const Alert& item) {
  Alert copy=item; if(locallySeen(copy.alert_id)) copy.seen=true;
  for(auto& a:alerts) if(a.alert_id==copy.alert_id) { copy.seen=copy.seen || a.seen; a=copy; return true; }
  if(alerts.size()>=MaxItems) {
    auto victim=std::find_if(alerts.begin(),alerts.end(),[](const Alert& a){ return a.seen || !emergencyCode(a.code); });
    if(victim==alerts.end()) return false;
    alerts.erase(victim);
  }
  alerts.push_back(copy); return true;
}
const Alert* Client::emergency() const {
  for(const auto& a:alerts) if(!a.seen && emergencyCode(a.code)) return &a;
  return nullptr;
}
std::string Client::alertLabel(unsigned code) const {
  for(const auto& type:types) if(type.code==code) return type.label;
  switch(code) { case 1:return "MAYDAY"; case 2:return TR("Medical emergency"); case 3:return TR("Evacuation"); default:return std::string(TR("Alert"))+" "+std::to_string(code); }
}
std::string Client::commandLabel() const {
  switch(command.state) {
    case CommandState::Executing:return TR("Executing"); case CommandState::Pending:return TR("Pending");
    case CommandState::Completed:return command.succeeded()?TR("Completed"):TR("Failed");
    case CommandState::Queued:return command.succeeded()?TR("Beacon queued"):TR("Failed");
    case CommandState::Failed:return TR("Failed"); default:return TR("Result unknown. Check PC before another command.");
  }
}
void Client::requestHistory(uint64_t offset,const std::string& rev) { historyOffset=offset; historyRevision=rev; historyRequested=true; ++historyGeneration; }
void Client::protocolError() {
  error="protocol_error";
  if(intent.unresolved) { command.state=CommandState::Unknown; command.error.code="protocol_error"; save(); }
  ++revision; guardian::rpcAbort();
}
bool Client::acceptEvent(const Event& event,bool allowGap) {
  if(event.kind==EventType::CommandChanged && event.command.command_id==intent.command_id &&
     (event.command.radio!=intent.radio || event.command.action!=intent.action)) { protocolError(); return false; }
  if(event.history_truncated) { historyTruncated=true; recover=true; haveStation=haveCaps=false; commandAt=0; requestHistory(); }
  if(event.stream==eventStream && event.seq<=eventCursor) { ack=true; return true; }
  if(!allowGap && (event.stream!=eventStream || event.seq!=eventCursor+1)) {
    // If uncorrelated command results fill their journal, a later emergency
    // must still become durable/visible while the Event cursor stays blocked.
    if(earlyCommandsFull && event.stream==eventStream && event.seq>eventCursor && event.kind==EventType::AlertCreated) {
      const bool known=std::any_of(alerts.begin(),alerts.end(),[&](const Alert& a){return a.alert_id==event.alert.alert_id;});
      if(!known) {
        const auto previous=alerts;
        if(upsertAlert(event.alert) && save()) ++revision;
        else alerts=previous;
      }
    }
    recover=true; return false;
  }
  const auto oldAlerts=alerts; const auto oldCommand=command; const auto oldIntent=intent;
  const auto oldEarly=earlyCommands;
  const auto oldIds=eventIds, oldSeen=seenIds; const auto oldStream=eventStream; const auto oldCursor=eventCursor;
  if(event.stream!=eventStream) { eventStream=event.stream; eventCursor=0; eventIds.clear(); historyTruncated=true; }
  const bool duplicate=std::find(eventIds.begin(),eventIds.end(),event.event_id)!=eventIds.end();
  bool valid=true;
  if(!duplicate) {
    if(event.kind==EventType::AlertCreated) valid=upsertAlert(event.alert);
    else if(event.kind==EventType::AlertSeen) {
      for(auto& a:alerts) if(a.alert_id==event.alert_id) a.seen=true;
      seenIds.erase(std::remove(seenIds.begin(),seenIds.end(),event.alert_id),seenIds.end());
    } else if(event.kind==EventType::CommandChanged && event.command.command_id==intent.command_id) {
      command=event.command; intent.unresolved=!command.terminal() || command.requiresDecision();
    } else if(event.kind==EventType::CommandChanged && intent.unresolved && intent.command_id.empty() &&
              event.command.radio==intent.radio && event.command.action==intent.action) {
      // Preserve early results before ACK, without guessing which command the
      // token owns. Only the execute response can establish its command ID.
      auto found=std::find_if(earlyCommands.begin(),earlyCommands.end(),[&](const Command& c){return c.command_id==event.command.command_id;});
      if(found!=earlyCommands.end()) {
        if(!(found->terminal() && !found->requiresDecision() && !event.command.terminal())) *found=event.command;
      }
      else if(earlyCommands.size()<MaxItems) earlyCommands.push_back(event.command);
      else { valid=false; earlyCommandsFull=true; if(!historyRequested) requestHistory(); }
    }
    eventIds.push_back(event.event_id); if(eventIds.size()>64) eventIds.pop_front();
  }
  eventCursor=event.seq;
  if(!valid || !save()) {
    alerts=oldAlerts; command=oldCommand; intent=oldIntent; eventIds=oldIds; seenIds=oldSeen;
    earlyCommands=oldEarly;
    eventStream=oldStream; eventCursor=oldCursor; recover=true; return false;
  }
  for(auto& item:history.items) {
    if(event.kind==EventType::AlertCreated && item.alert_id==event.alert.alert_id) {
      const bool wasSeen=item.seen; item=event.alert; if(wasSeen || locallySeen(item.alert_id)) item.seen=true;
    } else if(event.kind==EventType::AlertSeen && item.alert_id==event.alert_id) item.seen=true;
  }
  ack=true; ++revision; return true;
}
bool Client::start(const char* op,JsonDocument& doc,uint32_t now) {
  const std::string id="v3."+std::to_string(++serial); doc["id"]=id; doc["op"]=op;
  if(!guardian::rpcStart(guardian::jsonText(doc),now)) return false;
  requestAlertId=doc["alert_id"] | "";
  if(std::string(op)=="alerts.list") { requestHistoryOffset=historyOffset; requestHistoryGeneration=historyGeneration; }
  requestId=id; operation=op; pending=true; return true;
}
void Client::response(const std::string& raw,const std::string& failure,uint32_t now) {
  pending=false; JsonDocument doc; Response envelope; std::string problem=failure;
  if(problem.empty() && (!guardian::parseObject(doc,raw) || !parseResponse(doc.as<JsonVariantConst>(),envelope) || envelope.id!=requestId)) { protocolError(); return; }
  if(problem.empty() && !envelope.ok) problem=envelope.error.code;
  if(!problem.empty()) {
    error=problem; retryAt=now+2000;
    if((operation=="control.execute" || operation=="command.get") && intent.unresolved) {
      command.state=CommandState::Unknown; command.action=intent.action; command.radio=intent.radio;
      command.error.code=problem; intent.unresolved=true; save();
    } else if(operation=="alerts.list" && problem=="list_changed") { historyOffset=0; historyRevision.clear(); historyRequested=true; }
    else if(operation=="events.ack") ack=true;
    else if(operation=="events.list") recover=true;
    else if(operation=="alert.seen" && problem=="not_found") {
      seenIds.erase(std::remove(seenIds.begin(),seenIds.end(),requestAlertId),seenIds.end()); save();
    }
    ++revision; return;
  }
  auto result=doc["result"].as<JsonVariantConst>(); bool valid=true;
  if(operation=="capabilities.get") {
    valid=parseCapabilities(result,capabilities); if(valid) { haveCaps=true; capAt=now; }
  } else if(operation=="station.get") {
    valid=parseStation(result,station); if(valid) {
      haveStation=true; stationAt=now;
      // A saturated uncorrelated-command journal pauses Event replay. Keep
      // receiving emergency notices through current history until resolved.
      if(earlyCommandsFull) requestHistory();
    }
  } else if(operation=="alert.types") {
    valid=parseAlertTypes(result,types); if(valid) haveTypes=true;
  } else if(operation=="control.execute" || operation=="command.get") {
    Command incoming; valid=parseCommand(result,incoming) && incoming.radio==intent.radio && incoming.action==intent.action &&
      (intent.command_id.empty() || incoming.command_id==intent.command_id);
    if(valid) {
      const auto oldIntent=intent; const auto oldCommand=command; const auto oldEarly=earlyCommands;
      intent.command_id=incoming.command_id;
      for(const auto& candidate:earlyCommands) if(candidate.command_id==incoming.command_id) command=candidate;
      earlyCommands.clear();
      // A command.changed Event can complete the command while command.get is
      // in flight. Its older pending snapshot must not undo that completion.
      if(!(command.command_id==incoming.command_id && command.terminal() && !command.requiresDecision() && !incoming.terminal())) command=incoming;
      intent.unresolved=!command.terminal() || command.requiresDecision(); commandAt=now;
      if(!save()) { intent=oldIntent; command=oldCommand; earlyCommands=oldEarly; command.state=CommandState::Unknown; }
      else earlyCommandsFull=false;
      if(operation=="control.execute" || oldCommand.state!=command.state || oldCommand.error.code!=command.error.code)
        haveStation=haveCaps=false;
    }
  } else if(operation=="alerts.list") {
    AlertsPage page; valid=parseAlertsPage(result,page) && (!page.next_offset.known || page.next_offset.value>requestHistoryOffset);
    if(valid) {
      const auto old=alerts;
      for(auto& a:page.items) {
        if(locallySeen(a.alert_id)) a.seen=true;
        if(!upsertAlert(a)) { valid=false; break; }
        for(const auto& stored:alerts) if(stored.alert_id==a.alert_id) { a.seen=stored.seen; break; }
      }
      if(valid && save()) { if(requestHistoryGeneration==historyGeneration) { history=std::move(page); historyLoaded=true; loadedOffset=requestHistoryOffset; } }
      else { alerts=old; historyRequested=true; }
    }
  } else if(operation=="alert.seen") {
    const std::string id=result["alert_id"] | "";
    valid=uuid(id) && id==requestAlertId && result["seen"].is<bool>() && result["seen"].as<bool>();
    if(valid) { seenIds.erase(std::remove(seenIds.begin(),seenIds.end(),id),seenIds.end()); save(); }
  } else if(operation=="events.ack") {
    const std::string stream=result["stream"] | ""; const auto cursor=result["cursor"];
    valid=stream==eventStream && cursor.is<uint64_t>() && cursor.as<uint64_t>()<=MaxCursor;
    if(valid && cursor.as<uint64_t>()>eventCursor) recover=true;
  } else if(operation=="events.list") {
    EventsPage page; valid=parseEventsPage(result,page);
    if(valid) {
      bool applied=true; bool gap=page.truncated || page.stream!=eventStream;
      if(gap) {
        historyTruncated=true; haveStation=haveCaps=false; commandAt=0;
        requestHistory();
      }
      if(page.truncated && page.stream==eventStream && page.cursor<eventCursor) {
        // A restored PC database may retain its stream but rewind the cursor.
        // Nothing from the discarded range is treated as a new command intent.
        eventCursor=0; eventIds.clear();
      }
      for(const auto& event:page.items) { if(!acceptEvent(event,gap)) { applied=false; break; } gap=false; }
      if(applied && page.items.empty() && (page.stream!=eventStream || page.truncated)) {
        const auto oldStream=eventStream; const auto oldCursor=eventCursor; const auto oldIds=eventIds;
        eventStream=page.stream; eventCursor=page.cursor; eventIds.clear();
        if(!save()) { eventStream=oldStream; eventCursor=oldCursor; eventIds=oldIds; applied=false; }
      }
      if(applied) {
        // Live Events may advance the same durable stream beyond a history
        // snapshot assembled on PC before that RPC response arrived.
        if(page.cursor>eventCursor) { protocolError(); return; }
        recover=page.has_more; ack=eventCursor>0;
        for(auto it=heldEvents.begin();it!=heldEvents.end();) {
          if(it->stream!=eventStream || it->seq<=eventCursor) it=heldEvents.erase(it);
          else if(acceptEvent(*it)) it=heldEvents.erase(it); else { recover=true; break; }
        }
        if(!historyLoaded) requestHistory();
      } else { recover=true; retryAt=now+2000; }
    }
  }
  if(!valid) { protocolError(); return; }
  if(!storageFailed && error!="command_unresolved") error.clear();
  ++revision;
}
void Client::tick(uint32_t now,bool allowRpc,bool stationVisible) {
  if(!initialized) configure(storageConfigured?storageHooks:deviceStorage());
  if(!initialized || storage.corrupt() || stateCorrupt) return;
  if(storageFailed && (!storageRetryAt || uint32_t(now-storageRetryAt)>=2000)) {
    storageRetryAt=now;
    if(save()) ++revision;
  }
  std::string raw,failure;
  if(pending && guardian::rpcTake(raw,failure)) response(raw,failure,now);
  while(guardian::eventTake(raw)) {
    JsonDocument doc; Event event;
    if(!guardian::parseObject(doc,raw) || !parseEvent(doc.as<JsonVariantConst>(),event)) { protocolError(); break; }
    if(!acceptEvent(event)) {
      if(heldEvents.size()>=MaxItems) { guardian::rpcAbort(); heldEvents.clear(); break; }
      heldEvents.push_back(std::move(event));
    }
  }
  const bool online=guardian::snapshot(now).session.connected;
  if(!online) {
    if(connected) {
      connected=false; haveStation=haveCaps=haveTypes=false; sendIntent=false;
      // Disconnect may race the rpcTake above. Keep ownership until a later
      // tick consumes this request's error; otherwise reconnect stays blocked.
      heldEvents.clear(); recover=true;
      if(intent.unresolved) { command.state=CommandState::Unknown; command.error.code="disconnected"; save(); ++revision; }
    }
    return;
  }
  if(!connected) { connected=true; recover=true; haveStation=haveCaps=haveTypes=false; retryAt=0; commandAt=0; }
  if(!allowRpc || pending || !guardian::rpcReady() || (retryAt && int32_t(now-retryAt)<0)) return;
  JsonDocument doc;
  if(!haveCaps) { start("capabilities.get",doc,now); return; }
  if(!haveStation) { start("station.get",doc,now); return; }
  if(!haveTypes) { start("alert.types",doc,now); return; }
  if(sendIntent) {
    const auto* a=action(intent.radio,intent.action);
    if(!controlsReady(now) || !a || !a->supported || (!sendRetry && !a->available)) { sendIntent=false; command.state=CommandState::Unknown; error="unavailable"; ++revision; return; }
    if(!guardian::parseObject(doc,intent.fields)) { protocolError(); return; }
    doc["token"]=intent.token; doc["radio"]=intent.radio; doc["action"]=intent.action;
    if(start("control.execute",doc,now)) sendIntent=false;
    return;
  }
  if(!seenIds.empty()) { doc["alert_id"]=seenIds.front(); start("alert.seen",doc,now); return; }
  if(recover && !earlyCommandsFull) {
    if(!eventStream.empty()) doc["stream"]=eventStream;
    doc["after"]=eventCursor; doc["limit"]=5; start("events.list",doc,now); return;
  }
  if(ack && !eventStream.empty()) {
    doc["stream"]=eventStream; doc["cursor"]=eventCursor;
    if(start("events.ack",doc,now)) ack=false;
    return;
  }
  if(historyRequested) {
    doc["offset"]=historyOffset; doc["limit"]=5;
    if(historyOffset && !historyRevision.empty()) doc["revision"]=historyRevision;
    if(start("alerts.list",doc,now)) historyRequested=false;
    return;
  }
  if(intent.unresolved && !intent.command_id.empty() && uint32_t(now-commandAt)>=1500 && (command.state!=CommandState::Unknown || !commandAt)) {
    doc["command_id"]=intent.command_id; if(start("command.get",doc,now)) commandAt=now; return;
  }
  if(uint32_t(now-capAt)>=5000) { start("capabilities.get",doc,now); return; }
  if(uint32_t(now-stationAt)>=(stationVisible?1500u:5000u)) start("station.get",doc,now);
}
} }

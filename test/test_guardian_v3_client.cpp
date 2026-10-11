// Host integration: production coordinator, transport and durable store; only PC/NVS are faked.
#include "ui-touch/services/GuardianV3Client.h"
#include "ui-touch/services/GuardianRpcLink.h"
#include "ui-touch/services/GuardianLink.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::abort(); } } while(0)
using namespace guardian::v3;
const char* TR(const char* english) { return english; }
namespace {
const char* Stream="55555555-5555-4555-8555-555555555555";
const char* OtherStream="77777777-7777-4777-8777-777777777777";
const char* AlertId="44444444-4444-4444-8444-444444444444";
const char* CommandId="22222222-2222-4222-8222-222222222222";
std::string slots[2], outgoing, firstWriteState;
bool denied=false, torn=false, readUnavailable=false, complete=false, disconnectOnWrite=false;
uint8_t transfer=0;
unsigned writes=0, heartbeatSequence=0;
bool readSlot(unsigned slot,std::string& raw) { raw=slots[slot]; return !raw.empty(); }
bool strictReadSlot(unsigned slot,std::string& raw) {
  if(readUnavailable) { raw.clear(); return false; }
  raw=slots[slot]; return true;
}
bool writeSlot(unsigned slot,const std::string& raw) {
  ++writes; if(denied) return false;
  slots[slot]=torn?raw.substr(0,raw.size()/2):raw;
  if(disconnectOnWrite) { disconnectOnWrite=false; guardian::disconnected(); }
  return true;
}
StorageHooks hooks(bool strict=false) { return {strict?strictReadSlot:readSlot,writeSlot,strict}; }
std::string committed() {
  DurableStore probe; probe.configure(hooks()); std::string raw;
  probe.load(raw); return raw;
}
bool notify(const uint8_t* bytes,size_t size) {
  CHECK(size>=5 && size<=20);
  if(bytes[0]&1) {
    CHECK(outgoing.empty()); firstWriteState=committed(); transfer=bytes[1];
  }
  CHECK(bytes[1]==transfer);
  outgoing.append(reinterpret_cast<const char*>(bytes+4),size-4);
  if(bytes[0]&2) complete=true;
  return true;
}
void heartbeat(uint32_t now) {
  uint8_t status[]={'G','M',1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  const uint32_t seq=++heartbeatSequence;
  for(unsigned i=0;i<4;++i) status[16+i]=uint8_t(seq>>(8*i));
  CHECK(guardian::receive(status,sizeof status,now)==guardian::Result::Ok);
}
void feed(const std::string& raw,bool event,uint32_t now,uint8_t id=0) {
  for(size_t offset=0,index=0;offset<raw.size();offset+=16,++index) {
    const size_t count=std::min(size_t(16),raw.size()-offset);
    uint8_t bytes[20]={uint8_t((offset==0?1:0)|(offset+count==raw.size()?2:0)),id,uint8_t(index),uint8_t(index>>8)};
    std::copy(raw.begin()+offset,raw.begin()+offset+count,bytes+4);
    CHECK(event?guardian::eventReceive(bytes,count+4,now):guardian::rpcReceive(bytes,count+4,now));
  }
}
void parse(JsonDocument& doc,const std::string& raw) { CHECK(guardian::parseObject(doc,raw)); }
std::string alert(unsigned code=2,bool seen=false) {
  JsonDocument doc; doc["alert_id"]=AlertId; doc["code"]=code; doc["note"]="Cvičení: bod A.";
  doc["source"]="OK7PS"; doc["created"]=1791712800.0; doc["radio"]=1; doc["rf_id"]=123;
  doc["forwarded"]=false; doc["seen"]=seen; doc["origin"]["network"]="guardian_rf"; doc["origin"]["local"]=false;
  return guardian::jsonText(doc);
}
std::string event(uint64_t seq,const char* type="alert.created",const char* stream=Stream) {
  JsonDocument doc, data; doc["stream"]=stream; doc["history_truncated"]=false; doc["seq"]=seq;
  char id[37]; std::snprintf(id,sizeof id,"66666666-6666-4666-8666-%012llx",static_cast<unsigned long long>(seq & 0xffffffffffffULL));
  doc["event_id"]=id; doc["type"]=type; doc["created"]=1791712800.0;
  if(std::string(type)=="alert.created") parse(data,alert());
  else if(std::string(type)=="alert.seen") { data["alert_id"]=AlertId; data["peer"]="pc-device"; }
  else data["future_field"]=true;
  doc["data"]=data.as<JsonVariantConst>(); return guardian::jsonText(doc);
}
std::string eventPage(const std::vector<std::string>& events,uint64_t cursor,const char* stream=Stream,bool truncated=false,bool more=false) {
  JsonDocument doc; doc["stream"]=stream; doc["cursor"]=cursor; doc["latest"]=cursor;
  doc["truncated"]=truncated; doc["has_more"]=more; auto items=doc["items"].to<JsonArray>();
  for(const auto& raw:events) {
    JsonDocument item; parse(item,raw); item.remove("stream"); item.remove("history_truncated");
    items.add(item.as<JsonVariantConst>()); // Exactly the PC's history-item schema.
  }
  return guardian::jsonText(doc);
}
struct Fixture {
  Client client;
  uint32_t now=100;
  bool controlAllowed=true, freezeClock=false;
  JsonDocument request;
  std::vector<std::string> operations;
  std::deque<std::string> recoveryPages;
  Fixture(bool clear=true,bool strict=false,bool unavailable=false) {
    guardian::disconnected();
    std::string discarded,failure;
    while(guardian::rpcTake(discarded,failure)) {} // Isolate a new process from the previous test's abandoned request.
    guardian::rpcConnect();
    if(clear) { slots[0].clear(); slots[1].clear(); writes=0; }
    denied=torn=disconnectOnWrite=false; readUnavailable=unavailable; client.configure(hooks(strict)); connect();
  }
  void connect() {
    guardian::configure(true,true,true); guardian::connected(); guardian::rpcConnect(); guardian::rpcSubscribe(true);
    heartbeat(now);
  }
  void disconnect() {
    guardian::disconnected(); client.tick(++now,false);
  }
  bool next() {
    outgoing.clear(); firstWriteState.clear(); complete=false; request.clear();
    heartbeat(now); client.tick(now);
    if(!client.busy()) return false;
    for(unsigned i=0;i<4096 && !complete;++i) CHECK(guardian::rpcTick(now,notify));
    CHECK(complete); parse(request,outgoing); operations.push_back(request["op"].as<const char*>());
    return true;
  }
  std::string op() const { return request["op"] | ""; }
  void reply(const std::string& result) {
    JsonDocument doc, body; parse(body,result);
    doc["id"]=request["id"].as<const char*>(); doc["ok"]=true; doc["result"]=body.as<JsonVariantConst>();
    feed(guardian::jsonText(doc),false,now,transfer); if(!freezeClock) ++now; client.tick(now,false);
  }
  void reject(const char* code) {
    JsonDocument doc; doc["id"]=request["id"].as<const char*>(); doc["ok"]=false;
    doc["error"]["code"]=code; doc["error"]["message"]=code;
    feed(guardian::jsonText(doc),false,now,transfer); client.tick(++now,false);
  }
  std::string commandResult(const char* state="pending",bool accepted=true,const char* code=nullptr) const {
    JsonDocument doc; doc["command_id"]=CommandId; doc["action"]=client.intent.action;
    doc["radio"]=client.intent.radio; doc["state"]=state; doc["accepted"]=accepted;
    doc["duplicate"]=false; doc["worker"]="radio-control"; doc["created"]=1791712800.0;
    if(code) { doc["error"]["code"]=code; doc["error"]["message"]=code; }
    return guardian::jsonText(doc);
  }
  void automatic() {
    if(op()=="capabilities.get") {
      JsonDocument doc; doc["api_version"]=3; doc["control_allowed"]=controlAllowed;
      doc["operations"].to<JsonArray>().add("station.get"); auto all=doc["actions"].to<JsonArray>();
      const char* actions[]={"radio.connect","radio.disconnect","modem.connect","modem.disconnect","control.start","control.stop","beacon.send","radio.tune","profile.apply","calibration.start","calibration.cancel","scanner.start"};
      for(const auto* name:actions) all.add(name);
      doc["limits"]["json_bytes"]=32768; doc["limits"]["display_note_chars"]=512;
      auto radios=doc["radios"].to<JsonArray>();
      for(unsigned i=1;i<=2;++i) {
        auto radio=radios.add<JsonObject>(); radio["radio"]=i; radio["profiles"].to<JsonArray>().add("VHF");
        radio["total_profiles"]=1; radio["tune_mode"]="frequency_only"; radio["tune_modes"].to<JsonArray>().add("FM");
        for(const auto* name:actions) { radio["actions"][name]["supported"]=true; radio["actions"][name]["available"]=true; radio["actions"][name]["reason"]=nullptr; }
      }
      reply(guardian::jsonText(doc));
    } else if(op()=="station.get") {
      reply(R"({"version":"1.1.25","callsign":"OK7PS","grid":"JO70","updated_at":1791712800,"revision":7,"ready":true,"readiness_verified":true,
        "mailbox":{"inbox":1,"unread":1,"outbox":0,"outbox_failed":0,"transit":0},"server":{"enabled":false,"state":"disabled","last_verified":null},"alerts":{"total":0,"unseen":0},
        "radios":[{"radio":1,"backend":"guardian_k5","name":"K5","connected":true,"frequency_hz":145500000,"mode":"FM","ptt":false,"signal":null,"error":null,"ready":true,"readiness_verified":true,"busy":false,"pending_operations":[],"total_sessions":0,
        "modem":{"selected":"vara_p2p","name":"VARA","configured":true,"started":true,"state":"connected","ready":true,"readiness_verified":true,"lifecycle":"connection","vara":{"command_connected":true,"data_connected":true}},
        "control":{"active":true,"active_sessions":0},"transfers":[]},
        {"radio":2,"backend":"guardian_k5","name":"K5 second","connected":false,"frequency_hz":null,"ready":false,"readiness_verified":true,"transfers":[]}]})");
    } else if(op()=="alert.types") {
      reply(R"({"items":[{"code":1,"key":"alert.mayday","label":"MAYDAY","priority":3},{"code":2,"key":"alert.medical","label":"Zdravotní nouze","priority":2},{"code":3,"key":"alert.evacuation","label":"Evakuace","priority":3}],"display_note_chars":512,"mesh_forwarding":false})");
    } else if(op()=="events.list") {
      if(!recoveryPages.empty()) { const auto page=recoveryPages.front(); recoveryPages.pop_front(); reply(page); }
      else reply(eventPage({},request["after"] | uint64_t(0),client.stream().empty()?Stream:client.stream().c_str()));
    } else if(op()=="events.ack") {
      JsonDocument doc; doc["stream"]=request["stream"].as<const char*>(); doc["cursor"]=request["cursor"].as<uint64_t>(); reply(guardian::jsonText(doc));
    } else if(op()=="alerts.list") {
      reply(R"({"items":[],"total":0,"revision":"abcdef0123456789","next_offset":null})");
    } else if(op()=="alert.seen") {
      JsonDocument doc; doc["alert_id"]=request["alert_id"].as<const char*>(); doc["seen"]=true; reply(guardian::jsonText(doc));
    } else if(op()=="command.get") reply(commandResult("completed"));
    else CHECK(false); // Never auto-answer or hide a side effect.
  }
  void idle(unsigned max=30) {
    for(unsigned i=0;i<max;++i) { if(!next()) return; CHECK(op()!="control.execute" && op()!="alert.publish"); automatic(); }
    CHECK(false);
  }
  void inject(const std::string& raw) { feed(raw,true,now,transfer); client.tick(++now,false); }
};
void bootstrapAndCommands() {
  Fixture f; CHECK(!f.client.controlsReady(f.now)); f.idle();
  CHECK(f.operations.size()>=5 && f.operations[0]=="capabilities.get" && f.operations[1]=="station.get" && f.operations[2]=="alert.types");
  CHECK(std::count(f.operations.begin(),f.operations.end(),"events.list")>=1);
  CHECK(std::count(f.operations.begin(),f.operations.end(),"alerts.list")>=1);
  CHECK(f.client.historyLoaded && f.client.stationFresh(f.now));
  CHECK(f.client.controlsReady(f.now) && f.client.radio(2)->radio==2);
  CHECK(f.client.alertLabel(2)=="Zdravotní nouze");
  const unsigned before=writes;
  CHECK(f.client.execute(2,"radio.connect","{}",f.now)); CHECK(writes>before && uuid(f.client.intent.token));
  const std::string token=f.client.intent.token;
  CHECK(f.next() && f.op()=="control.execute");
  CHECK(f.request["radio"].as<unsigned>()==2 && f.request["action"]=="radio.connect" && f.request["token"]==token);
  JsonDocument persisted; parse(persisted,firstWriteState);
  CHECK(persisted["intent"]["token"]==token && persisted["intent"]["radio"].as<unsigned>()==2);
  f.reply(f.commandResult()); CHECK(f.client.command.state==CommandState::Pending && !f.client.command.succeeded());
  CHECK(f.client.intent.unresolved && f.client.intent.command_id==CommandId);
  f.idle(); f.now+=1600;
  CHECK(f.next() && f.op()=="command.get" && f.request["command_id"]==CommandId);
  f.reply(f.commandResult("completed")); CHECK(f.client.command.succeeded() && !f.client.intent.unresolved);
  f.idle(); CHECK(f.client.execute(1,"beacon.send","{}",f.now)); CHECK(f.next() && f.op()=="control.execute");
  f.reply(f.commandResult("queued")); CHECK(f.client.commandLabel()=="Beacon queued" && !f.client.intent.unresolved);
  f.idle(); CHECK(f.client.execute(1,"radio.disconnect","{}",f.now)); CHECK(f.next());
  f.reply(f.commandResult("failed",true,"operation_failed")); CHECK(!f.client.command.succeeded() && !f.client.intent.unresolved && f.client.commandLabel()=="Failed");
  f.idle(); CHECK(f.client.execute(1,"radio.connect","{}",f.now)); CHECK(f.next());
  f.reply(f.commandResult("failed",false,"busy")); CHECK(!f.client.command.accepted && !f.client.command.succeeded());
  f.idle(); CHECK(f.client.execute(1,"radio.connect","{}",f.now)); CHECK(f.next());
  f.reject("control_denied"); CHECK(f.client.error=="control_denied" && f.client.command.requiresDecision());
}
void gatesAndFields() {
  Fixture f; f.idle();
  CHECK(!f.client.execute(0,"radio.connect","{}",f.now)); CHECK(!f.client.execute(3,"radio.connect","{}",f.now));
  CHECK(!f.client.execute(1,"scanner.start","{}",f.now)); CHECK(!f.client.execute(1,"advert.send","{}",f.now));
  CHECK(!f.client.execute(1,"alert.publish","{}",f.now));
  for(auto& radio:f.client.capabilities.radios) if(radio.radio==1) {
    for(auto& item:radio.actions) if(item.action=="radio.connect") item.supported=false;
  }
  CHECK(!f.client.execute(1,"radio.connect","{}",f.now));
  CHECK(!f.client.execute(2,"radio.tune",R"({"frequency_hz":145500003,"mode":"FM"})",f.now));
  CHECK(!f.client.execute(2,"radio.tune",R"({"frequency_hz":145500000,"mode":"USB"})",f.now));
  CHECK(!f.client.execute(2,"radio.tune",R"({"frequency_hz":10000000001,"mode":"FM"})",f.now));
  CHECK(!f.client.execute(2,"radio.tune",R"({"frequency_hz":-10,"mode":"FM"})",f.now));
  CHECK(!f.client.execute(2,"radio.tune",R"({"frequency_hz":145500000.5,"mode":"FM"})",f.now));
  CHECK(!f.client.execute(2,"profile.apply",R"({"profile":"not stored"})",f.now));
  CHECK(!f.client.execute(2,"profile.apply",R"({"profile":"VHF","radio":1})",f.now));
  CHECK(!f.client.execute(2,"radio.connect",R"({"token":"replacement"})",f.now));
  CHECK(!f.client.execute(2,"calibration.start",R"({"peer":"OK 1ABC","mode":"quick"})",f.now));
  CHECK(!f.client.execute(2,"calibration.start",R"({"peer":"OK1ABC","mode":"unsupported"})",f.now));
  CHECK(f.client.execute(2,"radio.tune",R"({"frequency_hz":10000000000,"mode":"FM"})",f.now));
  CHECK(f.next() && f.op()=="control.execute" && f.request["frequency_hz"].as<uint64_t>()==MaxFrequency);
  f.reply(f.commandResult("completed")); f.idle();
  f.now+=15000; CHECK(!f.client.stationFresh(f.now) && !f.client.controlsReady(f.now));
  CHECK(!f.client.execute(2,"radio.connect","{}",f.now));
  Fixture deniedPc; deniedPc.controlAllowed=false; deniedPc.idle();
  CHECK(!deniedPc.client.execute(1,"radio.connect","{}",deniedPc.now) && deniedPc.client.error=="control_denied");
}
void durableFailures() {
  Fixture f; f.idle(); denied=true;
  CHECK(!f.client.execute(1,"radio.connect","{}",f.now)); CHECK(f.client.intent.token.empty());
  CHECK(!f.next()); denied=false;
  Fixture cut; cut.idle(); torn=true;
  CHECK(!cut.client.execute(1,"radio.connect","{}",cut.now)); CHECK(cut.client.intent.token.empty());
  torn=false; Fixture reboot(false); reboot.idle(); CHECK(reboot.client.intent.token.empty());
  CHECK(std::find(reboot.operations.begin(),reboot.operations.end(),"control.execute")==reboot.operations.end());
  Fixture eventFailure; eventFailure.idle(); denied=true;
  eventFailure.inject(event(1)); CHECK(eventFailure.client.cursor()==0 && !eventFailure.client.emergency());
  CHECK(eventFailure.next() && eventFailure.op()=="events.list");
  eventFailure.reply(eventPage({event(1)},1)); CHECK(eventFailure.client.cursor()==0);
  eventFailure.now+=2000; CHECK(eventFailure.next() && eventFailure.op()=="events.list");
  CHECK(std::find(eventFailure.operations.begin(),eventFailure.operations.end(),"events.ack")==eventFailure.operations.end());
  denied=false;
}
void alertsAndRecovery() {
  Fixture f; f.idle(); f.inject(event(1)); CHECK(f.client.cursor()==1 && f.client.emergency());
  CHECK(!f.client.emergency()->seen);
  CHECK(f.next() && f.op()=="events.ack" && f.request["cursor"].as<uint64_t>()==1); f.automatic();
  CHECK(f.client.emergency() && !f.client.emergency()->seen); // Transport receipt is not reading.
  denied=true; CHECK(!f.client.seen(AlertId)); CHECK(f.client.emergency() && !f.client.emergency()->seen);
  denied=false; const auto before=writes; CHECK(f.client.seen(AlertId)); CHECK(writes>before && !f.client.emergency());
  JsonDocument stored; parse(stored,committed()); CHECK(stored["alerts"][0]["seen"].as<bool>());
  CHECK(f.next() && f.op()=="alert.seen" && f.request["alert_id"]==AlertId); f.automatic();
  Fixture reboot(false); CHECK(!reboot.client.emergency()); reboot.idle(); CHECK(!reboot.client.emergency());
  Fixture gap; gap.idle(); gap.inject(event(2)); CHECK(gap.client.cursor()==0 && !gap.client.emergency());
  CHECK(gap.next() && gap.op()=="events.list" && gap.request["after"].as<uint64_t>()==0);
  gap.reply(eventPage({event(1),event(2)},2)); CHECK(gap.client.cursor()==2 && gap.client.emergency());
  CHECK(gap.next() && gap.op()=="events.ack" && gap.request["cursor"].as<uint64_t>()==2); gap.automatic();
  const auto writesBefore=writes; gap.inject(event(2)); CHECK(gap.client.cursor()==2 && writes==writesBefore);
  CHECK(gap.next() && gap.op()=="events.ack"); gap.automatic();
  gap.inject(event(3,"future.event")); CHECK(gap.client.cursor()==3 && gap.client.emergency());
  CHECK(gap.next() && gap.op()=="events.ack" && gap.request["cursor"].as<uint64_t>()==3); gap.automatic();
  gap.inject(event(4,"alert.seen")); CHECK(gap.client.cursor()==4 && !gap.client.emergency());
  CHECK(gap.next() && gap.op()=="events.ack"); gap.automatic();
  CHECK(std::count(gap.operations.begin(),gap.operations.end(),"alert.seen")==0);
  CHECK(std::count(gap.operations.begin(),gap.operations.end(),"control.execute")==0);
}
void cursorAndStream() {
  guardian::disconnected(); slots[0].clear(); slots[1].clear(); denied=torn=false;
  DurableStore seed; seed.configure(hooks()); JsonDocument doc; doc["version"]=1; doc["stream"]=Stream;
  doc["cursor"]=4294967296ULL; doc["alerts"].to<JsonArray>(); doc["event_ids"].to<JsonArray>(); doc["seen_ids"].to<JsonArray>();
  CHECK(seed.save(guardian::jsonText(doc)));
  Fixture big(false); CHECK(big.client.cursor()==4294967296ULL); big.idle();
  big.inject(event(4294967297ULL)); CHECK(big.client.cursor()==4294967297ULL);
  CHECK(big.next() && big.op()=="events.ack" && big.request["cursor"].as<uint64_t>()==4294967297ULL); big.automatic();
  big.inject(event(8,"future.event",OtherStream));
  CHECK(big.next() && big.op()=="events.list"); big.reply(eventPage({event(8,"future.event",OtherStream)},8,OtherStream,true));
  CHECK(big.client.stream()==OtherStream && big.client.cursor()==8 && big.client.historyTruncated);
  big.idle(); CHECK(std::count(big.operations.begin(),big.operations.end(),"control.execute")==0);
}
void corruptStorageAndBootRetry() {
  guardian::disconnected(); slots[0].clear(); slots[1].clear(); denied=torn=false;
  JsonDocument state; state["version"]=1; state["stream"]=Stream; state["cursor"]=0;
  state["alerts"].to<JsonArray>(); state["event_ids"].to<JsonArray>(); state["seen_ids"].to<JsonArray>();
  state["intent"]["token"]="bad UUID"; state["intent"]["radio"]=1;
  state["intent"]["action"]="radio.connect"; state["intent"]["fields"]="{}"; state["intent"]["unresolved"]=true;
  DurableStore seed; seed.configure(hooks()); CHECK(seed.save(guardian::jsonText(state))); // Valid CRC, invalid semantics.
  const auto before=writes; Fixture corrupt(false);
  CHECK(corrupt.client.storageFailed && corrupt.client.error=="storage_error" && !corrupt.next());
  CHECK(!corrupt.client.execute(1,"radio.connect","{}",corrupt.now));
  CHECK(!corrupt.client.seen(AlertId) && writes==before);
  // A temporary denied lease must retry these same hooks, preserving persisted alert/cursor data.
  slots[0].clear(); slots[1].clear(); state.remove("intent"); state["cursor"]=3;
  JsonDocument emergency; parse(emergency,alert()); state["alerts"].as<JsonArray>().add(emergency.as<JsonVariantConst>());
  seed.configure(hooks()); CHECK(seed.save(guardian::jsonText(state)));
  Fixture blocked(false,true,true); CHECK(blocked.client.storageFailed && !blocked.next());
  const auto blockedWrites=writes; CHECK(!blocked.client.seen(AlertId)); CHECK(!blocked.client.execute(1,"radio.connect","{}",blocked.now));
  CHECK(writes==blockedWrites && blocked.operations.empty());
  readUnavailable=false; blocked.idle();
  CHECK(!blocked.client.storageFailed && blocked.client.cursor()==3 && blocked.client.emergency());
  CHECK(std::count(blocked.operations.begin(),blocked.operations.end(),"control.execute")==0);
  Fixture empty(true,true,false); CHECK(!empty.client.storageFailed); empty.idle(); CHECK(empty.client.historyLoaded);
}
void zeroTimestampAndRefresh() {
  Fixture f; f.now=0; f.freezeClock=true; f.idle();
  CHECK(f.client.stationFresh(0) && f.client.controlsReady(0));
  CHECK(!f.next()); // An actual snapshot at millis()==0 must not look like an uninitialized poll.
  const auto before=std::count(f.operations.begin(),f.operations.end(),"station.get");
  f.client.refreshStation(); CHECK(!f.client.stationFresh(0)); f.idle();
  CHECK(f.client.stationFresh(0)); CHECK(std::count(f.operations.begin(),f.operations.end(),"station.get")==before+1);
  CHECK(!f.next()); f.now=15000; CHECK(!f.client.stationFresh(f.now));
}
void commandEventsAndMalformedReply() {
  Fixture f; f.idle(); CHECK(f.client.execute(1,"radio.connect","{}",f.now));
  CHECK(f.next()); f.reply(f.commandResult());
  JsonDocument completed,payload; parse(completed,event(1,"command.changed")); parse(payload,f.commandResult("completed"));
  completed["data"]=payload.as<JsonVariantConst>(); f.inject(guardian::jsonText(completed));
  CHECK(f.client.command.succeeded() && !f.client.intent.unresolved && f.client.cursor()==1);
  JsonDocument durable; parse(durable,committed());
  CHECK(durable["command"]["state"]=="completed" && durable["cursor"].as<uint64_t>()==1);
  f.idle(); CHECK(std::count(f.operations.begin(),f.operations.end(),"events.ack")>=1);
  Fixture wrongEvent; wrongEvent.idle(); CHECK(wrongEvent.client.execute(1,"radio.connect","{}",wrongEvent.now));
  CHECK(wrongEvent.next()); wrongEvent.reply(wrongEvent.commandResult());
  parse(payload,wrongEvent.commandResult("completed")); payload["radio"]=2; payload["action"]="radio.disconnect";
  completed["data"]=payload.as<JsonVariantConst>(); wrongEvent.inject(guardian::jsonText(completed));
  CHECK(wrongEvent.client.command.radio==1 && wrongEvent.client.command.action=="radio.connect");
  CHECK(!wrongEvent.client.command.succeeded() && wrongEvent.client.intent.unresolved);
  CHECK(wrongEvent.client.cursor()==0);
  Fixture malformed; malformed.idle(); CHECK(malformed.client.execute(2,"radio.connect","{}",malformed.now));
  const auto token=malformed.client.intent.token; CHECK(malformed.next());
  parse(payload,malformed.commandResult("completed")); payload["radio"]=1;
  malformed.reply(guardian::jsonText(payload));
  CHECK(malformed.client.error=="protocol_error" && malformed.client.command.requiresDecision());
  CHECK(malformed.client.intent.token==token && malformed.client.intent.unresolved);
}
void concurrentEventsAndResponses() {
  Fixture midTick; midTick.idle(); CHECK(midTick.client.execute(1,"radio.connect","{}",midTick.now));
  const auto midTickToken=midTick.client.intent.token;
  CHECK(midTick.next() && midTick.op()=="control.execute");
  // The first rpcTake sees no reply. Committing an Event then drops the link
  // before tick reads its connection snapshot, leaving this RPC's error owned.
  disconnectOnWrite=true; midTick.inject(event(1));
  CHECK(midTick.client.cursor()==1 && midTick.client.emergency());
  midTick.connect(); midTick.idle();
  CHECK(guardian::rpcReady() && !midTick.client.busy() && midTick.client.stationFresh(midTick.now));
  CHECK(midTick.client.intent.token==midTickToken && midTick.client.intent.unresolved && midTick.client.command.requiresDecision());
  CHECK(std::count(midTick.operations.begin(),midTick.operations.end(),"control.execute")==1);
  Fixture early; early.idle(); CHECK(early.client.execute(1,"radio.connect","{}",early.now));
  CHECK(early.next() && early.op()=="control.execute");
  JsonDocument earlyNotice,earlyPayload; parse(earlyNotice,event(1,"command.changed")); parse(earlyPayload,early.commandResult("completed"));
  earlyNotice["data"]=earlyPayload.as<JsonVariantConst>(); early.inject(guardian::jsonText(earlyNotice));
  CHECK(early.client.cursor()==1 && early.client.intent.command_id.empty() && early.client.intent.unresolved);
  JsonDocument savedEarly; parse(savedEarly,committed()); CHECK(savedEarly["early_commands"].size()==1);
  early.reply(early.commandResult("pending"));
  CHECK(early.client.command.succeeded() && !early.client.intent.unresolved);
  parse(savedEarly,committed()); CHECK(savedEarly["early_commands"].size()==0);
  Fixture earlyLost; earlyLost.idle(); CHECK(earlyLost.client.execute(1,"radio.connect","{}",earlyLost.now));
  CHECK(earlyLost.next()); parse(earlyPayload,earlyLost.commandResult("completed")); earlyNotice["data"]=earlyPayload.as<JsonVariantConst>();
  earlyLost.inject(guardian::jsonText(earlyNotice)); earlyLost.inject(event(2));
  CHECK(earlyLost.client.cursor()==2 && earlyLost.client.emergency()); earlyLost.disconnect();
  Fixture earlyBoot(false); earlyBoot.idle(); CHECK(earlyBoot.client.cursor()==2 && earlyBoot.client.intent.command_id.empty());
  CHECK(std::count(earlyBoot.operations.begin(),earlyBoot.operations.end(),"control.execute")==0);
  for(auto& radio:earlyBoot.client.capabilities.radios) for(auto& a:radio.actions)
    if(radio.radio==1 && a.action=="radio.connect") a.available=false; // Already applied on PC.
  CHECK(earlyBoot.client.retryIntent(earlyBoot.now)); CHECK(earlyBoot.next() && earlyBoot.op()=="control.execute");
  earlyBoot.reply(earlyBoot.commandResult("pending")); CHECK(earlyBoot.client.command.succeeded() && !earlyBoot.client.intent.unresolved);
  Fixture overflow; overflow.idle(); CHECK(overflow.client.execute(1,"radio.connect","{}",overflow.now)); CHECK(overflow.next());
  for(unsigned seq=1;seq<=21;++seq) {
    parse(earlyNotice,event(seq,"command.changed")); parse(earlyPayload,overflow.commandResult("completed"));
    char candidateId[37]; std::snprintf(candidateId,sizeof candidateId,"99999999-9999-4999-8999-%012x",seq);
    earlyPayload["command_id"]=candidateId; earlyNotice["data"]=earlyPayload.as<JsonVariantConst>(); overflow.inject(guardian::jsonText(earlyNotice));
  }
  CHECK(overflow.client.cursor()==20); overflow.reject("uncertain_result"); overflow.now+=16000; overflow.idle();
  overflow.inject(event(22)); CHECK(overflow.client.emergency() && overflow.client.cursor()==20); overflow.now+=5100;
  bool refreshedAlerts=false;
  for(unsigned attempt=0;attempt<15 && !refreshedAlerts;++attempt) {
    CHECK(overflow.next());
    if(overflow.op()=="alerts.list") {
      JsonDocument page,item; page["revision"]="overflow-history"; page["total"]=1; page["next_offset"]=nullptr;
      parse(item,alert()); page["items"].to<JsonArray>().add(item.as<JsonVariantConst>()); overflow.reply(guardian::jsonText(page)); refreshedAlerts=true;
    } else overflow.automatic();
  }
  CHECK(refreshedAlerts && overflow.client.emergency());
  CHECK(overflow.client.stationFresh(overflow.now) && overflow.client.cursor()==20 && overflow.client.discardIntent(overflow.now));
  Fixture reconnect; reconnect.idle();
  const uint8_t malformed[]={2,1,0,0,'{','}'};
  CHECK(!guardian::eventReceive(malformed,sizeof malformed,reconnect.now));
  CHECK(!guardian::rpcTick(reconnect.now,nullptr)); reconnect.disconnect(); reconnect.connect(); reconnect.idle();
  CHECK(reconnect.client.stationFresh(reconnect.now) && guardian::rpcReady());
  Fixture seen; seen.idle(); seen.inject(event(1));
  CHECK(seen.client.seen(AlertId)); CHECK(seen.next() && seen.op()=="alert.seen");
  seen.inject(event(2,"alert.seen")); seen.automatic();
  CHECK(seen.client.error.empty() && seen.client.cursor()==2 && !seen.client.emergency()); seen.idle();
  Fixture history; history.idle(); history.inject(event(3,"future.event"));
  CHECK(history.next() && history.op()=="events.list");
  history.inject(event(1,"future.event")); history.inject(event(2,"future.event")); history.inject(event(3,"future.event"));
  history.reply(eventPage({event(1,"future.event"),event(2,"future.event")},2));
  CHECK(history.client.error.empty() && history.client.cursor()==3); history.idle();
  Fixture pages; pages.idle(); pages.client.requestHistory();
  CHECK(pages.next() && pages.op()=="alerts.list" && pages.request["offset"]==0);
  pages.client.requestHistory(5,"next-revision");
  pages.reply(R"({"items":[],"total":10,"revision":"old-revision","next_offset":5})");
  CHECK(pages.client.error.empty() && pages.client.history.revision!="old-revision");
  CHECK(pages.next() && pages.op()=="alerts.list" && pages.request["offset"]==5 && pages.request["revision"]=="next-revision"); pages.automatic();
  Fixture oldSeen; oldSeen.idle(); oldSeen.inject(event(1)); oldSeen.client.requestHistory();
  CHECK(oldSeen.next() && oldSeen.op()=="events.ack"); oldSeen.automatic();
  CHECK(oldSeen.next() && oldSeen.op()=="alerts.list");
  oldSeen.inject(event(2,"alert.seen"));
  JsonDocument stale,staleAlert; parse(staleAlert,alert()); stale["items"].to<JsonArray>().add(staleAlert.as<JsonVariantConst>());
  stale["total"]=1; stale["revision"]="revision"; stale["next_offset"]=nullptr;
  oldSeen.reply(guardian::jsonText(stale));
  CHECK(!oldSeen.client.emergency() && oldSeen.client.history.items[0].seen);
  Fixture command; command.idle(); CHECK(command.client.execute(1,"radio.connect","{}",command.now));
  CHECK(command.next()); command.reply(command.commandResult()); command.idle(); command.now+=1600;
  CHECK(command.next() && command.op()=="command.get");
  JsonDocument notice,payload; parse(notice,event(1,"command.changed")); parse(payload,command.commandResult("completed"));
  notice["data"]=payload.as<JsonVariantConst>(); command.inject(guardian::jsonText(notice));
  command.reply(command.commandResult("pending"));
  CHECK(command.client.command.succeeded() && !command.client.intent.unresolved);
  Fixture failed; failed.idle(); CHECK(failed.client.execute(1,"radio.connect","{}",failed.now));
  CHECK(failed.next()); failed.reply(failed.commandResult()); failed.idle(); failed.now+=1600;
  CHECK(failed.next() && failed.op()=="command.get");
  notice["seq"]=1; notice["event_id"]="88888888-8888-4888-8888-888888888888";
  failed.inject(guardian::jsonText(notice)); failed.reject("not_found");
  CHECK(failed.client.command.succeeded() && !failed.client.intent.unresolved);
}
void lostRepliesAndBoot() {
  Fixture lost; lost.idle(); CHECK(lost.client.execute(2,"radio.connect","{}",lost.now));
  const auto token=lost.client.intent.token; CHECK(lost.next() && lost.op()=="control.execute");
  lost.disconnect(); CHECK(lost.client.intent.unresolved && lost.client.intent.token==token && lost.client.command.requiresDecision());
  CHECK(!lost.client.discardIntent(lost.now));
  lost.connect(); lost.idle(); CHECK(lost.client.intent.token==token && lost.client.intent.unresolved);
  CHECK(std::count(lost.operations.begin(),lost.operations.end(),"control.execute")==1);
  Fixture boot(false); boot.idle(); CHECK(boot.client.intent.token==token && boot.client.intent.unresolved);
  CHECK(std::count(boot.operations.begin(),boot.operations.end(),"control.execute")==0);
  boot.inject(event(8,"future.event",OtherStream)); CHECK(boot.next() && boot.op()=="events.list");
  boot.reply(eventPage({event(8,"future.event",OtherStream)},8,OtherStream,true)); boot.idle();
  CHECK(boot.client.stream()==OtherStream && boot.client.historyTruncated && boot.client.intent.token==token);
  CHECK(boot.client.intent.unresolved && std::count(boot.operations.begin(),boot.operations.end(),"control.execute")==0);
  CHECK(boot.client.discardIntent(boot.now)); CHECK(!boot.client.intent.unresolved); boot.idle();
  Fixture known; known.idle(); CHECK(known.client.execute(1,"radio.connect","{}",known.now));
  CHECK(known.next()); known.reply(known.commandResult()); CHECK(known.client.intent.command_id==CommandId);
  known.disconnect(); CHECK(known.client.command.requiresDecision());
  Fixture withId(false); withId.idle();
  withId.now+=1600;
  bool lookedUp=std::count(withId.operations.begin(),withId.operations.end(),"command.get")>0;
  for(unsigned i=0;i<10 && !lookedUp;++i) {
    if(!withId.next()) { withId.now+=1600; continue; }
    CHECK(withId.op()!="control.execute");
    lookedUp=withId.op()=="command.get"; withId.automatic();
  }
  CHECK(lookedUp); // Restart/reconnect may retrieve a result; they never repeat the effect.
  CHECK(std::count(withId.operations.begin(),withId.operations.end(),"control.execute")==0);
  CHECK(withId.client.command.succeeded());
}
}
int main() {
  bootstrapAndCommands(); gatesAndFields(); durableFailures(); alertsAndRecovery(); cursorAndStream(); corruptStorageAndBootRetry(); zeroTimestampAndRefresh(); commandEventsAndMalformedReply(); concurrentEventsAndResponses(); lostRepliesAndBoot();
  guardian::disconnected();
  puts("Guardian v3 Client: bootstrap, radio gates, intent-before-wire, states, restart, lost reply, recovery/dedup, durable seen, no implicit effects PASS");
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/GuardianAppScreen.h"
#include "services/GuardianRpcLink.h"
#include "services/GuardianLink.h"
#include "services/GuardianJson.h"
#include "i18n.h"
#include "theme/Theme.h"
#include "theme/Fonts.h"
#include "services/GuardianAppearance.h"
#include "services/GuardianV3Client.h"
#include <Arduino.h>
#include <stdexcept>
#include <cstring>
// Use the real firmware status bar for visual captures as well as the app body.
void guardSimGuardianAppTitle(const char* title);
namespace {
int homes = 0;
void check(bool ok,const char* why) { if (!ok) throw std::runtime_error(why); }
lv_obj_t* find(lv_obj_t* root,const char* text) {
  if (lv_obj_check_type(root,&lv_label_class) && !strcmp(lv_label_get_text(root),text)) return root;
  for (unsigned i=0;i<lv_obj_get_child_cnt(root);++i) if (auto* p=find(lv_obj_get_child(root,i),text)) return p;
  return nullptr;
}
void dump(lv_obj_t* root) {
  if(lv_obj_check_type(root,&lv_label_class)) printf("Guardian label: %s\n",lv_label_get_text(root));
  for(unsigned i=0;i<lv_obj_get_child_cnt(root);++i) dump(lv_obj_get_child(root,i));
}
void click(lv_obj_t* root,const char* text) {
  lv_obj_update_layout(root);
  auto* label=find(root,text); check(label!=nullptr,text);
  lv_event_send(lv_obj_get_parent(label),LV_EVENT_CLICKED,nullptr);
}
std::string wire; uint8_t transfer;
std::string v3Slots[2];
bool readV3Slot(unsigned slot,std::string& raw) { raw=v3Slots[slot]; return !raw.empty(); }
bool writeV3Slot(unsigned slot,const std::string& raw) { v3Slots[slot]=raw; return true; }
uint32_t testNow=0;
ui::screens::GuardianAppScreen* activeScreen=nullptr;
bool notification(const uint8_t* p,size_t n) { transfer=p[1]; wire.append((const char*)p+4,n-4); return true; }
void refresh(ui::screens::GuardianAppScreen& screen,uint32_t now) {
  testNow=std::max(testNow,now); screen.refresh(testNow);
}
JsonDocument rawRequest() {
  testNow=std::max<uint32_t>(testNow,millis());
  wire.clear();
  for (int i=0;i<2200;++i) { size_t before=wire.size(); guardian::rpcTick(testNow,notification); if (wire.size()==before) break; }
  JsonDocument doc;
  if(!guardian::parseObject(doc,wire)) {
    std::string abandoned,transportError; guardian::rpcTake(abandoned,transportError);
    const auto state=guardian::snapshot(testNow);
    printf("Guardian request diagnostics: bytes=%lu, RPC busy=%d, ready=%d, Client busy=%d, now=%lu, heartbeat=%lu, connected=%d, failure=%s, client=%s\n",(unsigned long)wire.size(),guardian::rpcBusy(),guardian::rpcReady(),guardian::v3::client().busy(),(unsigned long)testNow,(unsigned long)state.session.receivedAt,state.session.connected,transportError.c_str(),guardian::v3::client().error.c_str());
    dump(lv_layer_top());
    check(false,"Invalid RPC request");
  }
  return doc;
}
void reply(JsonDocument& doc,ui::screens::GuardianAppScreen& screen) {
  const auto json=guardian::jsonText(doc);
  for (size_t pos=0,index=0;pos<json.size();pos+=16,++index) {
    const size_t n=std::min(size_t(16),json.size()-pos); uint8_t p[20];
    p[0]=(pos==0?1:0)|(pos+n==json.size()?2:0); p[1]=transfer; p[2]=uint8_t(index); p[3]=uint8_t(index>>8);
    memcpy(p+4,json.data()+pos,n); check(guardian::rpcReceive(p,n+4,testNow),"Response framing failed");
  }
  refresh(screen,testNow);
  // LVGL temporarily ellipsizes new labels before their parent's first layout.
  // Assert the laid-out screen, also when no screenshot/render tick follows.
  lv_obj_update_layout(lv_layer_top());
}
bool backgroundReply(JsonDocument& request,JsonDocument& response,bool history=false) {
  const std::string op=request["op"] | "";
  response.clear(); response["id"]=request["id"]; response["ok"]=true;
  auto result=response["result"].to<JsonObject>();
  const char* const actions[]={"radio.connect","radio.disconnect","modem.connect","modem.disconnect","control.start","control.stop","beacon.send","radio.tune","profile.apply","calibration.start","calibration.cancel"};
  if (op=="capabilities.get") {
    result["api_version"]=3; result["control_allowed"]=true;
    result["operations"].to<JsonArray>(); result["actions"].to<JsonArray>(); result["limits"].to<JsonObject>();
    auto radios=result["radios"].to<JsonArray>();
    for(unsigned number=1;number<=2;++number) {
      auto radio=radios.add<JsonObject>(); radio["radio"]=number;
      radio["tune_mode"]=number==1?"cat":"frequency_only";
      auto modes=radio["tune_modes"].to<JsonArray>(); modes.add(number==1?"USB":"FM");
      auto profiles=radio["profiles"].to<JsonArray>(); profiles.add(number==1?"HF VARA":"VHF FM"); radio["total_profiles"]=1;
      for(auto action:actions) {
        auto item=radio["actions"][action].to<JsonObject>();
        item["supported"]=true; item["available"]=true; item["reason"]="";
      }
    }
  } else if(op=="station.get") {
    result["version"]="1.1.25"; result["callsign"]="OK7PS"; result["grid"]="JO70";
    result["ready"]=true; result["readiness_verified"]=true; result["revision"]=1;
    result["mailbox"]["inbox"]=5; result["mailbox"]["unread"]=2; result["mailbox"]["outbox"]=1;
    result["server"]["enabled"]=true; result["server"]["state"]="connected";
    result["alerts"]["total"]=0; result["alerts"]["unseen"]=0;
    auto radios=result["radios"].to<JsonArray>();
    for(unsigned number=1;number<=2;++number) {
      auto radio=radios.add<JsonObject>(); radio["radio"]=number; radio["name"]=number==1?"IC-7300":"K5";
      radio["connected"]=true; radio["frequency_hz"]=number==1?uint64_t(7110000):uint64_t(10000000000ULL);
      radio["mode"]=number==1?"USB":"FM";
      if(number==1) radio["ready"]=true; else radio["ready"]=nullptr;
      radio["readiness_verified"]=number==1;
      auto modem=radio["modem"].to<JsonObject>(); modem["selected"]=number==1?"VARA":"SC-FTN";
      if(number==1) modem["ready"]=true; else modem["ready"]=nullptr;
      modem["readiness_verified"]=number==1; modem["lifecycle"]=number==1?"persistent":"per_transfer";
      auto control=radio["control"].to<JsonObject>(); control["active"]=number==1;
      auto cal=radio["calibration"].to<JsonObject>(); cal["state"]="idle"; cal["progress"]=nullptr; cal["total"]=nullptr;
      auto tx=radio["transfers"].to<JsonArray>().add<JsonObject>(); tx["msg_id"]=number;
      tx["state"]="active"; tx["direction"]=number==1?"receive":"send"; tx["source"]="OK1ABC"; tx["destination"]="OK7PS";
      tx["transport"]=number==1?"VARA":"SC-FTN";
      if(number==1) {tx["percent"]=64;tx["total_bytes"]=19200;tx["progress_bytes"]=12288;tx["total_bytes_exact"]=true;}
      else {tx["percent"]=nullptr;tx["total_bytes"]=nullptr;tx["progress_bytes"]=nullptr;}
    }
  } else if(op=="alert.types") {
    auto type=result["items"].to<JsonArray>().add<JsonObject>(); type["code"]=4; type["key"]="qsy"; type["label"]="QSY"; type["priority"]=4;
  } else if(op=="events.list") {
    result["stream"]="55555555-5555-4555-8555-555555555555"; result["items"].to<JsonArray>();
    result["cursor"]=0; result["latest"]=0; result["truncated"]=false; result["has_more"]=false;
  } else if(op=="events.ack") {
    result["stream"]=request["stream"]; result["cursor"]=request["cursor"];
  } else if(op=="alerts.list" && history) {
    result["revision"]="alerts-empty"; result["items"].to<JsonArray>(); result["total"]=0; result["next_offset"]=nullptr;
  } else return false;
  return true;
}
void bootstrap(ui::screens::GuardianAppScreen& screen) {
  activeScreen=&screen; refresh(screen,testNow);
  for(unsigned attempts=0;attempts<20 && guardian::v3::client().busy();++attempts) {
    auto req=rawRequest(); JsonDocument response;
    check(backgroundReply(req,response,true),"Unexpected Guardian v3 bootstrap operation"); reply(response,screen);
  }
  check(!guardian::v3::client().busy(),"Guardian v3 bootstrap did not complete");
}
JsonDocument request() {
  for(unsigned attempts=0;attempts<20;++attempts) {
    auto req=rawRequest(); JsonDocument response;
    if(guardian::v3::client().busy() && activeScreen && backgroundReply(req,response)) {reply(response,*activeScreen);continue;}
    return req;
  }
  throw std::runtime_error("Guardian background poll starved app request");
}
lv_obj_t* containing(lv_obj_t* root,const std::string& text) {
  if(lv_obj_check_type(root,&lv_label_class) && strstr(lv_label_get_text(root),text.c_str())) return root;
  for(unsigned i=0;i<lv_obj_get_child_cnt(root);++i) if(auto* item=containing(lv_obj_get_child(root,i),text)) return item;
  return nullptr;
}
lv_obj_t* firstType(lv_obj_t* root,const lv_obj_class_t* type) {
  if(lv_obj_check_type(root,type)) return root;
  for(unsigned i=0;i<lv_obj_get_child_cnt(root);++i) if(auto* item=firstType(lv_obj_get_child(root,i),type)) return item;
  return nullptr;
}
void clickContaining(lv_obj_t* root,const std::string& text) {
  lv_obj_update_layout(root); auto* item=containing(root,text); check(item!=nullptr,text.c_str());
  lv_event_send(lv_obj_get_parent(item),LV_EVENT_CLICKED,nullptr);
}
void v3UiRegression(ui::screens::GuardianAppScreen& screen,lv_obj_t* parent,uint8_t* heartbeat,void (*capture)(const char*)) {
  auto& client=guardian::v3::client();
  auto paint=[&]() {
    delay(260); testNow=millis(); ++heartbeat[16];
    check(guardian::receive(heartbeat,20,testNow)==guardian::Result::Ok,"V3 test heartbeat rejected");
    refresh(screen,testNow); lv_obj_update_layout(parent);
  };
  auto idle=[&]() { paint(); bootstrap(screen); lv_obj_update_layout(parent); };
  auto shot=[&](const char* file) { if(capture) capture(file); };
  auto commandReply=[&](JsonDocument& req,const char* state) {
    JsonDocument response; response["id"]=req["id"]; response["ok"]=true;
    auto result=response["result"].to<JsonObject>(); result["command_id"]="66666666-6666-4666-8666-666666666666";
    result["radio"]=req["radio"]; result["action"]=req["action"]; result["accepted"]=true; result["duplicate"]=false;
    result["state"]=state; result["worker"]="inline"; result["error"]=nullptr;
    reply(response,screen); bootstrap(screen); idle();
  };
  // Both radios retain independent readiness and the full 64-bit frequency.
  click(parent,TR("Station")); idle();
  check(containing(parent,"10000.000 MHz"),"Radio 2 frequency narrowed to 32 bits");
  check(containing(parent,TR("Unverified")),"Unknown readiness presented as ready");
  shot("guardian-v3-station.png");
  clickContaining(parent,std::string(TR("Radio 2"))+" · K5"); idle();
  check(containing(parent,"10000000000 Hz") && containing(parent,"per_transfer"),"Radio detail missing frequency or modem lifecycle");
  shot("guardian-v3-radio.png");
  auto* detailScroll=lv_obj_get_child(parent,0); lv_obj_scroll_to_y(detailScroll,80,LV_ANIM_OFF);
  const auto position=lv_obj_get_scroll_y(detailScroll); check(position>0,"Radio detail is not scrollable");
  ++client.revision; paint();
  check(lv_obj_get_scroll_y(lv_obj_get_child(parent,0))==position,"Station update reset the actual detail scroll");
  screen.back();
  click(parent,TR("Active transfers")); idle();
  check(containing(parent,"12288 / 19200 B") && containing(parent,"- / - B"),"Known and unknown transfer lengths mixed");
  check(find(parent,TR("Progress is not delivery confirmation")),"Transfer page claims delivery");
  shot("guardian-v3-transfers.png"); screen.back();
  clickContaining(parent,std::string(TR("Radio 2"))+" · K5"); click(parent,TR("Station control")); idle();
  shot("guardian-v3-controls.png");
  click(parent,TR("Tune radio")); idle();
  auto* frequency=firstType(parent,&lv_textarea_class); check(frequency,"Tune field missing");
  check(!strcmp(lv_textarea_get_text(frequency),"10000000000"),"Entering Tune did not seed the selected radio frequency");
  click(parent,TR("Radio 1")); frequency=firstType(parent,&lv_textarea_class);
  check(frequency && !strcmp(lv_textarea_get_text(frequency),"7110000"),"Changing Tune radio did not load its frequency");
  click(parent,TR("Radio 2")); frequency=firstType(parent,&lv_textarea_class);
  check(frequency && !strcmp(lv_textarea_get_text(frequency),"10000000000"),"Changing Tune radio back did not restore its frequency");
  lv_textarea_set_text(frequency,"145500000"); click(parent,TR("Radio 2"));
  check(firstType(parent,&lv_textarea_class)==frequency && !strcmp(lv_textarea_get_text(frequency),"145500000"),
    "Selecting the current radio rebuilt Tune or reset its input");
  shot("guardian-v3-tune.png"); click(parent,TR("Tune radio"));
  check(find(parent,TR("Confirm command")) && containing(parent,"145500000") && containing(parent,TR("Radio 2")),"Tune confirmation does not identify radio and exact frequency");
  shot("guardian-v3-confirm.png"); click(parent,TR("Cancel"));
  frequency=firstType(parent,&lv_textarea_class); check(frequency && !strcmp(lv_textarea_get_text(frequency),"145500000"),"Cancelling confirmation erased tuning input");
  click(parent,TR("Tune radio")); click(parent,TR("Confirm")); paint(); auto req=request();
  check(!strcmp(req["op"],"control.execute") && req["radio"].as<unsigned>()==2 &&
    !strcmp(req["action"],"radio.tune") && req["frequency_hz"].as<uint64_t>()==145500000ULL && !strcmp(req["mode"],"FM"),"Tune executed with wrong radio or fields");
  check(client.intent.unresolved && client.intent.token==req["token"].as<const char*>(),"Control intent not stored before execute");
  commandReply(req,"completed"); check(find(parent,TR("Completed")),"Completed control outcome missing");
  click(parent,TR("Back to control")); click(parent,TR("Radio 1")); idle();
  // Stop always has its own confirmation and cancellation sends nothing.
  click(parent,TR("Stop control")); check(find(parent,TR("Confirm command")) && containing(parent,TR("Also stops payload transfers and calibration")),"Stop control lacks payload warning");
  shot("guardian-v3-stop-confirm.png");
  click(parent,TR("Cancel")); check(!client.intent.unresolved,"Cancelled stop created a command intent");
  click(parent,TR("Queue beacon")); paint(); req=request();
  check(!strcmp(req["action"],"beacon.send") && req["radio"].as<unsigned>()==1,"Beacon targeted wrong radio");
  commandReply(req,"queued");
  check(find(parent,TR("Queued does not confirm transmission")),"Queued beacon described as transmitted");
  shot("guardian-v3-command-result.png"); click(parent,TR("Back to control"));
  click(parent,TR("Apply saved profile")); idle();
  check(find(parent,"HF VARA") && find(parent,TR("Profiles are created on PC")),"Saved PC profiles missing");
  shot("guardian-v3-profiles.png"); click(parent,TR("Apply saved profile"));
  check(containing(parent,"HF VARA") && find(parent,TR("Confirm command")),"Profile apply skipped exact confirmation");
  click(parent,TR("Cancel")); screen.back();
  // Availability affects the actual button and explains why it is disabled.
  auto* availability=const_cast<guardian::v3::ActionAvailability*>(client.action(1,"beacon.send"));
  check(availability,"Beacon capability missing"); auto oldAction=*availability;
  availability->available=false; availability->reason="Radio occupied"; ++client.revision; paint();
  auto* beacon=find(parent,TR("Queue beacon"));
  check(beacon && lv_obj_has_state(lv_obj_get_parent(beacon),LV_STATE_DISABLED) && containing(parent,"Radio occupied"),"Unavailable action is still executable or hides its reason");
  *availability=oldAction; ++client.revision; bootstrap(screen); idle();
  click(parent,TR("Start AutoTune")); idle();
  auto* peer=firstType(parent,&lv_textarea_class); check(peer,"AutoTune peer field missing"); lv_textarea_set_text(peer,"OK1ABC");
  click(parent,TR("Radio 1"));
  check(firstType(parent,&lv_textarea_class)==peer && !strcmp(lv_textarea_get_text(peer),"OK1ABC"),
    "Selecting the current radio rebuilt AutoTune or reset its input");
  auto& calibration=client.station.radios[0].calibration;
  calibration.state="running"; calibration.current="Bandwidth"; calibration.message="Testing mode";
  calibration.progress={true,3.0}; calibration.total={true,8}; ++client.revision; paint();
  check(!strcmp(lv_textarea_get_text(peer),"OK1ABC") && containing(parent,"3 / 8"),"AutoTune update erased typing or used fixed total");
  auto* bar=firstType(parent,&lv_bar_class); check(bar && lv_bar_get_value(bar)==37,"AutoTune progress is not dynamic");
  shot("guardian-v3-autotune.png");
  // A live gate may change between the last button refresh and its click.
  client.refreshStation(); check(!client.stationFresh(millis()),"Stale Cancel fixture retained fresh station state");
  click(parent,TR("Cancel AutoTune")); peer=firstType(parent,&lv_textarea_class);
  check(peer && !strcmp(lv_textarea_get_text(peer),"OK1ABC") && !client.intent.unresolved,
    "Rejected stale AutoTune cancellation erased its input or created an intent");
  idle(); screen.back(); screen.back(); screen.back();
  click(parent,TR("Alerts")); paint(); req=request();
  check(!strcmp(req["op"],"alerts.list") && req["offset"].as<uint64_t>()==0,"Alert history first page request missing");
  auto alertsReply=[&](JsonDocument& request,bool next) {
    JsonDocument response; response["id"]=request["id"]; response["ok"]=true;
    auto result=response["result"].to<JsonObject>(); result["revision"]="alert-revision"; result["total"]=6;
    if(next) result["next_offset"]=5; else result["next_offset"]=nullptr;
    auto item=result["items"].to<JsonArray>().add<JsonObject>(); item["alert_id"]="77777777-7777-4777-8777-777777777777";
    item["code"]=4; item["note"]="Please change frequency"; item["source"]="OK1ABC"; item["created"]=1791702000;
    item["seen"]=false; item["forwarded"]=false; item["radio"]=2; item["rf_id"]=4294967297ULL;
    item["origin"]["network"]="RF"; item["origin"]["device"]="K5"; item["origin"]["local"]=false;
    reply(response,screen); idle();
  };
  alertsReply(req,true); shot("guardian-v3-alerts.png");
  clickContaining(parent," · OK1ABC");
  check(containing(parent,"4294967297") && find(parent,TR("Reading does not confirm assistance")),"Alert detail missing source identity or assistance distinction");
  shot("guardian-v3-alert-detail.png"); click(parent,TR("Mark as read"));
  check(client.history.items[0].seen && find(parent,TR("Read")),"Alert read acknowledgement not saved locally");
  paint(); req=request(); check(!strcmp(req["op"],"alert.seen"),"Read acknowledgement not sent");
  JsonDocument response; response["id"]=req["id"]; response["ok"]=true;
  response["result"]["alert_id"]=req["alert_id"]; response["result"]["seen"]=true; reply(response,screen); idle(); screen.back();
  click(parent,LV_SYMBOL_RIGHT); paint(); req=request();
  check(req["offset"].as<uint64_t>()==5 && !strcmp(req["revision"],"alert-revision"),"Alert pagination lost snapshot revision");
  alertsReply(req,false); click(parent,LV_SYMBOL_LEFT); paint(); req=request();
  check(req["offset"].as<uint64_t>()==0 && req["revision"].isNull(),"Alert previous page offset incorrect");
  alertsReply(req,true);
  click(parent,LV_SYMBOL_RIGHT); paint(); req=request();
  response.clear(); response["id"]=req["id"]; response["ok"]=false; response["error"]["code"]="list_changed";
  reply(response,screen); delay(2100); paint(); req=request();
  check(req["offset"].as<uint64_t>()==0 && req["revision"].isNull(),"Changed alert history did not restart");
  alertsReply(req,true);
  check(find(parent,"1 / 6"),"Changed alert history left stale pagination in UI"); screen.back();
  // An uncertain execution stays unresolved; only an explicit confirmation
  // after refreshing station state may resend the same durable intent.
  click(parent,TR("Station")); clickContaining(parent,std::string(TR("Radio 1"))+" · IC-7300");
  click(parent,TR("Station control")); idle(); click(parent,TR("Queue beacon")); paint(); req=request();
  const std::string retryToken=req["token"].as<const char*>();
  response.clear(); response["id"]=req["id"]; response["ok"]=false; response["error"]["code"]="uncertain_result";
  reply(response,screen); paint();
  check(client.intent.unresolved && client.intent.command_id.empty() && !client.busy() && find(parent,TR("Result unknown")),"Unknown result silently retried or cleared the intent");
  shot("guardian-v3-command-unknown.png");
  click(parent,TR("Retry same command"));
  auto* confirm=find(parent,TR("Confirm"));
  check(confirm && lv_obj_has_state(lv_obj_get_parent(confirm),LV_STATE_DISABLED),"Unknown retry accepted stale station state");
  delay(2100); idle(); click(parent,TR("Confirm")); paint(); req=request();
  check(!strcmp(req["op"],"control.execute") && retryToken==req["token"].as<const char*>() &&
    req["radio"].as<unsigned>()==1 && !strcmp(req["action"],"beacon.send"),"Confirmed retry changed token or action");
  commandReply(req,"queued"); click(parent,TR("Back to control")); screen.back(); screen.back(); screen.back();
  puts("Guardian v3 UI: two radios, u64/nullable state, transfers, gated controls, exact confirmations, queued beacon, profiles, AutoTune, alert history/read/paging and confirmed same-token retry PASS");
}
}
void runGuardianAppRegression(void (*capture)(const char*)) {
  testNow=millis();
  guardian::disconnected();
  std::string abandoned,error;
  while(guardian::rpcTake(abandoned,error)) {}
  v3Slots[0].clear(); v3Slots[1].clear();
  guardian::v3::client()=guardian::v3::Client{};
  guardian::v3::client().configure({readV3Slot,writeV3Slot});
  homes=0;
  const auto roots=lv_obj_get_child_cnt(lv_layer_top());
  auto* parent=lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(parent);
  lv_obj_set_size(parent,320,218); lv_obj_set_pos(parent,0,22);
  lv_obj_set_style_pad_all(parent,6,LV_PART_MAIN);
  lv_obj_set_style_bg_color(parent,lv_color_hex(ui::theme::colors().COLOR_BG),LV_PART_MAIN);
  lv_obj_set_style_bg_opa(parent,LV_OPA_COVER,LV_PART_MAIN);
  guardian::configure(true,true,true); guardian::connected(); guardian::rpcConnect(); guardian::rpcSubscribe(true);
  guardian::saveDraft(guardian::Draft{});
  guardian::saveAppearance(guardian::Appearance::Blue);
  guardian::saveMessageAlert(false);
  uint8_t state[]={'G','M',1,0x3f,5,0,0,0,2,0,0,0,1,0,0,0,7,0,0,0};
  uint8_t progress[]={'G','P',2,32,64,7,0,0,0};
  guardian::receive(state,sizeof state,millis()); guardian::progress(progress,sizeof progress);
  ui::screens::GuardianAppScreen app; app.create(parent,nullptr,nullptr,[]{ ++homes; },guardSimGuardianAppTitle);
  bootstrap(app);
  v3UiRegression(app,parent,state,capture);
  progress[5]=state[16]; guardian::progress(progress,sizeof progress); refresh(app,millis()+300);
  if (capture) capture("guardian-app-dashboard.png");
  check(!find(parent,TR("Pair PC (2 min)")) && !find(parent,TR("Refresh")),"App still contains pairing/refresh");
  check(find(parent,"64 %") && !find(parent,"32 %"),"Dashboard must show one dominant transfer");
  click(parent,LV_SYMBOL_SETTINGS);
  click(parent,TR("Two beeps: off"));
  lv_obj_update_layout(lv_layer_top());
  check(guardian::loadMessageAlert() && find(parent,TR("Two beeps: on")),"Guardian alert setting did not enable");
  click(parent,TR("Two beeps: on"));
  lv_obj_update_layout(lv_layer_top());
  check(!guardian::loadMessageAlert() && find(parent,TR("Two beeps: off")),"Guardian alert setting did not disable");
  const auto globalAccent=ui::theme::colors().COLOR_ACCENT;
  click(parent,TR("Green"));
  check(guardian::loadAppearance()==guardian::Appearance::Green && globalAccent==ui::theme::colors().COLOR_ACCENT,"App theme changed global chrome or failed persistence");
  if (capture) capture("guardian-app-settings-green.png");
  app.back();
  if (capture) capture("guardian-app-dashboard-green.png");
  click(parent,LV_SYMBOL_HOME); check(homes==1,"Guardian Home callback missing");
  click(parent,LV_SYMBOL_SETTINGS); click(parent,TR("Blue")); app.back();
  click(parent,TR("Messages")); auto req=request();
  check(!strcmp(req["op"],"messages.list") && req["offset"].as<unsigned>()==0,"Wrong message list request");
  JsonDocument response; response["id"]=req["id"]; response["ok"]=true;
  auto result=response["result"].to<JsonObject>(); result["revision"]="page-a"; result["next_offset"]=4; result["total"]=5;
  auto row=result["items"].to<JsonArray>().add<JsonObject>();
  row["msg_id"]=uint32_t(0xf1234567); row["source"]="OK1ABC"; row["final_dest"]="OK7PS";
  row["subject"]="Zkouška spojení"; row["status"]="received"; row["read"]=false;
  auto second=result["items"].as<JsonArray>().add<JsonObject>();
  second["msg_id"]=2; second["source"]="OK2DEF"; second["subject"]="Potvrzení příjmu"; second["read"]=false;
  auto third=result["items"].as<JsonArray>().add<JsonObject>();
  third["msg_id"]=3; third["source"]="OK3XYZ"; third["subject"]="Večerní provoz"; third["read"]=true;
  reply(response,app);
  if (capture) capture("guardian-app-messages.png");
  click(parent,LV_SYMBOL_RIGHT); req=request();
  check(req["offset"].as<unsigned>()==4 && !strcmp(req["revision"],"page-a"),"Lost list revision/offset");
  response.clear(); response["id"]=req["id"]; response["ok"]=false; response["error"]["code"]="list_changed";
  reply(response,app); req=request();
  check(req["offset"].as<unsigned>()==0 && req["revision"].isNull(),"Changed list not restarted");
  response.clear(); response["id"]=req["id"]; response["ok"]=true;
  result=response["result"].to<JsonObject>(); result["revision"]="page-b"; result["next_offset"]=nullptr; result["total"]=1;
  row=result["items"].to<JsonArray>().add<JsonObject>(); row["msg_id"]=uint32_t(0xf1234567);
  row["source"]="OK1ABC"; row["final_dest"]="OK7PS"; row["subject"]="Test"; row["status"]="received"; row["read"]=false;
  reply(response,app);
  std::string caption="OK1ABC";
  click(parent,caption.c_str()); req=request();
  check(!strcmp(req["op"],"message.get") && req["msg_id"].as<uint32_t>()==0xf1234567,"Message ID truncated");
  response.clear(); response["id"]=req["id"]; response["ok"]=true;
  result=response["result"].to<JsonObject>(); result["msg_id"]=uint32_t(0xf1234567); result["revision"]="text-a";
  result["offset"]=0; result["next_offset"]=nullptr; result["body"]="Příjem 😀"; result["subject"]="Test";
  reply(response,app); check(find(parent," > \n\nTest\n\nPříjem 😀"),"Unicode message body missing");
  if (capture) capture("guardian-app-text.png");
  app.back(); req=request();
  response["id"]=req["id"]; response["result"]["revision"]="back-list"; response["result"]["items"].to<JsonArray>(); reply(response,app);
  app.back(); click(parent,TR("Network")); req=request();
  check(!strcmp(req["op"],"contacts.list") && !strcmp(req["source"],"saved"),"Wrong contacts request");
  response.clear(); response["id"]=req["id"]; response["ok"]=true;
  result=response["result"].to<JsonObject>(); result["revision"]="contacts-a"; result["next_offset"]=nullptr; result["total"]=2;
  row=result["items"].to<JsonArray>().add<JsonObject>(); row["callsign"]="OK2DEF"; row["live"]=true; row["saved"]=true; row["grid"]="JN99"; row["next_hop"]="OK9XYZ"; row["live_next_hop"]="OK2DEF";
  second=result["items"].as<JsonArray>().add<JsonObject>(); second["callsign"]="OK3XYZ";
  second["live"]=false; second["saved"]=true; second["next_hop"]="OK3XYZ";
  reply(response,app);
  if (capture) capture("guardian-app-network.png");
  if(!find(parent,(std::string(TR("Via"))+" OK9XYZ").c_str())) { dump(parent); check(false,"Saved next hop was replaced by live route"); }
  const std::string contact="OK2DEF";
  click(parent,contact.c_str());
  unsigned field=0;
  for (unsigned i=0;i<lv_obj_get_child_cnt(parent);++i) {
    auto* obj=lv_obj_get_child(parent,i); if (!lv_obj_check_type(obj,&lv_textarea_class)) continue;
    if (field==0) check(!strcmp(lv_textarea_get_text(obj),"OK2DEF"),"Contact did not prefill recipient");
    lv_textarea_set_text(obj,field==0?"OK1ABC":field==1?"Zkouška":"Ahoj 😀"); ++field;
  }
  check(field==3,"Composer fields missing");
  if (capture) capture("guardian-app-compose.png");
  click(parent,TR("Send")); req=request();
  check(!strcmp(req["op"],"message.queue"),"Composer not queued");
  const std::string token=req["token"].as<const char*>(); guardian::Draft saved;
  check(guardian::loadDraft(saved) && saved.token==token && saved.body=="Ahoj 😀","Draft/token not durable before send");
  guardian::disconnected(); refresh(app,millis()); app.detach(); lv_obj_clean(parent);
  guardian::connected(); guardian::rpcConnect(); guardian::rpcSubscribe(true);
  ui::screens::GuardianAppScreen reopened; reopened.create(parent,nullptr,nullptr,nullptr,guardSimGuardianAppTitle);
  bootstrap(reopened);
  click(parent,LV_SYMBOL_SETTINGS);
  lv_obj_update_layout(lv_layer_top());
  check(find(parent,TR("Two beeps: off")),"Guardian alert setting did not survive reopening");
  reopened.back();
  click(parent,TR("Write")); click(parent,TR("Retry same send")); req=request();
  check(token==req["token"].as<const char*>() && !strcmp(req["body"],"Ahoj 😀"),"Retry changed token or content");
  response.clear(); response["id"]=req["id"]; response["ok"]=true;
  result=response["result"].to<JsonObject>(); result["accepted"]=true; result["duplicate"]=true; result["msg_id"]=42;
  reply(response,reopened); check(guardian::loadDraft(saved) && saved.token.empty(),"Accepted send not cleared");
  // The dashboard shows a single ellipsized line; tapping opens its full text.
  lv_event_send(lv_obj_get_child(parent,-1),LV_EVENT_CLICKED,nullptr);
  check(find(parent,TR("Queued on PC. Delivery is handled by Guardian.")),"Queue acceptance not shown");
  reopened.back();
  // Automatic polling is silent and must not replace the fields when its
  // response arrives after the user has already opened the composer.
  guardian::connected(); guardian::receive(state,sizeof state,millis());
  click(parent,TR("Messages")); req=request();
  response.clear(); response["id"]=req["id"]; response["ok"]=true;
  result=response["result"].to<JsonObject>(); result["revision"]="auto-a"; result["next_offset"]=nullptr; result["total"]=0; result["items"].to<JsonArray>();
  reply(response,reopened);
  int deleted=0;
  lv_obj_add_event_cb(lv_obj_get_child(parent,0),[](lv_event_t* e){
    ++*static_cast<int*>(lv_event_get_user_data(e));
  },LV_EVENT_DELETE,&deleted);
  refresh(reopened,testNow+5100); req=request();
  response["id"]=req["id"]; response["result"]["revision"]="auto-same-content";
  reply(response,reopened);
  check(deleted==0,"Unchanged automatic reply rebuilt the list");
  refresh(reopened,testNow+5100); req=request();
  check(!strcmp(req["op"],"messages.list"),"Visible list did not refresh automatically");
  click(parent,LV_SYMBOL_EDIT);
  lv_obj_t* bodyField=nullptr;
  for (unsigned i=0;i<lv_obj_get_child_cnt(parent);++i) {
    auto* obj=lv_obj_get_child(parent,i); if (lv_obj_check_type(obj,&lv_textarea_class)) bodyField=obj;
  }
  check(bodyField!=nullptr,"Composer missing during background RPC");
  lv_textarea_set_text(bodyField,"Keep my typing");
  response["id"]=req["id"]; response["result"]["revision"]="auto-b";
  reply(response,reopened);
  check(!strcmp(lv_textarea_get_text(bodyField),"Keep my typing"),"Late background reply rebuilt composer");
  reopened.detach(); lv_obj_del(parent); guardian::disconnected(); guardian::v3::client().tick(testNow,false); guardian::configure(false,false,false); activeScreen=nullptr;
  guardSimGuardianAppTitle(nullptr);
  check(lv_obj_get_child_cnt(lv_layer_top())==roots,"Guardian leaked UI roots");
  puts("Guardian app: A3 actions/themes, saved routes, automatic refresh/typing, paging, Unicode, durable send/retry PASS");
}

void runGuardianPolishRegression(void (*capture)(const char*)) {
  testNow=millis();
  const auto roots=lv_obj_get_child_cnt(lv_layer_top());
  auto* parent=lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(parent);
  lv_obj_set_size(parent,320,218); lv_obj_set_pos(parent,0,22); lv_obj_set_style_pad_all(parent,6,0);
  guardian::configure(true,true,true); guardian::connected(); guardian::rpcConnect(); guardian::rpcSubscribe(true);
  uint8_t state[]={'G','M',1,1,5,0,0,0,2,0,0,0,1,0,0,0,0,0,0,0};
  auto heartbeat=[&]() { testNow=std::max<uint32_t>(testNow,millis()); ++state[16]; check(guardian::receive(state,sizeof state,testNow)==guardian::Result::Ok,"Heartbeat rejected"); };
  heartbeat();
  ui::screens::GuardianAppScreen app; app.create(parent,nullptr,nullptr,nullptr,guardSimGuardianAppTitle);
  bootstrap(app);
  auto visible=[&](const char* text) { lv_obj_update_layout(parent); return find(parent,text); };
  auto shot=[&](const char* name) { lv_obj_update_layout(parent); if(capture) capture(name); };
  auto listReply=[&](JsonDocument& req,const char* title) {
    JsonDocument doc; doc["id"]=req["id"]; doc["ok"]=true;
    auto result=doc["result"].to<JsonObject>(); result["revision"]="rev-1"; result["total"]=1; result["next_offset"]=nullptr;
    auto row=result["items"].to<JsonArray>().add<JsonObject>();
    row["msg_id"]=101; row["source"]=title; row["final_dest"]=title; row["subject"]="Cached message"; row["read"]=true;
    row["callsign"]=title; row["saved"]=true; row["live"]=false; row["next_hop"]=title;
    reply(doc,app);
  };
  auto statusReply=[&](JsonDocument& req) {
    JsonDocument doc; doc["id"]=req["id"]; doc["ok"]=true;
    auto result=doc["result"].to<JsonObject>(); result["inbox"]=6; result["outbox"]=2; result["unread"]=1;
    for (auto key:{"tx","rx","radio_connected","vara_connected","control_active"}) result[key]=false;
    reply(doc,app);
  };
  // Appearance is independent of the global day/night preference.
  for (auto name:{"Blue","Green"}) {
    click(parent,LV_SYMBOL_SETTINGS); click(parent,TR(name)); app.back();
    auto* write=visible(TR("Write")); check(write,"Write action missing");
    auto* button=lv_obj_get_parent(write);
    check(lv_color_to32(lv_obj_get_style_bg_color(button,0))==lv_color_to32(lv_color_hex(0xf29d38)),"Primary action not orange");
    check(lv_color_to32(lv_obj_get_style_text_color(write,0))==lv_color_to32(lv_color_hex(0x1b1b1b)),"Primary action text not dark");
    for (unsigned i=1;i<lv_obj_get_child_cnt(button);++i) {
      auto* icon=lv_obj_get_child(button,i);
      check(lv_color_to32(lv_obj_get_style_line_color(icon,0))==lv_color_to32(lv_color_hex(0x1b1b1b)),"Orange button icon outline not dark");
      check(lv_color_to32(lv_obj_get_style_text_color(icon,0))==lv_color_to32(lv_color_hex(0x1b1b1b)),"Orange button icon fill not dark");
    }
    auto* panel=lv_obj_get_child(parent,0);
    check(lv_obj_get_style_bg_opa(panel,0)<=LV_OPA_70,"Panel still opaque");
    shot(!strcmp(name,"Blue")?"guardian-muted-blue.png":"guardian-muted-green.png");
  }
  // Count blocks, including the icon area, are actionable.
  puts("Guardian polish: count targets");
  auto* panel=lv_obj_get_child(parent,0);
  lv_obj_t* countTargets[2]{}; unsigned n=0;
  for (unsigned i=0;i<lv_obj_get_child_cnt(panel);++i) {
    auto* child=lv_obj_get_child(panel,i);
    if (lv_obj_check_type(child,&lv_btn_class) && n<2) countTargets[n++]=child;
  }
  check(n==2,"Dashboard count hit targets missing");
  puts("Guardian polish: outbox"); lv_event_send(countTargets[1],LV_EVENT_CLICKED,nullptr); auto req=request();
  check(!strcmp(req["folder"],"outbox"),"Outgoing count opened wrong folder"); listReply(req,"OUTBOX");
  app.back(); panel=lv_obj_get_child(parent,0);
  for (unsigned i=0;i<lv_obj_get_child_cnt(panel);++i) if (lv_obj_check_type(lv_obj_get_child(panel,i),&lv_btn_class)) {
    lv_event_send(lv_obj_get_child(panel,i),LV_EVENT_CLICKED,nullptr); break;
  }
  puts("Guardian polish: inbox"); req=request(); check(!strcmp(req["folder"],"inbox"),"Incoming count opened wrong folder"); listReply(req,"INBOX");
  app.back(); click(parent,TR("Messages"));
  puts("Guardian polish: cached messages");
  check(visible("INBOX") && !visible(TR("Loading...")),"Cached messages not shown before RPC");
  req=request(); listReply(req,"INBOX UPDATED");
  check(visible("INBOX UPDATED") && !visible("INBOX"),"Background reply failed to update cache view");
  click(parent,"INBOX UPDATED"); req=request();
  auto textReply=[&]() {
    JsonDocument doc; doc["id"]=req["id"]; doc["ok"]=true;
    auto result=doc["result"].to<JsonObject>(); result["revision"]="body-1"; result["next_offset"]=nullptr;
    result["msg_id"]=101; result["body"]="Cached body"; result["subject"]="Test";
    reply(doc,app);
  };
  textReply(); app.back(); req=request(); listReply(req,"INBOX UPDATED");
  click(parent,"INBOX UPDATED");
  check(visible(" > \n\nTest\n\nCached body"),"Cached message body missing before RPC");
  req=request(); textReply(); app.back(); req=request(); listReply(req,"INBOX UPDATED");
  click(parent,"INBOX UPDATED");
  check(visible(" > \n\nTest\n\nCached body"),"Unchanged background reply corrupted cached message heading");
  req=request(); textReply(); app.back(); req=request(); listReply(req,"INBOX UPDATED");
  app.back(); click(parent,TR("Network")); req=request(); listReply(req,"SAVED");
  puts("Guardian polish: cached contacts");
  auto* live=lv_obj_get_parent(visible(TR("Live routes")));
  auto* saved=lv_obj_get_parent(visible(TR("Saved routes")));
  lv_area_t a,b; lv_obj_get_coords(live,&a); lv_obj_get_coords(saved,&b);
  check(b.x1-a.x2>=8,"Network buttons have no gap"); shot("guardian-network-gap.png");
  click(parent,TR("Live routes")); req=request(); listReply(req,"LIVE");
  click(parent,TR("Saved routes"));
  check(visible("SAVED") && !visible("LIVE"),"Saved/live contact caches mixed");
  req=request(); listReply(req,"SAVED");
  guardian::disconnected(); refresh(app,testNow+300);
  puts("Guardian polish: disconnected");
  check(visible("SAVED") && visible(TR("Offline")),"Link loss erased cached contacts");
  shot("guardian-cached-offline.png");
  // Reconnection requires CCCD and a fresh PC heartbeat; UI then retries without a tap.
  guardian::connected(); guardian::rpcConnect(); guardian::rpcSubscribe(true); heartbeat();
  refresh(app,testNow+300); bootstrap(app); req=request(); check(!strcmp(req["op"],"contacts.list"),"Reconnect did not refresh contacts");
  listReply(req,"RECONNECTED"); check(visible("RECONNECTED"),"Reconnected data missing");
  // Hold boundaries, one request per hold, status first and current page second.
  puts("Guardian polish: manual hold");
  const uint32_t start=millis(); app.refreshKey(true,start); app.refreshKey(true,start+1999);
  check(!visible(TR("Cancel")) && !guardian::rpcBusy(),"R fired before two seconds");
  app.refreshKey(true,start+2000); check(visible(TR("Cancel")),"Refresh modal missing");
  shot("guardian-refresh-modal.png"); refresh(app,testNow+300); req=request();
  check(!strcmp(req["op"],"status.get"),"Manual refresh did not query current status"); statusReply(req);
  refresh(app,testNow+300); req=request(); check(!strcmp(req["op"],"contacts.list"),"Manual refresh skipped visible list");
  listReply(req,"MANUALLY UPDATED"); lv_timer_handler();
  app.refreshKey(true,start+4000); check(!guardian::rpcBusy(),"Held R repeated refresh");
  app.refreshKey(false,start+4100);
  // Cancelling a modal must allow its in-flight RPC to finish safely, without
  // starting the second refresh request or blocking navigation afterwards.
  app.manualRefresh(); refresh(app,testNow+400); req=request();
  click(parent,TR("Cancel")); statusReply(req); lv_timer_handler();
  check(!visible(TR("Cancel")) && !guardian::rpcBusy(),"Cancelled refresh restarted or leaked");
  app.back(); guardian::disconnected(); refresh(app,testNow+300);
  check(visible("-"),"Offline counts not replaced with ASCII placeholder");
  lv_font_glyph_dsc_t glyph{};
  check(lv_font_get_glyph_dsc(&lv_font_montserrat_28,&glyph,'-',0) && !glyph.is_placeholder,"Offline placeholder missing from large font");
  shot("guardian-offline-counts.png");
  app.manualRefresh(); refresh(app,testNow+1500); lv_timer_handler();
  check(visible(TR("Offline")),"Offline manual refresh did not finish");
  auto* footer=visible(TR("Offline"));
  check(lv_obj_get_height(footer)<=16,"Footer wrapped to a second line");
  click(parent,TR("Write")); app.refreshKey(true,start+5000); app.refreshKey(true,start+8000);
  check(!visible(TR("Cancel")),"R refresh intercepted composer input");
  app.detach(); lv_obj_del(parent); lv_timer_handler(); guardian::disconnected(); guardian::v3::client().tick(testNow,false); guardian::configure(false,false,false); activeScreen=nullptr;
  guardSimGuardianAppTitle(nullptr);
  check(lv_obj_get_child_cnt(lv_layer_top())==roots,"Guardian refresh modal leaked");
  puts("Guardian polish: themes, count actions, cached lists, network gap, reconnect, R hold/modal and offline glyph PASS");
}

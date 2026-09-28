// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/GuardianAppScreen.h"
#include "services/GuardianRpcLink.h"
#include "services/GuardianLink.h"
#include "services/GuardianJson.h"
#include "i18n.h"
#include "theme/Theme.h"
#include "services/GuardianAppearance.h"
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
bool notification(const uint8_t* p,size_t n) { transfer=p[1]; wire.append((const char*)p+4,n-4); return true; }
JsonDocument request() {
  wire.clear();
  for (int i=0;i<2200;++i) { size_t before=wire.size(); guardian::rpcTick(millis(),notification); if (wire.size()==before) break; }
  JsonDocument doc; check(guardian::parseObject(doc,wire),"Invalid RPC request"); return doc;
}
void reply(JsonDocument& doc,ui::screens::GuardianAppScreen& screen) {
  const auto json=guardian::jsonText(doc);
  for (size_t pos=0,index=0;pos<json.size();pos+=16,++index) {
    const size_t n=std::min(size_t(16),json.size()-pos); uint8_t p[20];
    p[0]=(pos==0?1:0)|(pos+n==json.size()?2:0); p[1]=transfer; p[2]=uint8_t(index); p[3]=uint8_t(index>>8);
    memcpy(p+4,json.data()+pos,n); check(guardian::rpcReceive(p,n+4,millis()),"Response framing failed");
  }
  screen.refresh(millis());
  // LVGL temporarily ellipsizes new labels before their parent's first layout.
  // Assert the laid-out screen, also when no screenshot/render tick follows.
  lv_obj_update_layout(lv_layer_top());
}
}
void runGuardianAppRegression(void (*capture)(const char*)) {
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
  const uint8_t state[]={'G','M',1,0x3f,5,0,0,0,2,0,0,0,1,0,0,0,7,0,0,0};
  const uint8_t progress[]={'G','P',2,32,64,7,0,0,0};
  guardian::receive(state,sizeof state,millis()); guardian::progress(progress,sizeof progress);
  ui::screens::GuardianAppScreen app; app.create(parent,nullptr,nullptr,[]{ ++homes; },guardSimGuardianAppTitle);
  if (capture) capture("guardian-app-dashboard.png");
  check(!find(parent,TR("Pair PC (2 min)")) && !find(parent,TR("Refresh")),"App still contains pairing/refresh");
  check(find(parent,"64 %") && !find(parent,"32 %"),"Dashboard must show one dominant transfer");
  click(parent,LV_SYMBOL_SETTINGS);
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
  guardian::disconnected(); app.refresh(millis()); app.detach(); lv_obj_clean(parent);
  guardian::connected(); guardian::rpcConnect(); guardian::rpcSubscribe(true);
  ui::screens::GuardianAppScreen reopened; reopened.create(parent,nullptr,nullptr,nullptr,guardSimGuardianAppTitle);
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
  reopened.refresh(millis()+5100); req=request();
  response["id"]=req["id"]; response["result"]["revision"]="auto-same-content";
  reply(response,reopened);
  check(deleted==0,"Unchanged automatic reply rebuilt the list");
  reopened.refresh(millis()+5100); req=request();
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
  reopened.detach(); lv_obj_del(parent); guardian::disconnected(); guardian::configure(false,false,false);
  guardSimGuardianAppTitle(nullptr);
  check(lv_obj_get_child_cnt(lv_layer_top())==roots,"Guardian leaked UI roots");
  puts("Guardian app: A3 actions/themes, saved routes, automatic refresh/typing, paging, Unicode, durable send/retry PASS");
}

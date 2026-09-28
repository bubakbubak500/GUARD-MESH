// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/GuardianAppScreen.h"
#include "services/GuardianRpcLink.h"
#include "services/GuardianLink.h"
#include "services/GuardianJson.h"
#include "i18n.h"
#include "theme/Theme.h"
#include <Arduino.h>
#include <stdexcept>
#include <cstring>
namespace {
void check(bool ok,const char* why) { if (!ok) throw std::runtime_error(why); }
lv_obj_t* find(lv_obj_t* root,const char* text) {
  if (lv_obj_check_type(root,&lv_label_class) && !strcmp(lv_label_get_text(root),text)) return root;
  for (unsigned i=0;i<lv_obj_get_child_cnt(root);++i) if (auto* p=find(lv_obj_get_child(root,i),text)) return p;
  return nullptr;
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
}
}
void runGuardianAppRegression(void (*capture)(const char*)) {
  const auto roots=lv_obj_get_child_cnt(lv_layer_top());
  auto* parent=lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(parent);
  lv_obj_set_size(parent,320,218); lv_obj_set_pos(parent,0,22);
  lv_obj_set_style_pad_all(parent,6,LV_PART_MAIN);
  lv_obj_set_style_bg_color(parent,lv_color_hex(ui::theme::colors().COLOR_BG),LV_PART_MAIN);
  lv_obj_set_style_bg_opa(parent,LV_OPA_COVER,LV_PART_MAIN);
  guardian::configure(true,true,true); guardian::connected(); guardian::rpcConnect(); guardian::rpcSubscribe(true);
  guardian::saveDraft(guardian::Draft{});
  ui::screens::GuardianAppScreen app; app.create(parent,nullptr,nullptr);
  if (capture) capture("guardian-app-dashboard.png");
  check(!find(parent,TR("Pair PC (2 min)")),"App still contains pairing");
  click(parent,TR("Messages")); auto req=request();
  check(!strcmp(req["op"],"messages.list") && req["offset"].as<unsigned>()==0,"Wrong message list request");
  JsonDocument response; response["id"]=req["id"]; response["ok"]=true;
  auto result=response["result"].to<JsonObject>(); result["revision"]="page-a"; result["next_offset"]=4; result["total"]=5;
  auto row=result["items"].to<JsonArray>().add<JsonObject>();
  row["msg_id"]=uint32_t(0xf1234567); row["source"]="OK1ABC"; row["final_dest"]="OK7PS";
  row["subject"]="Test"; row["status"]="received"; row["read"]=false;
  reply(response,app);
  if (capture) capture("guardian-app-messages.png");
  click(parent,TR("Next")); req=request();
  check(req["offset"].as<unsigned>()==4 && !strcmp(req["revision"],"page-a"),"Lost list revision/offset");
  response.clear(); response["id"]=req["id"]; response["ok"]=false; response["error"]["code"]="list_changed";
  reply(response,app); req=request();
  check(req["offset"].as<unsigned>()==0 && req["revision"].isNull(),"Changed list not restarted");
  response.clear(); response["id"]=req["id"]; response["ok"]=true;
  result=response["result"].to<JsonObject>(); result["revision"]="page-b"; result["next_offset"]=nullptr; result["total"]=1;
  row=result["items"].to<JsonArray>().add<JsonObject>(); row["msg_id"]=uint32_t(0xf1234567);
  row["source"]="OK1ABC"; row["final_dest"]="OK7PS"; row["subject"]="Test"; row["status"]="received"; row["read"]=false;
  reply(response,app);
  std::string caption=std::string("* OK1ABC > OK7PS\n")+TR("Received")+" · Test";
  click(parent,caption.c_str()); req=request();
  check(!strcmp(req["op"],"message.get") && req["msg_id"].as<uint32_t>()==0xf1234567,"Message ID truncated");
  response.clear(); response["id"]=req["id"]; response["ok"]=true;
  result=response["result"].to<JsonObject>(); result["msg_id"]=uint32_t(0xf1234567); result["revision"]="text-a";
  result["offset"]=0; result["next_offset"]=nullptr; result["body"]="Příjem 😀"; result["subject"]="Test";
  reply(response,app); check(find(parent,"Test\n\nPříjem 😀"),"Unicode message body missing");
  if (capture) capture("guardian-app-text.png");
  click(parent,LV_SYMBOL_LEFT); click(parent,TR("Network")); req=request();
  check(!strcmp(req["op"],"contacts.list") && !strcmp(req["source"],"all"),"Wrong contacts request");
  response.clear(); response["id"]=req["id"]; response["ok"]=true;
  result=response["result"].to<JsonObject>(); result["revision"]="contacts-a"; result["next_offset"]=nullptr; result["total"]=1;
  row=result["items"].to<JsonArray>().add<JsonObject>(); row["callsign"]="OK2DEF"; row["live"]=true; row["grid"]="JN99";
  reply(response,app);
  if (capture) capture("guardian-app-network.png");
  const std::string contact=std::string("OK2DEF\n")+TR("Live")+" · JN99";
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
  ui::screens::GuardianAppScreen reopened; reopened.create(parent,nullptr,nullptr);
  click(parent,TR("New message")); click(parent,TR("Retry same send")); req=request();
  check(token==req["token"].as<const char*>() && !strcmp(req["body"],"Ahoj 😀"),"Retry changed token or content");
  response.clear(); response["id"]=req["id"]; response["ok"]=true;
  result=response["result"].to<JsonObject>(); result["accepted"]=true; result["duplicate"]=true; result["msg_id"]=42;
  reply(response,reopened); check(guardian::loadDraft(saved) && saved.token.empty(),"Accepted send not cleared");
  check(find(parent,TR("Queued on PC. Delivery is handled by Guardian.")),"Queue acceptance not shown");
  reopened.detach(); lv_obj_del(parent); guardian::disconnected(); guardian::configure(false,false,false);
  check(lv_obj_get_child_cnt(lv_layer_top())==roots,"Guardian leaked UI roots");
  puts("Guardian app: paging revisions, uint32 IDs, Unicode body, persistent token/retry, accepted queue PASS");
}

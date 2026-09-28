// SPDX-License-Identifier: GPL-3.0-or-later
#include "GuardianAppScreen.h"
#include "../services/GuardianLink.h"
#include "../services/GuardianRpcLink.h"
#include "../services/GuardianJson.h"
#include "../theme/Theme.h"
#include "../theme/Fonts.h"
#include "../widgets/Styles.h"
#include "../widgets/GuardianShield.h"
#include "../i18n.h"
#include <cstdio>
#include <cstring>
#include <Arduino.h>
namespace ui { namespace screens {
namespace {
const char* const folders[] = {"inbox","outbox","sent","draft","transit"};
const char* const sources[] = {"all","live","saved"};
const char* errorText(const std::string& code) {
  if (code == "not_found") return TR("Message not found");
  if (code == "station_not_configured") return TR("Set the callsign in Guardian on PC.");
  if (code == "token_conflict") return TR("Send token conflict. Check Guardian on PC.");
  if (code == "storage_error") return TR("Guardian storage unavailable");
  if (code == "invalid_request") return TR("Guardian rejected the request");
  if (code == "unknown_operation") return TR("Update Guardian on PC");
  if (code == "protocol_error") return TR("Invalid Guardian response");
  return TR("Guardian unavailable. Try again after reconnecting.");
}
const char* messageStatus(const char* code) {
  if (!strcmp(code,"queued")) return TR("Queued");
  if (!strcmp(code,"sending")) return TR("Sending");
  if (!strcmp(code,"delivered")) return TR("Delivered");
  if (!strcmp(code,"received")) return TR("Received");
  if (!strcmp(code,"failed")) return TR("Failed");
  if (!strcmp(code,"draft")) return TR("Draft");
  if (!strcmp(code,"waiting")) return TR("Waiting");
  if (!strcmp(code,"forwarded")) return TR("Forwarded");
  return "—";
}
}
lv_obj_t* GuardianAppScreen::label(const char* value, int x, int y, int w, bool small) {
  auto* l = lv_label_create(_root.get()); lv_label_set_text(l, value);
  lv_obj_set_pos(l,x,y); lv_obj_set_width(l,w);
  lv_obj_set_style_text_font(l, small ? &theme::font12() : &theme::font14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(l, lv_color_hex(theme::colors().COLOR_TEXT), LV_PART_MAIN);
  return l;
}
void GuardianAppScreen::button(const char* title, int x, int y, int w, int action, int h) {
  if (_bindingCount >= 20) return;
  auto* b = lv_btn_create(_root.get()); widgets::styleButton(b);
  lv_obj_set_pos(b,x,y); lv_obj_set_size(b,w,h);
  auto* l = lv_label_create(b); lv_label_set_text(l,title);
  lv_obj_set_width(l,w-8); lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(l,&theme::font12(),LV_PART_MAIN);
  lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,LV_PART_MAIN); lv_obj_center(l);
  auto& bind = _bindings[_bindingCount++]; bind.owner=this; bind.action=action;
  lv_obj_add_event_cb(b,clicked,LV_EVENT_CLICKED,&bind);
}
void GuardianAppScreen::create(lv_obj_t* parent, void (*attach)(lv_obj_t*), void (*hideKeyboard)()) {
  _root.set(parent); _attach=attach; _hideKeyboard=hideKeyboard;
  if (!_loaded) { guardian::loadDraft(_draft); _loaded=true; }
  _page=Dashboard; _last=0; render();
}
void GuardianAppScreen::captureDraft() {
  if (_page != Compose || !_fields[0] || !_root.get() || _draft.pending()) return;
  _draft.to=lv_textarea_get_text(_fields[0]); _draft.subject=lv_textarea_get_text(_fields[1]);
  _draft.body=lv_textarea_get_text(_fields[2]); _draft.priority=lv_dropdown_get_selected(_priority);
}
void GuardianAppScreen::detach() {
  if (_root.get()) { captureDraft(); if (_loaded) guardian::saveDraft(_draft); }
  if (_hideKeyboard && _root.get()) _hideKeyboard();
  _root.set(nullptr); _status=_notice=nullptr; for (auto*& f:_fields) f=nullptr;
}
void GuardianAppScreen::render() {
  if (!_root.get()) return;
  if (_hideKeyboard) _hideKeyboard();
  lv_obj_clean(_root.get()); _bindingCount=0; _status=_notice=nullptr;
  for (auto*& f:_fields) f=nullptr;
  lv_obj_update_layout(_root.get()); const int w=lv_obj_get_content_width(_root.get());
  if (_page==Dashboard) {
    auto* shield=widgets::guardianShield(_root.get(),30); lv_obj_set_pos(shield,0,0);
    label("Guardian",36,4,w-96);
    _status=label("",0,38,w-115);
    const char* titles[]={TR("Messages"),TR("Network"),TR("New message"),TR("Refresh")};
    for (int i=0;i<4;++i) button(titles[i],w-106,38+i*33,104,i+1);
    _notice=label(_info.c_str(),0,176,w,true);
    _last=0;
  } else if (_page==Compose) {
    button(LV_SYMBOL_LEFT,0,0,32,0,26);
    label(_draft.pending()?TR("Unconfirmed send"):TR("New message"),38,4,w-96);
    const char* hints[]={TR("Recipient"),TR("Subject"),TR("Message")};
    const std::string* values[]={&_draft.to,&_draft.subject,&_draft.body};
    for (int i=0;i<3;++i) {
      auto* field=_fields[i]=lv_textarea_create(_root.get());
      widgets::styleCard(field);
      lv_obj_set_style_text_color(field,lv_color_hex(theme::colors().COLOR_TEXT),LV_PART_MAIN);
      lv_obj_set_pos(field,0,32+i*33); lv_obj_set_size(field,w,i==2?63:30);
      lv_obj_set_style_text_font(field,&theme::font12(),LV_PART_MAIN);
      lv_textarea_set_one_line(field,i!=2); lv_textarea_set_max_length(field,i==0?16:i==1?256:4096);
      if (i==0) lv_textarea_set_accepted_chars(field,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/-");
      widgets::taSetPlaceholder(field,hints[i]); lv_textarea_set_text(field,values[i]->c_str());
      if (_draft.pending()) lv_obj_add_state(field,LV_STATE_DISABLED);
      else if (_attach) _attach(field);
    }
    _priority=lv_dropdown_create(_root.get()); lv_obj_set_pos(_priority,0,166); lv_obj_set_size(_priority,102,30);
    widgets::styleCard(_priority);
    lv_obj_set_style_text_color(_priority,lv_color_hex(theme::colors().COLOR_TEXT),LV_PART_MAIN);
    lv_obj_set_style_text_font(_priority,&theme::font12(),LV_PART_MAIN);
    lv_dropdown_set_options(_priority,TR("Normal\nPriority 1\nPriority 2\nPriority 3"));
    lv_dropdown_set_selected(_priority,_draft.priority);
    if (_draft.pending()) lv_obj_add_state(_priority,LV_STATE_DISABLED);
    button(_draft.pending()?TR("Retry same send"):TR("Send"),108,166,w-108,5);
    _notice=label(_info.c_str(),0,200,w,true);
    if (_draft.pending()) button(_confirmNew?TR("PC checked: start new"):TR("Start new message"),0,270,w,8);
  } else {
    button(LV_SYMBOL_LEFT,0,0,32,0,26);
    if (_page==Text) label(_heading.c_str(),38,2,w-98,true);
    else {
      auto* choice=lv_dropdown_create(_root.get()); lv_obj_set_pos(choice,38,0); lv_obj_set_size(choice,w-94,28);
      widgets::styleCard(choice);
      lv_obj_set_style_text_color(choice,lv_color_hex(theme::colors().COLOR_TEXT),LV_PART_MAIN);
      lv_obj_set_style_text_font(choice,&theme::font12(),LV_PART_MAIN);
      lv_dropdown_set_options(choice,_page==Messages?TR("Inbox\nOutbox\nSent\nDrafts\nTransit"):TR("All contacts\nLive contacts\nSaved contacts"));
      lv_dropdown_set_selected(choice,_page==Messages?_folder:_source);
      lv_obj_add_event_cb(choice,selection,LV_EVENT_VALUE_CHANGED,this);
    }
    if (_page==Text) {
      auto* box=lv_obj_create(_root.get()); lv_obj_remove_style_all(box);
      lv_obj_set_pos(box,0,34); lv_obj_set_size(box,w,128); lv_obj_set_scroll_dir(box,LV_DIR_VER);
      auto* body=lv_label_create(box); lv_label_set_text(body,_body.c_str()); lv_obj_set_width(body,w-6);
      lv_obj_set_style_text_font(body,&theme::font14(),LV_PART_MAIN);
      lv_obj_set_style_text_color(body,lv_color_hex(theme::colors().COLOR_TEXT),LV_PART_MAIN);
    } else {
      for (size_t i=0;i<_rows.size() && i<4;++i) {
        std::string caption=_rows[i].title+"\n"+_rows[i].detail;
        button(caption.c_str(),0,34+int(i)*32,w,20+int(i),30);
      }
    }
    button(TR("First page"),0,166,(w-8)/2,6);
    if (_hasNext) button(TR("Next"),(w+8)/2,166,(w-8)/2,7);
    _notice=label(_info.c_str(),0,201,w,true);
  }
}
bool GuardianAppScreen::request(const char* operation,const std::string& json) {
  if (_awaiting || !guardian::rpcStart(json,millis())) {
    _info=TR("Waiting for Guardian BLE"); return false;
  }
  _operation=operation; _awaiting=true; _info=TR("Loading..."); return true;
}
void GuardianAppScreen::requestPage(bool next) {
  if (_awaiting) return;
  JsonDocument doc;
  _id="gm"+std::to_string(++_serial); doc["id"]=_id;
  const char* op=_page==Contacts?"contacts.list":_page==Text?"message.get":"messages.list";
  doc["op"]=op;
  if (next) { _offset=_next; doc["revision"]=_revision; }
  else { _offset=0; _revision.clear(); }
  doc["offset"]=_offset; doc["limit"]=_page==Text?512:4;
  if (_page==Contacts) doc["source"]=sources[_source];
  else if (_page==Text) doc["msg_id"]=_message;
  else doc["folder"]=folders[_folder];
  _rows.clear(); _body.clear(); _hasNext=false;
  request(op,guardian::jsonText(doc)); render();
}
void GuardianAppScreen::send() {
  if (_awaiting) return;
  captureDraft();
  if (!guardian::validateDraft(_draft)) { _info=TR("Enter recipient and message (max 4096 characters)."); render(); return; }
  if (!guardian::rpcReady()) { _info=TR("Waiting for Guardian BLE"); render(); return; }
  if (!_draft.pending()) _draft.token=guardian::newToken();
  if (!guardian::saveDraft(_draft)) { _info=TR("Cannot save draft. Nothing sent."); render(); return; }
  JsonDocument doc; _id="gm"+std::to_string(++_serial);
  doc["id"]=_id; doc["op"]="message.queue"; doc["token"]=_draft.token;
  doc["to"]=_draft.to; doc["subject"]=_draft.subject; doc["body"]=_draft.body; doc["priority"]=_draft.priority;
  request("message.queue",guardian::jsonText(doc)); render();
}
void GuardianAppScreen::response(const std::string& raw,const std::string& transportError) {
  if (!_awaiting) return;
  _awaiting=false;
  JsonDocument doc;
  std::string error=transportError;
  if (error.empty() && (!guardian::parseObject(doc,raw) || !doc["id"].is<const char*>() ||
      _id != doc["id"].as<const char*>() || !doc["ok"].is<bool>())) error="protocol_error";
  if (error.empty() && !doc["ok"].as<bool>()) error=doc["error"]["code"] | "protocol_error";
  if (!error.empty()) {
    if (error=="protocol_error") guardian::rpcAbort();
    if (_operation=="message.queue") _info=TR("Send unconfirmed. Check PC; retry uses the same token.");
    else if (error=="list_changed" || error=="message_changed") {
      _rows.clear(); _body.clear(); _revision.clear(); _hasNext=false;
      if (_root.get() && (_page==Messages || _page==Contacts || _page==Text)) { requestPage(); return; }
      _info=TR("List changed. Reload the first page.");
    } else _info=errorText(error);
    if (_operation=="message.queue" && error!="timeout" && error!="disconnected") _info += std::string("\n")+errorText(error);
    render(); return;
  }
  auto result=doc["result"].as<JsonObjectConst>();
  if (result.isNull()) { _info=TR("Invalid Guardian response"); render(); return; }
  if (_operation=="message.queue") {
    if (result["accepted"].is<bool>() && result["accepted"].as<bool>() && result["msg_id"].is<uint32_t>()) {
      guardian::Draft cleared;
      if (guardian::saveDraft(cleared)) {
        _draft=cleared; _info=TR("Queued on PC. Delivery is handled by Guardian."); _page=Dashboard;
      } else _info=TR("PC accepted the message; local confirmation could not be saved.");
    } else _info=TR("Send unconfirmed. Check PC; retry uses the same token.");
  } else if (_operation=="status.get") {
    _transfers.clear();
    for (auto item:result["transfers"].as<JsonArrayConst>()) {
      char line[48]; const char* dir=item["direction"] | "";
      snprintf(line,sizeof line,"R%u %s: ",item["radio"].as<unsigned>(),!strcmp(dir,"send")?"TX":"RX");
      _transfers+=line;
      if (item["percent"].is<unsigned>() && item["percent"].as<unsigned>()<=100) _transfers+=std::to_string(item["percent"].as<unsigned>())+"%";
      else _transfers+="—";
      _transfers+="  ";
    }
    _info=_transfers.empty()?TR("No active transfers"):_transfers;
  } else {
    if (!result["revision"].is<const char*>() ||
        (!result["next_offset"].isNull() && !result["next_offset"].is<uint32_t>())) {
      _info=TR("Invalid Guardian response"); render(); return;
    }
    _revision=result["revision"].as<const char*>(); _hasNext=!result["next_offset"].isNull();
    _next=result["next_offset"] | 0u;
    if (_hasNext && _next<=_offset) { _hasNext=false; _info=TR("Invalid Guardian response"); render(); return; }
    if (_operation=="message.get") {
      if (!result["body"].is<const char*>() || !result["msg_id"].is<uint32_t>() || result["msg_id"].as<uint32_t>()!=_message) {
        _info=TR("Invalid Guardian response"); render(); return;
      }
      _body=result["body"].as<const char*>();
      _heading=std::string(result["source"] | "")+" > "+(result["final_dest"] | "");
      _body=std::string(result["subject"] | "")+"\n\n"+_body;
      _info=TR("Reading does not mark the PC message as read.");
    } else {
      _rows.clear();
      for (auto item:result["items"].as<JsonArrayConst>()) {
        if (_rows.size()>=4) break;
        Row row;
        if (_operation=="contacts.list") {
          row.call=item["callsign"] | ""; row.title=row.call;
          row.detail=std::string(item["live"].as<bool>()?TR("Live"):TR("Saved"))+" · "+(item["grid"] | "");
          const char* via=item["live_next_hop"] | (item["next_hop"] | "");
          if (*via && row.call!=via) row.detail+=" > "+std::string(via);
          if (item["approved"].is<bool>() && !item["approved"].as<bool>()) row.detail+=" ?";
        } else {
          if (!item["msg_id"].is<uint32_t>()) continue;
          row.id=item["msg_id"].as<uint32_t>();
          row.title=std::string(item["read"].as<bool>()?"":"* ")+(item["source"] | "")+" > "+(item["final_dest"] | "");
          row.detail=std::string(messageStatus(item["status"] | ""))+" · "+(item["subject"] | "");
        }
        _rows.push_back(row);
      }
      _info=std::to_string(_offset+(_rows.empty()?0:1))+"–"+std::to_string(_offset+_rows.size())+" / "+std::to_string(result["total"] | 0u);
      if (_operation=="contacts.list") _info+=" · "+std::string(TR("Contact is not a delivery guarantee."));
    }
  }
  render();
}
void GuardianAppScreen::refresh(uint32_t now) {
  std::string raw,error;
  if (guardian::rpcTake(raw,error)) response(raw,error);
  if (!_root.get() || (_last && uint32_t(now-_last)<250)) return;
  _last=now;
  const auto state=guardian::snapshot(now); const bool online=state.session.fresh(now);
  if (_online && !online) { _rows.clear(); _revision.clear(); _hasNext=false; _body.clear(); _transfers.clear();
    if (_page!=Compose) { _info=TR("Guardian unavailable. Try again after reconnecting."); render(); }
  }
  _online=online;
  if (_page==Dashboard && _status) {
    char text[256];
    if (!online) snprintf(text,sizeof text,"%s\n\nInbox —\nOutbox —\nCAT —  CTRL —",TR("Guardian offline"));
    else {
      char tx[24],rx[24];
      auto pct=[](char* p,size_t n,bool active,uint8_t value) {
        if (!active) snprintf(p,n,"%s",TR("Idle"));
        else if (value<=100) snprintf(p,n,"%u%%",value);
        else snprintf(p,n,"—");
      };
      const auto& s=state.session.status;
      pct(tx,sizeof tx,s.flags&2,state.session.txPercent); pct(rx,sizeof rx,s.flags&4,state.session.rxPercent);
      snprintf(text,sizeof text,TR("TX %s   RX %s\nInbox %lu / new %lu\nOutbox %lu\nCAT %s  VARA %s\nCTRL %s"),tx,rx,
        (unsigned long)s.inbox,(unsigned long)s.unread,(unsigned long)s.outbox,
        s.flags&8?"+":"—",s.flags&16?"+":"—",s.flags&32?"+":"—");
    }
    if (strcmp(lv_label_get_text(_status),text)) lv_label_set_text(_status,text);
  }
}
void GuardianAppScreen::action(int a) {
  if (a==0) { captureDraft(); guardian::saveDraft(_draft); _page=Dashboard; render(); return; }
  if (_awaiting) return;
  if (a==1 || a==2) { _page=a==1?Messages:Contacts; requestPage(); }
  else if (a==3) { _page=Compose; _confirmNew=false; _info=_draft.pending()?TR("Check PC before retry if its database or BLE identity changed."):""; render(); }
  else if (a==4) {
    JsonDocument doc; _id="gm"+std::to_string(++_serial); doc["id"]=_id; doc["op"]="status.get";
    request("status.get",guardian::jsonText(doc)); render();
  } else if (a==5) send();
  else if (a==6 || a==7) requestPage(a==7);
  else if (a==8 && _draft.pending()) {
    if (!_confirmNew) { _confirmNew=true; _info=TR("The previous message may be queued on PC. Check it before starting a new message."); }
    else {
      guardian::Draft empty;
      if (guardian::saveDraft(empty)) { _draft=empty; _confirmNew=false; _info.clear(); }
      else _info=TR("Cannot save draft. Nothing sent.");
    }
    render();
  }
  else if (a>=20 && size_t(a-20)<_rows.size()) {
    const Row row=_rows[a-20];
    if (_page==Contacts) {
      if (!_draft.pending()) _draft.to=row.call;
      _page=Compose; _info=_draft.pending()?TR("Unconfirmed send"):""; render();
    } else { _message=row.id; _page=Text; _heading=row.title; requestPage(); }
  }
}
void GuardianAppScreen::clicked(lv_event_t* event) {
  const Binding binding=*static_cast<Binding*>(lv_event_get_user_data(event));
  if (binding.owner->_root.get()) binding.owner->action(binding.action);
}
void GuardianAppScreen::selection(lv_event_t* event) {
  auto* self=static_cast<GuardianAppScreen*>(lv_event_get_user_data(event));
  if (self->_awaiting) { lv_dropdown_set_selected(lv_event_get_target(event),self->_page==Messages?self->_folder:self->_source); return; }
  const unsigned selected=lv_dropdown_get_selected(lv_event_get_target(event));
  if (self->_page==Messages) self->_folder=selected; else self->_source=selected;
  self->requestPage();
}
} }

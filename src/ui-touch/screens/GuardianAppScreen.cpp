// SPDX-License-Identifier: GPL-3.0-or-later
#include "GuardianAppScreen.h"
#include "../services/GuardianLink.h"
#include "../services/GuardianRpcLink.h"
#include "../services/GuardianJson.h"
#include "../services/GuardianV3Client.h"
#include "../theme/Theme.h"
#include "../theme/Fonts.h"
#include "../widgets/Styles.h"
#include "../widgets/GuardianIcon.h"
#include "../i18n.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cmath>
#include <Arduino.h>
namespace ui { namespace screens {
namespace {
const lv_font_t* activityPercentFont() {
#if LV_FONT_MONTSERRAT_20
  return &lv_font_montserrat_20;
#else
  return &theme::font16();
#endif
}
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
std::string integer(const guardian::v3::Nullable<uint64_t>& value) {
  return value.known?std::to_string(value.value):"-";
}
std::string number(const guardian::v3::Nullable<double>& value) {
  if (!value.known) return "-";
  char text[48]; snprintf(text,sizeof text,"%.2f",value.value); return text;
}
std::string frequency(const guardian::v3::Nullable<uint64_t>& value) {
  return value.known?std::to_string(value.value)+" Hz":"-";
}
std::string displayFrequency(const guardian::v3::Nullable<uint64_t>& value) {
  if (!value.known || value.value<1000000) return frequency(value);
  char text[48]; snprintf(text,sizeof text,"%llu.%06llu",
    static_cast<unsigned long long>(value.value/1000000),static_cast<unsigned long long>(value.value%1000000));
  std::string result=text; const auto decimal=result.find('.');
  while(result.size()>decimal+4 && result.back()=='0') result.pop_back();
  return result+" MHz";
}
const char* yesNo(bool value) { return value?TR("Yes"):TR("No"); }
const char* readiness(guardian::v3::Readiness value,bool fresh) {
  if (!fresh) return TR("Stale station state");
  return value==guardian::v3::Readiness::Ready?TR("Ready"):
    value==guardian::v3::Readiness::NotReady?TR("Not ready"):TR("Unverified");
}
std::string calibrationState(const std::string& value) {
  if(value=="idle") return TR("Idle");
  if(value=="running") return TR("Running");
  return value;
}
std::string timeText(const guardian::v3::Nullable<double>& value) {
  if (!value.known || value.value<0 || value.value>2147483647.0) return "-";
  const auto stamp=static_cast<time_t>(value.value); const auto* utc=std::gmtime(&stamp);
  if (!utc) return "-";
  char text[48]; std::strftime(text,sizeof text,"%d.%m.%Y %H:%M UTC",utc); return text;
}
const char* controlTitle(const std::string& action) {
  if (action=="radio.connect") return TR("Connect radio");
  if (action=="radio.disconnect") return TR("Disconnect radio");
  if (action=="modem.connect") return TR("Connect VARA");
  if (action=="modem.disconnect") return TR("Disconnect VARA");
  if (action=="control.start") return TR("Start control");
  if (action=="control.stop") return TR("Stop control");
  if (action=="beacon.send") return TR("Queue beacon");
  if (action=="radio.tune") return TR("Tune radio");
  if (action=="profile.apply") return TR("Apply saved profile");
  if (action=="calibration.start") return TR("Start AutoTune");
  if (action=="calibration.cancel") return TR("Cancel AutoTune");
  return TR("Command");
}
}
uint32_t GuardianAppScreen::accent() const { return _appearance==guardian::Appearance::Green?0x15b6a6:0x0087ff; }
uint32_t GuardianAppScreen::surface() const {
  return _appearance==guardian::Appearance::Green?0x081713:0x0a1620;
}
void GuardianAppScreen::style(lv_obj_t* object, bool active) {
  widgets::styleCard(object);
  lv_obj_set_style_bg_color(object,lv_color_hex(active?0xf29d38:surface()),0);
  lv_obj_set_style_bg_opa(object,active?LV_OPA_COVER:LV_OPA_70,0);
  lv_obj_set_style_bg_grad_dir(object,LV_GRAD_DIR_NONE,0);
  lv_obj_set_style_bg_color(object,lv_color_hex(active?0xdb8522:surface()),LV_STATE_PRESSED);
  lv_obj_set_style_border_width(object,1,0);
  lv_obj_set_style_border_color(object,lv_color_hex(active?0xf29d38:accent()),0);
  lv_obj_set_style_border_opa(object,active?LV_OPA_COVER:LV_OPA_40,0);
  lv_obj_set_style_radius(object,7,0);
  lv_obj_set_style_shadow_width(object,0,0);
  lv_obj_set_style_text_color(object,lv_color_hex(active?0x1b1b1b:0xeaf1ff),0);
}
lv_obj_t* GuardianAppScreen::label(const char* value, int x, int y, int w, bool small, lv_obj_t* parent) {
  auto* l=lv_label_create(parent?parent:_root.get());
  lv_label_set_text(l,value); lv_obj_set_pos(l,x,y); lv_obj_set_width(l,w);
  lv_obj_set_style_text_font(l,small?&theme::font12():&theme::font14(),0);
  lv_obj_set_style_text_color(l,lv_obj_get_style_text_color(parent?parent:_root.get(),0),0);
  return l;
}
lv_obj_t* GuardianAppScreen::button(const char* title, int x, int y, int w, int action, int h, bool left, lv_obj_t* parent) {
  if (_bindingCount>=96) return nullptr;
  auto* b=lv_btn_create(parent?parent:_root.get()); style(b);
  lv_obj_set_style_pad_all(b,0,0);
  lv_obj_set_pos(b,x,y); lv_obj_set_size(b,w,h);
  auto* l=lv_label_create(b); lv_label_set_text(l,title); lv_obj_set_width(l,w-12);
  lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);
  lv_obj_set_height(l,16);
  lv_obj_set_style_text_font(l,&theme::font14(),0);
  if (lv_txt_get_width(title,strlen(title),&theme::font14(),0,LV_TEXT_FLAG_NONE)>w-12)
    lv_obj_set_style_text_font(l,&theme::font12(),0);
  lv_obj_set_style_text_align(l,left?LV_TEXT_ALIGN_LEFT:LV_TEXT_ALIGN_CENTER,0); lv_obj_center(l);
  auto& bind=_bindings[_bindingCount++]; bind.owner=this; bind.action=action;
  lv_obj_add_event_cb(b,clicked,LV_EVENT_CLICKED,&bind);
  return b;
}
void GuardianAppScreen::create(lv_obj_t* parent, void (*attach)(lv_obj_t*), void (*hideKeyboard)(), void (*home)(), void (*chrome)(const char*)) {
  _root.set(parent); _attach=attach; _hideKeyboard=hideKeyboard; _home=home; _chrome=chrome;
  if (!_loaded) { guardian::loadDraft(_draft); _loaded=true; }
  _appearance=guardian::loadAppearance(); _messageAlert=guardian::loadMessageAlert(); _page=Dashboard; ++_epoch;
  _info.clear(); _last=0; _lastPoll=millis(); _needPage=false; render();
}
bool GuardianAppScreen::back() {
  if (_refreshing) { finishRefresh(); return true; }
  if (!_root.get() || _page==Dashboard) return false;
  if (_page==Info) { action(17); return true; }
  if (v3Page()) {
    const Page target=_page==Station || _page==Alerts?Dashboard:
      _page==RadioDetail || _page==Transfers?Station:
      _page==Controls?RadioDetail:_page==AlertDetail?Alerts:
      _page==Confirm?_confirmReturn:Controls;
    go(target); return true;
  }
  if (_page==Contacts && _contactPicker) { _contactPicker=false; go(Compose); return true; }
  go(_page==Text?Messages:Dashboard); return true;
}
void GuardianAppScreen::captureDraft() {
  if (_page!=Compose || !_fields[0] || !_root.get() || _draft.pending()) return;
  _draft.to=lv_textarea_get_text(_fields[0]); _draft.subject=lv_textarea_get_text(_fields[1]);
  _draft.body=lv_textarea_get_text(_fields[2]); _draft.priority=lv_dropdown_get_selected(_priority);
}
void GuardianAppScreen::detach() {
  finishRefresh(); _rDown=_rFired=false;
  if (_root.get()) { captureDraft(); captureV3Fields(); if (_loaded) guardian::saveDraft(_draft); }
  if (_hideKeyboard && _root.get()) _hideKeyboard();
  ++_epoch; _root.set(nullptr); _notice=nullptr;
  for (auto*& f:_fields) f=nullptr;
}
void GuardianAppScreen::go(Page page) {
  const bool returnToHistory=_page==AlertDetail && page==Alerts;
  captureDraft(); captureV3Fields();
  if (_page==Compose) guardian::saveDraft(_draft);
  ++_epoch; _page=page; _info.clear(); _signature.clear(); _lastPoll=millis();
  _offset=0; _previous.clear(); _hasNext=false; _rows.clear(); _body.clear(); _pageLoaded=_cached=false;
  _needPage=page==Messages || page==Contacts || page==Text;
  if (_needPage) restoreCache();
  if (page==Alerts && !returnToHistory) {
    auto& client=guardian::v3::client(); client.requestHistory(_alertOffset,_alertOffset?client.history.revision:"");
  }
  render();
  if (_needPage && !_awaiting) requestPage();
}
void GuardianAppScreen::render() {
  if (!_root.get()) return;
  if (_hideKeyboard) _hideKeyboard();
  lv_obj_clean(_root.get()); _bindingCount=0; _notice=nullptr;
  _v3Scroll.set(nullptr);
  _activity=_percent=_bar=_inbox=_unread=_outbox=_arrowRx=_arrowTx=nullptr;
  _alertBadge=_v3Status=_v3Progress=nullptr;
  _priority=nullptr; _v3Actions.clear();
  for (auto*& f:_fields) f=nullptr;
  const char* title=_page==Dashboard?"Guardian":_page==Messages?TR("Messages"):
    _page==Contacts?TR("Network"):_page==Compose?TR("New message"):
    _page==Settings?TR("Guardian appearance"):_page==Text?TR("Message"):
    _page==Station?TR("Station"):_page==RadioDetail?TR("Radio detail"):
    _page==Transfers?TR("Active transfers"):_page==Controls?TR("Station control"):
    _page==Tune?TR("Tune radio"):_page==Profiles?TR("Saved profiles"):
    _page==AutoTune?"AutoTune":_page==Alerts || _page==AlertDetail?TR("Alerts"):
    _page==CommandResult?TR("Command result"):_page==Confirm?TR("Confirm command"):"Guardian";
  if (_chrome) _chrome(title);
  lv_obj_set_style_bg_color(_root.get(),lv_color_hex(0x000000),0);
  lv_obj_set_style_bg_opa(_root.get(),LV_OPA_COVER,0);
  lv_obj_set_style_text_color(_root.get(),lv_color_hex(0xeaf1ff),0);
  lv_obj_update_layout(_root.get());
  const int w=lv_obj_get_content_width(_root.get());
  const int h=lv_obj_get_content_height(_root.get());
  using Icon=widgets::GuardianIcon;
  auto icon=[&](lv_obj_t* parent,Icon kind,int x,int y,int size,uint32_t color) {
    auto* o=widgets::guardianIcon(parent,kind,size,color,lv_obj_get_style_text_color(parent,0));
    lv_obj_set_pos(o,x,y); return o;
  };
  auto line=[&](lv_obj_t* parent,int x,int y,int width,int height) {
    auto* o=lv_obj_create(parent); lv_obj_remove_style_all(o); lv_obj_set_pos(o,x,y); lv_obj_set_size(o,width,height);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x45647b),0); lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_clear_flag(o,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  };
  auto dot=[&](lv_obj_t* parent,int x,int y,int size,bool on) {
    auto* o=lv_obj_create(parent); lv_obj_remove_style_all(o); lv_obj_set_pos(o,x,y); lv_obj_set_size(o,size,size);
    lv_obj_set_style_radius(o,LV_RADIUS_CIRCLE,0); lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(o,lv_color_hex(on?0x32ed53:0x7395b9),0);
    lv_obj_clear_flag(o,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE); return o;
  };
  auto sub=[&](lv_obj_t* l) { lv_obj_set_style_text_color(l,lv_color_hex(theme::isDay()?0x45647b:0xa6bfde),0); };
  auto bare=[&](lv_obj_t* b) { lv_obj_set_style_bg_opa(b,LV_OPA_TRANSP,0); lv_obj_set_style_border_width(b,0,0); };
  auto iconButton=[&](const char* access,Icon kind,int x,int y,int width,int height,int action,bool active) {
    auto* b=button(access,x,y,width,action,height); style(b,active);
    lv_obj_add_flag(lv_obj_get_child(b,0),LV_OBJ_FLAG_HIDDEN);
    icon(b,kind,(width-23)/2,(height-23)/2,23,active?0x1b1b1b:accent()); return b;
  };
  if (v3Page()) renderV3(w,h);
  else if (_page==Dashboard) {
    const int right=110, gap=8, left=w-right-gap, usable=h-(_info.empty()?0:18);
    auto* panel=lv_obj_create(_root.get()); lv_obj_remove_style_all(panel); style(panel);
    lv_obj_set_style_pad_all(panel,0,0); lv_obj_set_size(panel,left,usable);
    lv_obj_clear_flag(panel,LV_OBJ_FLAG_SCROLLABLE);
    const uint32_t cyan=_appearance==guardian::Appearance::Green?0x18d9ad:0x03b4ec;
    _arrowRx=icon(panel,Icon::Down,10,17,22,cyan);
    _arrowTx=icon(panel,Icon::Up,10,17,22,0xffbd45);
    _activity=label("",40,21,left-100,true,panel);
    lv_label_set_long_mode(_activity,LV_LABEL_LONG_DOT);
    _percent=label("",left-60,17,52,false,panel);
    lv_obj_set_style_text_font(_percent,activityPercentFont(),0);
    lv_obj_set_style_text_align(_percent,LV_TEXT_ALIGN_RIGHT,0);
    _bar=lv_bar_create(panel); lv_obj_set_pos(_bar,10,51); lv_obj_set_size(_bar,left-20,12);
    lv_obj_set_style_bg_color(_bar,lv_color_hex(0x263e50),0); lv_obj_set_style_bg_opa(_bar,LV_OPA_COVER,0);
    lv_obj_set_style_radius(_bar,LV_RADIUS_CIRCLE,0); lv_obj_set_style_radius(_bar,LV_RADIUS_CIRCLE,LV_PART_INDICATOR);
    lv_bar_set_range(_bar,0,100);
    line(panel,10,77,left-20,1);
    const int half=(left-20)/2, mailY=usable-114;
    line(panel,10+half,mailY,1,62);
    icon(panel,Icon::Mail,10,mailY+12,24,accent());
    icon(panel,Icon::Send,15+half,mailY+12,24,accent());
    auto* incoming=label(TR("Inbox"),39,mailY+1,half-29,true,panel);
    auto* outgoing=label(TR("Outbox"),43+half,mailY+1,half-33,true,panel);
    lv_label_set_long_mode(incoming,LV_LABEL_LONG_CLIP); lv_label_set_long_mode(outgoing,LV_LABEL_LONG_WRAP);
    _inbox=label("-",39,mailY+28,half-30,false,panel);
    _outbox=label("-",43+half,mailY+28,half-33,false,panel);
    lv_label_set_long_mode(_inbox,LV_LABEL_LONG_DOT); lv_label_set_long_mode(_outbox,LV_LABEL_LONG_DOT);
    _unread=label("",39,mailY+57,half-30,true,panel);
    // Invisible hit targets cover the complete count blocks, including their icons.
    for (int i=0;i<2;++i) {
      auto* hit=lv_btn_create(panel); lv_obj_remove_style_all(hit);
      lv_obj_set_pos(hit,10+i*half,mailY); lv_obj_set_size(hit,half,72);
      auto& bind=_bindings[_bindingCount++]; bind.owner=this; bind.action=18+i;
      lv_obj_add_event_cb(hit,clicked,LV_EVENT_CLICKED,&bind);
    }
    line(panel,10,usable-41,left-20,1);
    const int third=(left-20)/3;
    const char* links[]={"CAT","VARA",TR("CTRL")};
    for(int i=0;i<3;++i) {
      _links[i]=dot(panel,11+i*third,usable-26,10,false);
      label(links[i],26+i*third,usable-28,third-12,true,panel);
    }
    const int footer=32, row=(usable-footer-4*gap)/4;
    const char* titles[]={TR("Messages"),TR("Network"),TR("Station"),TR("Write")};
    const Icon kinds[]={Icon::Mail,Icon::Network,Icon::Antenna,Icon::Pencil};
    const int actions[]={1,2,100,3};
    for(int i=0;i<4;++i) {
      auto* b=button(titles[i],left+gap,i*(row+gap),right,actions[i],std::max(28,row));
      if(i==3) style(b,true);
      auto* text=lv_obj_get_child(b,0); lv_obj_set_width(text,right-46);
      if(lv_txt_get_width(titles[i],strlen(titles[i]),&theme::font14(),0,LV_TEXT_FLAG_NONE)>right-46)
        lv_obj_set_style_text_font(text,&theme::font12(),0);
      lv_obj_align(text,LV_ALIGN_LEFT_MID,34,0); lv_obj_set_style_text_align(text,LV_TEXT_ALIGN_LEFT,0);
      icon(b,kinds[i],8,(row-23)/2,23,i==3?0x1b1b1b:accent());
      icon(b,Icon::Chevron,right-14,(row-12)/2,12,i==3?0x1b1b1b:accent());
    }
    const int footButton=(right-2*gap)/3, footY=usable-footer;
    iconButton(LV_SYMBOL_SETTINGS,Icon::Gear,left+gap,footY,footButton,footer,9,false);
    auto* alert=button(TR("Alerts"),left+gap+footButton+gap,footY,footButton,170,footer);
    lv_obj_add_flag(lv_obj_get_child(alert,0),LV_OBJ_FLAG_HIDDEN);
    icon(alert,Icon::Bell,3,5,22,accent());
    _alertBadge=label("-",16,1,14,true,alert); lv_obj_set_style_text_color(_alertBadge,lv_color_hex(0xf29d38),0);
    iconButton(LV_SYMBOL_HOME,Icon::Home,left+gap+2*(footButton+gap),footY,footButton,footer,10,false);
    updateDashboard(millis());
  } else if (_page==Settings) {
    label(TR("Theme"),0,2,w);
    auto* blue=button(TR("Blue"),0,25,w,11,38);
    auto* green=button(TR("Green"),0,69,w,12,38);
    lv_obj_set_style_border_color(blue,lv_color_hex(0x0087ff),0);
    lv_obj_set_style_border_color(green,lv_color_hex(0x15b6a6),0);
    lv_obj_set_style_border_width(_appearance==guardian::Appearance::Blue?blue:green,3,0);
    label(TR("New Guardian message"),0,112,w,true);
    button(_messageAlert?TR("Two beeps: on"):TR("Two beeps: off"),0,136,w,27,40);
    label(TR("Applies only to Guardian."),0,184,w,true);
  } else if (_page==Info) {
    auto* box=lv_obj_create(_root.get()); lv_obj_remove_style_all(box);
    lv_obj_set_size(box,w,h); lv_obj_set_scroll_dir(box,LV_DIR_VER);
    label(_info.c_str(),0,0,w-6,false,box);
  } else if (_page==Compose) {
    const int fieldX=59, fieldW=w-fieldX, bodyY=66, footerY=h-38-(_info.empty()?0:18);
    const char* hints[]={TR("Recipient"),TR("Subject"),TR("Message")};
    const std::string* values[]={&_draft.to,&_draft.subject,&_draft.body};
    for(int i=0;i<3;++i) {
      const int y=i==2?bodyY:i*33;
      label(hints[i],0,y+7,fieldX-3,true);
      auto* field=_fields[i]=lv_textarea_create(_root.get()); style(field);
      lv_obj_set_pos(field,fieldX,y); lv_obj_set_size(field,i==0?fieldW-31:fieldW,i==2?footerY-bodyY-7:27);
      lv_obj_set_style_text_font(field,&theme::font14(),0); lv_obj_set_style_pad_all(field,5,0);
      lv_textarea_set_one_line(field,i!=2); lv_textarea_set_max_length(field,i==0?16:i==1?256:4096);
      if(i==0) lv_textarea_set_accepted_chars(field,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/-");
      lv_textarea_set_text(field,values[i]->c_str());
      if(_draft.pending()) lv_obj_add_state(field,LV_STATE_DISABLED); else if(_attach) _attach(field);
    }
    auto* contacts=iconButton(LV_SYMBOL_DIRECTORY,Icon::Person,w-27,0,27,27,15,false);
    if(_draft.pending()) lv_obj_add_state(contacts,LV_STATE_DISABLED);
    label(TR("Priority"),0,footerY+8,fieldX-3,true);
    _priority=lv_dropdown_create(_root.get()); lv_obj_set_pos(_priority,fieldX,footerY); lv_obj_set_size(_priority,99,32); style(_priority);
    lv_obj_set_style_text_font(_priority,&theme::font12(),0); lv_obj_set_style_pad_all(_priority,7,0);
    lv_dropdown_set_options(_priority,TR("Normal\nPriority 1\nPriority 2\nPriority 3")); lv_dropdown_set_selected(_priority,_draft.priority);
    if(_draft.pending()) lv_obj_add_state(_priority,LV_STATE_DISABLED);
    const int sendX=fieldX+106;
    auto* sendButton=button(_draft.pending()?TR("Retry same send"):TR("Send"),sendX,footerY,w-sendX,5,32); style(sendButton,true);
    if(!_draft.pending()) {
      auto* text=lv_obj_get_child(sendButton,0); lv_obj_set_width(text,w-sendX-34); lv_obj_align(text,LV_ALIGN_LEFT_MID,31,0);
      icon(sendButton,Icon::Send,7,7,20,0x1b1b1b);
    }
    if(_draft.pending()) button(_confirmNew?TR("PC checked: start new"):TR("Start new message"),0,h+3,w,8,32);
  } else {
    int top=0;
    if(_page==Messages) {
      auto* choice=lv_dropdown_create(_root.get()); lv_obj_set_pos(choice,0,0); lv_obj_set_size(choice,w-51,31); style(choice);
      lv_obj_set_style_text_font(choice,&theme::font14(),0); lv_obj_set_style_pad_all(choice,6,0);
      const char* names[]={TR("Inbox"),TR("Outbox"),TR("Sent"),TR("Drafts"),TR("Transit")};
      std::string options;
      const auto state=guardian::snapshot(millis()); const bool fresh=state.session.fresh(millis());
      for(int i=0;i<5;++i) {
        if(i) options+='\n'; options+=names[i];
        if(fresh && (i==0 || i==1)) options+="  "+std::to_string(i==0?state.session.status.inbox:state.session.status.outbox);
      }
      lv_dropdown_set_options(choice,options.c_str()); lv_dropdown_set_selected(choice,_folder);
      lv_obj_add_event_cb(choice,selection,LV_EVENT_VALUE_CHANGED,this);
      iconButton(LV_SYMBOL_EDIT,Icon::Pencil,w-43,0,43,31,3,true); top=38;
    } else if(_page==Contacts) {
      const int gap=8, half=(w-gap)/2;
      auto* live=button(TR("Live routes"),0,0,half,13,32); style(live,_source==1);
      auto* saved=button(TR("Saved routes"),half+gap,0,w-half-gap,14,32); style(saved,_source==2); top=40;
    }
    if(_page==Text) {
      auto* box=lv_obj_create(_root.get()); lv_obj_remove_style_all(box);
      lv_obj_set_size(box,w,h-29); lv_obj_set_scroll_dir(box,LV_DIR_VER);
      label((_heading+"\n\n"+_body).c_str(),0,0,w-6,false,box);
    } else {
      const int slots=_page==Contacts?2:3, rowHeight=(h-top-29)/slots;
      for(size_t i=0;i<_rows.size() && i<size_t(slots);++i) {
        const bool contact=_page==Contacts;
        auto* b=button("",0,top+int(i)*rowHeight,w,20+int(i),rowHeight-3,true);
        lv_obj_set_style_border_color(b,lv_color_hex(0x304d61),0);
        if(contact) icon(b,_rows[i].saved?Icon::Bookmark:Icon::Network,12,(rowHeight-27)/2,23,0x03b4ec);
        auto* led=dot(b,contact?w-58:12,(rowHeight-13)/2,9,_rows[i].dot);
        if(!contact && _rows[i].dot) lv_obj_set_style_bg_color(led,lv_color_hex(0x03b4ec),0);
        const int x=contact?47:37;
        auto* titleLabel=label(_rows[i].title.c_str(),x,contact?11:6,w-x-36,false,b);
        lv_label_set_long_mode(titleLabel,LV_LABEL_LONG_DOT);
        auto* detail=label(_rows[i].detail.c_str(),x,contact?32:25,w-x-28,true,b); sub(detail);
        lv_obj_set_height(detail,rowHeight-(contact?36:29)); lv_label_set_long_mode(detail,LV_LABEL_LONG_DOT);
        icon(b,Icon::Chevron,w-23,(rowHeight-18)/2,15,accent());
      }
      if(_rows.empty()) label(!_pageLoaded && (_needPage||_awaiting)?TR("Loading..."):TR("No items"),8,top+20,w-16);
    }
    const int footerY=h-27, mid=w/2;
    auto* prev=button(LV_SYMBOL_LEFT,mid-64,footerY,32,6,26); bare(prev);
    auto* next=button(LV_SYMBOL_RIGHT,mid+32,footerY,32,7,26); bare(next);
    if(_previous.empty()) lv_obj_add_state(prev,LV_STATE_DISABLED);
    if(!_hasNext) lv_obj_add_state(next,LV_STATE_DISABLED);
    char page[40];
    if(_page==Text) snprintf(page,sizeof page,"%u",unsigned(_previous.size()+1));
    else snprintf(page,sizeof page,"%u / %lu",unsigned(_previous.size()+1),(unsigned long)std::max(1u,(_total+(_page==Contacts?1:2))/(_page==Contacts?2:3)));
    auto* count=label(_info.empty()?page:"",mid-31,footerY+5,62,true); lv_obj_set_style_text_align(count,LV_TEXT_ALIGN_CENTER,0);
  }
  if(!_info.empty() && _page!=Info) {
    const bool list=_page==Messages || _page==Contacts || _page==Text;
    _notice=button(noticeText(),list?(w-62)/2:0,list?h-27:h-17,list?62:w,16,list?26:16); bare(_notice);
    auto* l=lv_obj_get_child(_notice,0); lv_obj_set_style_text_font(l,&theme::font12(),0);
    lv_obj_set_height(l,14); lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);
  }
  if (_refreshing) showRefresh();
}
bool GuardianAppScreen::v3StationVisible() const {
  return _root.get() && (_page==Station || _page==RadioDetail || _page==Transfers || _page==Controls ||
    _page==Tune || _page==Profiles || _page==AutoTune || _page==CommandResult || _page==Confirm);
}
void GuardianAppScreen::captureV3Fields() {
  if (!_root.get()) return;
  if (_page==Tune && _fields[0]) {
    _tuneHz=lv_textarea_get_text(_fields[0]);
    char mode[257]{};
    if (_priority) lv_dropdown_get_selected_str(_priority,mode,sizeof mode);
    _tuneMode=mode;
  } else if (_page==AutoTune && _fields[0]) {
    _calPeer=lv_textarea_get_text(_fields[0]);
    _calMode=_priority && lv_dropdown_get_selected(_priority)?"full":"quick";
  }
}
void GuardianAppScreen::selectRadio(unsigned number) {
  // Selecting the current radio must not rebuild an editor or reset its input.
  if(number==_radio && (_page==Tune || _page==AutoTune || _page==Profiles)) return;
  captureV3Fields(); _radio=number;
  const auto* radio=guardian::v3::client().radio(number);
  _tuneHz=radio && radio->frequency_hz.known?std::to_string(radio->frequency_hz.value):"";
  _tuneMode=radio?radio->mode:""; _profile.clear(); _calPeer.clear();
  if (_page==Station) go(RadioDetail); else render();
}
std::string GuardianAppScreen::controlReason(const std::string& operation,uint32_t now) const {
  const auto& client=guardian::v3::client();
  if (!client.stationFresh(now)) return TR("Stale station state");
  if (!client.capabilities.control_allowed) return TR("Enable station control on PC");
  const auto* available=client.action(_radio,operation);
  if (!available || !available->supported) return TR("Not supported by this radio");
  if (!available->available) return available->reason.empty()?TR("Currently unavailable"):available->reason;
  if (client.intent.unresolved) return TR("Resolve the previous command first");
  if (_awaiting || client.busy()) return TR("Loading...");
  if (!client.controlsReady(now)) return TR("Refresh station before control");
  return "";
}
void GuardianAppScreen::control(const std::string& operation,const std::string& fields) {
  captureV3Fields();
  auto& client=guardian::v3::client();
  const auto reason=controlReason(operation,millis());
  if (!reason.empty()) { _info=reason; render(); return; }
  _controlAction=operation; _controlFields=fields; _intentDecision=0;
  const bool confirmation=operation=="control.stop" || operation=="radio.disconnect" ||
    operation=="modem.disconnect" || operation=="radio.tune" || operation=="profile.apply" ||
    operation=="calibration.cancel";
  if (confirmation) { _confirmReturn=_page; go(Confirm); return; }
  if (client.execute(_radio,operation,fields,millis())) go(CommandResult);
  else { _info=client.error.empty()?TR("Command was not sent"):client.error; render(); }
}
std::string GuardianAppScreen::recoveryReason(bool retry,uint32_t now) const {
  const auto& client=guardian::v3::client();
  if(!client.stationFresh(now)) return TR("Refresh station before control");
  if(client.storageFailed) return TR("Cannot save Guardian state");
  if(_awaiting || client.busy()) return TR("Loading...");
  if(!retry) return "";
  if(!client.intent.command_id.empty()) return TR("Refresh station");
  if(!client.capabilities.control_allowed) return TR("Enable station control on PC");
  if(!client.controlsReady(now)) return TR("Refresh station before control");
  const auto* action=client.action(client.intent.radio,client.intent.action);
  if(!action || !action->supported) return TR("Not supported by this radio");
  return "";
}
void GuardianAppScreen::updateCalibration() {
  if (!_v3Status) return;
  const auto* radio=guardian::v3::client().radio(_radio);
  if (!radio) return;
  const auto& cal=radio->calibration;
  std::string text=calibrationState(cal.state)+(cal.peer.empty()?"":" · "+cal.peer);
  const auto detail=cal.current.empty()?cal.message:cal.current;
  if (cal.progress.known) {
    char progress[48]; snprintf(progress,sizeof progress,"%.0f",cal.progress.value);
    text+="\n"+std::string(progress)+" / "+integer(cal.total);
  }
  if (!detail.empty()) text+='\n'+detail;
  lv_label_set_text(_v3Status,text.c_str());
  if (_v3Progress) {
    if (cal.progress.known && cal.total.known && cal.total.value>0) {
      const int percent=int(std::max(0.0,std::min(100.0,cal.progress.value*100.0/double(cal.total.value))));
      lv_bar_set_value(_v3Progress,percent,LV_ANIM_OFF); lv_obj_clear_flag(_v3Progress,LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(_v3Progress,LV_OBJ_FLAG_HIDDEN);
  }
}
void GuardianAppScreen::renderV3(int w,int h) {
  using Icon=widgets::GuardianIcon;
  auto& client=guardian::v3::client(); const uint32_t now=millis();
  const bool fresh=client.stationFresh(now); const auto* radio=client.radio(_radio);
  const auto* cap=client.radioCapabilities(_radio);
  const uint32_t cyan=_appearance==guardian::Appearance::Green?0x18d9ad:0x03b4ec;
  const uint32_t white=0xeaf1ff, muted=0xa6bfde, green=0x32ed53, amber=0xffbd45, grey=0x7395b9, red=0xf36b5f;
  const int viewport=h-(_info.empty()?0:20);
  auto* box=lv_obj_create(_root.get()); lv_obj_remove_style_all(box); _v3Scroll.set(box);
  lv_obj_set_size(box,w,viewport); lv_obj_set_scroll_dir(box,LV_DIR_VER);
  lv_obj_set_scrollbar_mode(box,LV_SCROLLBAR_MODE_AUTO); lv_obj_set_style_pad_all(box,0,0);
  int y=0;
  auto row=[](const char* key,const std::string& value) { return std::string(key)+": "+value+"\n"; };
  auto icon=[&](lv_obj_t* parent,Icon kind,int x,int top,int size,uint32_t color) {
    const uint32_t foreground=kind==Icon::Warning || kind==Icon::Info?surface():
      kind==Icon::Chevron || kind==Icon::Pencil?color:white;
    auto* object=widgets::guardianIcon(parent,kind,size,color,lv_color_hex(foreground));
    lv_obj_set_pos(object,x,top); return object;
  };
  auto dot=[&](lv_obj_t* parent,int x,int top,uint32_t color,int size=8) {
    auto* object=lv_obj_create(parent); lv_obj_remove_style_all(object);
    lv_obj_set_pos(object,x,top); lv_obj_set_size(object,size,size);
    lv_obj_set_style_radius(object,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(object,lv_color_hex(color),0); lv_obj_set_style_bg_opa(object,LV_OPA_COVER,0);
    lv_obj_clear_flag(object,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE); return object;
  };
  auto readyColor=[&](guardian::v3::Readiness ready) {
    return !fresh?grey:ready==guardian::v3::Readiness::Ready?green:
      ready==guardian::v3::Readiness::Unknown?amber:grey;
  };
  auto text=[&](lv_obj_t* parent,const std::string& value,int x,int top,int width,bool small=false,uint32_t color=0xeaf1ff) {
    auto* object=label(value.c_str(),x,top,std::max(8,width),small,parent);
    lv_obj_set_style_text_color(object,lv_color_hex(color),0);
    lv_label_set_long_mode(object,LV_LABEL_LONG_DOT); lv_obj_set_height(object,small?14:18);
    return object;
  };
  auto paragraph=[&](lv_obj_t* parent,const std::string& value,int x,int top,int width,bool small=true,uint32_t color=0xa6bfde) {
    auto* object=label(value.c_str(),x,top,std::max(8,width),small,parent);
    lv_obj_set_style_text_color(object,lv_color_hex(color),0);
    lv_label_set_long_mode(object,LV_LABEL_LONG_WRAP); lv_obj_update_layout(object); return object;
  };
  auto line=[&](lv_obj_t* parent,int top) {
    auto* object=lv_obj_create(parent); lv_obj_remove_style_all(object);
    lv_obj_set_pos(object,8,top); lv_obj_set_size(object,w-16,1);
    lv_obj_set_style_bg_color(object,lv_color_hex(0x45647b),0); lv_obj_set_style_bg_opa(object,LV_OPA_COVER,0);
    lv_obj_clear_flag(object,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  };
  auto panel=[&](lv_obj_t* parent,int x,int top,int width,int height,int action=-1) {
    lv_obj_t* object;
    if(action>=0) { object=button("",x,top,width,action,height,true,parent); lv_obj_add_flag(lv_obj_get_child(object,0),LV_OBJ_FLAG_HIDDEN); }
    else { object=lv_obj_create(parent); style(object); lv_obj_set_pos(object,x,top); lv_obj_set_size(object,width,height); lv_obj_clear_flag(object,LV_OBJ_FLAG_CLICKABLE); }
    lv_obj_set_style_pad_all(object,0,0); lv_obj_clear_flag(object,LV_OBJ_FLAG_SCROLLABLE); return object;
  };
  auto card=[&](const std::string& title,const std::string& value,int action=-1,Icon kind=Icon::Info) {
    auto* object=panel(box,0,y,w,32,action);
    icon(object,kind,8,7,18,cyan);
    auto* heading=paragraph(object,title,34,7,w-(action>=0?56:44),false,white);
    int height=std::max(32,14+int(lv_obj_get_height(heading)));
    if(!value.empty()) { auto* detail=paragraph(object,value,9,height-2,w-18); height+=4+lv_obj_get_height(detail); }
    if(action>=0) icon(object,Icon::Chevron,w-20,8,14,white);
    lv_obj_set_height(object,height); y+=height+4; return object;
  };
  auto selectors=[&]() {
    const int gap=6, half=(w-gap)/2;
    for(unsigned number=1;number<=2;++number) {
      auto* object=button(number==1?TR("Radio 1"):TR("Radio 2"),number==1?0:half+gap,y,
        number==1?half:w-half-gap,100+number,30,false,box);
      const bool selected=_radio==number; style(object,selected);
      auto* caption=lv_obj_get_child(object,0); lv_obj_set_width(caption,half-39);
      lv_obj_align(caption,LV_ALIGN_LEFT_MID,32,0); lv_obj_set_style_text_align(caption,LV_TEXT_ALIGN_LEFT,0);
      icon(object,Icon::Antenna,8,6,19,selected?0x1b1b1b:cyan);
    }
    y+=34;
  };
  auto operationIcon=[](const std::string& operation) {
    return operation=="beacon.send"?Icon::Antenna:operation=="radio.tune"?Icon::Radio:
      operation=="profile.apply"?Icon::Bookmark:operation.find("calibration.")==0?Icon::Gear:Icon::Network;
  };
  // The reason remains a full-width label, including when the live gate changes.
  auto actionRow=[&](const std::string& operation,int action,const std::string& description="",bool primary=false) {
    const auto reason=controlReason(operation,now);
    auto* holder=panel(box,0,y,w,description.empty()?44:58);
    auto* object=button(controlTitle(operation),0,0,w,action,30,true,holder); style(object,primary);
    auto* caption=lv_obj_get_child(object,0); lv_obj_set_width(caption,w-58);
    lv_obj_align(caption,LV_ALIGN_LEFT_MID,35,0);
    icon(object,operationIcon(operation),9,6,19,primary?0x1b1b1b:cyan);
    icon(object,Icon::Chevron,w-22,8,14,primary?0x1b1b1b:white);
    const auto value=description+(description.empty() || reason.empty()?"":"\n")+reason;
    auto* explanation=paragraph(holder,value.empty()?" ":value,8,31,w-16,true,reason.empty()?muted:amber);
    lv_obj_set_height(explanation,description.empty()?13:26); lv_label_set_long_mode(explanation,LV_LABEL_LONG_WRAP);
    _v3Actions.push_back({object,explanation,operation,description});
    if(!reason.empty()) lv_obj_add_state(object,LV_STATE_DISABLED);
    y+=lv_obj_get_height(holder)+4; return object;
  };
  auto stateAction=[&](const char* title,const std::string& state,bool active,const std::string& operation,int action) {
    auto* holder=panel(box,0,y,w,46);
    dot(holder,9,12,fresh && active?green:grey,9);
    text(holder,title,26,3,w-176); text(holder,state,26,19,w-176,true,muted);
    const int actionWidth=142;
    auto* object=button(controlTitle(operation),w-actionWidth-4,3,actionWidth,action,28,false,holder);
    const bool primary=!active; style(object,primary);
    auto* caption=lv_obj_get_child(object,0); lv_obj_set_width(caption,actionWidth-23);
    lv_obj_set_style_text_font(caption,&theme::font12(),0);
    lv_obj_align(caption,LV_ALIGN_LEFT_MID,7,0); lv_obj_set_style_text_align(caption,LV_TEXT_ALIGN_LEFT,0);
    icon(object,Icon::Chevron,actionWidth-17,8,12,primary?0x1b1b1b:white);
    const auto reason=controlReason(operation,now);
    auto* explanation=text(holder,reason.empty()?" ":reason,8,32,w-16,true,amber);
    _v3Actions.push_back({object,explanation,operation,""});
    if(!reason.empty()) lv_obj_add_state(object,LV_STATE_DISABLED);
    y+=50;
  };
  auto input=[&](lv_obj_t* parent,const char* title,const std::string& value,unsigned index,int top,bool digits=false) {
    const int left=113;
    auto* caption=paragraph(parent,title,8,top+1,left-12,true,white);
    if(lv_obj_get_height(caption)<20) lv_obj_set_y(caption,top+8);
    auto* field=_fields[index]=lv_textarea_create(parent); style(field);
    lv_obj_set_pos(field,left,top); lv_obj_set_size(field,w-left-7,30);
    lv_obj_set_style_pad_all(field,5,0); lv_obj_set_style_text_font(field,&theme::font14(),0);
    lv_textarea_set_one_line(field,true); lv_textarea_set_max_length(field,digits?11:16);
    lv_textarea_set_accepted_chars(field,digits?"0123456789":"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/-");
    lv_textarea_set_text(field,value.c_str()); if(_attach) _attach(field);
  };
  auto dropdown=[&](lv_obj_t* parent,const char* title,const std::vector<std::string>& values,const std::string& selected,int top) {
    const int left=113; text(parent,title,8,top+8,left-12,true);
    _priority=lv_dropdown_create(parent); style(_priority); lv_obj_set_pos(_priority,left,top);
    lv_obj_set_size(_priority,w-left-7,30); lv_obj_set_style_pad_all(_priority,5,0);
    lv_obj_set_style_text_font(_priority,&theme::font14(),0);
    std::string options; unsigned index=0;
    for(size_t i=0;i<values.size();++i) { if(i) options+='\n'; options+=values[i]; if(values[i]==selected) index=unsigned(i); }
    lv_dropdown_set_options(_priority,options.empty()?"-":options.c_str()); lv_dropdown_set_selected(_priority,index);
    if(values.empty()) lv_obj_add_state(_priority,LV_STATE_DISABLED);
  };
  if(_page==Station) {
    auto* header=panel(box,0,y,w,26);
    auto* call=text(header,client.station.callsign.empty()?TR("Station"):client.station.callsign+" / "+client.station.grid,8,2,w-29);
    lv_obj_set_style_text_font(call,activityPercentFont(),0); lv_obj_set_height(call,23);
    dot(header,w-18,9,readyColor(client.station.ready)); y+=30;
    for(unsigned number=1;number<=2;++number) {
      const auto* item=client.radio(number); auto* object=panel(box,0,y,w,66,100+number);
      icon(object,Icon::Radio,9,23,24,cyan);
      text(object,std::string(number==1?TR("Radio 1"):TR("Radio 2"))+(item?" · "+item->name:""),40,7,w-64);
      icon(object,Icon::Chevron,w-20,26,14,white);
      const auto ready=item?item->ready:guardian::v3::Readiness::Unknown; const auto color=readyColor(ready);
      dot(object,42,37,color,8); text(object,item?readiness(ready,fresh):TR("Radio not configured"),55,34,100,true,color);
      if(item) {
        text(object,displayFrequency(item->frequency_hz)+" · "+item->mode,158,29,w-184,true);
        const std::string state=item->modem.lifecycle=="per_transfer"?TR("Starts for each transfer"):
          item->control.active?TR("Control active"):TR("Control stopped");
        text(object,item->modem.selected+" · "+state,40,49,w-64,true,muted);
      }
      y+=70;
    }
    const int gap=6, half=(w-gap)/2;
    auto* mail=panel(box,0,y,half,32,1); icon(mail,Icon::Mail,7,8,19,cyan);
    text(mail,std::string(TR("Inbox"))+" "+integer(client.station.mailbox.inbox),33,2,half-37,true);
    text(mail,std::string(TR("Outbox"))+" "+integer(client.station.mailbox.outbox),33,17,half-37,true,muted);
    auto* server=panel(box,half+gap,y,w-half-gap,32);
    icon(server,Icon::Network,7,8,18,cyan); dot(server,31,6,fresh && client.station.server.state=="connected"?green:grey,7);
    text(server,"ARDOS-CZ",44,2,half-48,true);
    text(server,client.station.server.state=="connected"?TR("Connected"):client.station.server.state,31,17,half-37,true,muted);
    y+=36;
    card(TR("Active transfers"),TR("Progress is not delivery confirmation"),110,Icon::Down);
    const auto& mailbox=client.station.mailbox;
    card(TR("Mailbox"),row(TR("Inbox"),integer(mailbox.inbox))+row(TR("Unread"),integer(mailbox.unread))+
      row(TR("Outbox"),integer(mailbox.outbox))+row(TR("Failed outbox"),integer(mailbox.outbox_failed))+row(TR("Transit"),integer(mailbox.transit)),-1,Icon::Mail);
    card("ARDOS-CZ",row(TR("Enabled"),yesNo(client.station.server.enabled))+row(TR("State"),client.station.server.state)+
      row(TR("Last verified"),timeText(client.station.server.last_verified)),-1,Icon::Network);
    card(TR("Alerts"),row(TR("Unread"),integer(client.station.alerts.unseen)),170,Icon::Bell);
    card(TR("Station"),"Guardian "+client.station.version+"\n"+row(TR("Last update"),timeText(client.station.updated_at))+
      row(TR("Revision"),integer(client.station.revision))+row(TR("Ready"),readiness(client.station.ready,fresh))+
      row(TR("Readiness verified"),yesNo(client.station.readiness_verified)));
  } else if(_page==RadioDetail) {
    selectors();
    if(!radio) { card(TR("Radio not configured"),TR("Configure this radio on PC"),-1,Icon::Radio); return; }
    const auto& modem=radio->modem; const auto& control=radio->control;
    auto* summary=panel(box,0,y,w,168);
    auto summaryRow=[&](unsigned index,Icon kind,const std::string& title,uint32_t color,int action) {
      auto* object=panel(summary,0,index*28,w,28,action);
      lv_obj_set_style_radius(object,0,0); lv_obj_set_style_border_width(object,0,0);
      icon(object,kind,9,5,18,cyan); text(object,title,35,5,w-72,true);
      if(color) dot(object,w-38,10,color,8);
      icon(object,Icon::Chevron,w-20,8,12,white);
      if(index) { auto* divider=lv_obj_create(object); lv_obj_remove_style_all(divider);
        lv_obj_set_pos(divider,1,0); lv_obj_set_size(divider,w-2,1);
        lv_obj_set_style_bg_color(divider,lv_color_hex(0x304d61),0); lv_obj_set_style_bg_opa(divider,LV_OPA_COVER,0); }
    };
    summaryRow(0,Icon::Antenna,radio->name+" · "+(radio->connected?TR("Connected"):TR("Offline")),fresh && radio->connected?green:grey,111);
    summaryRow(1,Icon::Radio,displayFrequency(radio->frequency_hz)+" · "+radio->mode,0,112);
    summaryRow(2,Icon::Network,modem.selected+" · "+readiness(modem.ready,fresh),readyColor(modem.ready),111);
    summaryRow(3,Icon::Gear,modem.lifecycle=="per_transfer"?TR("Starts for each transfer"):std::string(TR("Lifecycle"))+": "+modem.lifecycle,0,111);
    summaryRow(4,Icon::Network,control.active?TR("Control active"):TR("Control stopped"),fresh && control.active?green:grey,111);
    summaryRow(5,Icon::Gear,std::string(TR("Calibration"))+": "+calibrationState(radio->calibration.state),fresh && radio->calibration.state!="idle"?amber:grey,114);
    y+=172;
    card(TR("Station control"),TR("Choose an explicit radio for every command"),111,Icon::Antenna);
    card(TR("Active transfers"),row(TR("Sessions"),integer(radio->total_sessions)),110,Icon::Down);
    card(TR("Radio detail"),row(TR("Ready"),readiness(radio->ready,fresh))+row(TR("Readiness verified"),yesNo(radio->readiness_verified))+
      row(TR("Connected"),yesNo(radio->connected))+row(TR("Revision"),integer(radio->revision))+
      row(TR("Backend"),radio->backend)+row(TR("Frequency"),frequency(radio->frequency_hz))+row(TR("Mode"),radio->mode)+
      row("PTT",yesNo(radio->ptt))+row(TR("Signal"),number(radio->signal))+row(TR("Busy"),yesNo(radio->busy))+radio->busy_reason+"\n"+radio->error,-1,Icon::Radio);
    card(TR("Modem"),row(TR("Selected"),modem.selected)+row(TR("Name"),modem.name)+row(TR("Configured"),yesNo(modem.configured))+
      row(TR("Started"),yesNo(modem.started))+row(TR("Ready"),readiness(modem.ready,fresh))+row(TR("Readiness verified"),yesNo(modem.readiness_verified))+
      row(TR("State"),modem.state)+row(TR("Lifecycle"),modem.lifecycle)+row(TR("Active transport"),modem.active_transport)+row(TR("Control modem"),modem.control_modem)+
      row(TR("VARA mode"),modem.vara_mode)+row(TR("VARA command port"),yesNo(modem.vara.command_connected))+
      row(TR("VARA data port"),yesNo(modem.vara.data_connected))+row(TR("Transport lost"),yesNo(modem.vara.transport_lost))+modem.vara.link_state+"\n"+modem.vara.error,-1,Icon::Network);
    card(TR("Control"),row(TR("Active"),yesNo(control.active))+row(TR("Suspended for payload"),yesNo(control.suspended_for_payload))+
      row(TR("Sessions"),integer(control.active_sessions))+row(TR("Handoff pending"),yesNo(control.handoff_pending))+control.handoff_error+"\n"+
      row(TR("Pending operations"),integer(radio->pending_operations)),-1,Icon::Antenna);
    const auto& calibration=radio->calibration;
    card("AutoTune",row(TR("State"),calibrationState(calibration.state))+row(TR("Peer callsign"),calibration.peer)+row(TR("Session ID"),calibration.session_id)+
      row(TR("Mode"),calibration.mode)+row(TR("Progress"),number(calibration.progress)+" / "+integer(calibration.total))+
      calibration.current+"\n"+calibration.message+"\n"+calibration.error+"\n"+row(TR("Pending offer"),yesNo(!calibration.pending_offer_json.empty())),114,Icon::Gear);
  } else if(_page==Transfers) {
    if(!fresh) { text(box,TR("Stale station state"),5,y,w-10,true,amber); y+=18; }
    bool any=false;
    for(const auto& item:client.station.radios) for(const auto& transfer:item.transfers) {
      any=true; const bool receiving=transfer.direction=="receive"; const auto color=receiving?cyan:amber;
      auto* object=panel(box,0,y,w,82); icon(object,receiving?Icon::Down:Icon::Up,9,20,30,color);
      text(object,std::string(item.radio==1?TR("Radio 1"):TR("Radio 2"))+" · "+(receiving?TR("Receiving (RX)"):TR("Sending (TX)")),46,6,w-54);
      text(object,transfer.source+" > "+transfer.destination,46,25,w-113);
      char percent[32]; if(transfer.percent.known) snprintf(percent,sizeof percent,"%.0f %%",transfer.percent.value); else snprintf(percent,sizeof percent,"-");
      auto* count=text(object,percent,w-63,25,55); lv_obj_set_style_text_font(count,activityPercentFont(),0); lv_obj_set_height(count,23);
      lv_obj_set_style_text_align(count,LV_TEXT_ALIGN_RIGHT,0);
      auto* bar=lv_bar_create(object); lv_obj_set_pos(bar,46,51); lv_obj_set_size(bar,w-56,9);
      lv_obj_set_style_bg_color(bar,lv_color_hex(0x263e50),LV_PART_MAIN); lv_obj_set_style_bg_opa(bar,LV_OPA_COVER,LV_PART_MAIN);
      lv_obj_set_style_radius(bar,5,LV_PART_MAIN); lv_obj_set_style_radius(bar,5,LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(bar,lv_color_hex(color),LV_PART_INDICATOR);
      lv_bar_set_value(bar,transfer.percent.known?int(transfer.percent.value):0,LV_ANIM_OFF);
      text(object,integer(transfer.progress_bytes)+" / "+integer(transfer.total_bytes)+" B · "+transfer.transport,46,64,w-54,true,muted); y+=88;
    }
    if(!any) card(TR("No active transfers"),"", -1,Icon::Down);
    icon(box,Icon::Info,3,y,17,muted); text(box,TR("Progress is not delivery confirmation"),26,y+1,w-29,true,muted); y+=23;
    for(const auto& item:client.station.radios) for(const auto& transfer:item.transfers) {
      card(std::string(item.radio==1?TR("Radio 1"):TR("Radio 2"))+" · "+transfer.msg_id,
        row(TR("State"),transfer.state)+row(TR("Via"),transfer.via)+row(TR("Message ID"),transfer.msg_id)+
        row(TR("Transport"),transfer.transport)+row(TR("Progress"),number(transfer.percent)+(transfer.percent.known?" %":""))+
        row(TR("Bitrate"),number(transfer.bitrate_bps)+" bit/s")+(transfer.total_bytes.known && !transfer.total_bytes_exact?TR("Estimated"):""),-1,Icon::Info);
    }
  } else if(_page==Controls) {
    selectors();
    stateAction(TR("Radio"),radio && radio->connected?TR("Connected"):TR("Offline"),radio && radio->connected,
      radio && radio->connected?"radio.disconnect":"radio.connect",120);
    const bool vara=radio && radio->modem.vara.command_connected && radio->modem.vara.data_connected;
    stateAction("VARA",vara?TR("Connected"):TR("Offline"),vara,vara?"modem.disconnect":"modem.connect",121);
    stateAction(TR("Control"),radio && radio->control.active?TR("Control active"):TR("Control stopped"),radio && radio->control.active,
      radio && radio->control.active?"control.stop":"control.start",122);
    icon(box,Icon::Info,3,y,16,muted); text(box,TR("Only the selected radio is controlled"),25,y+1,w-28,true,muted); y+=23;
    actionRow("beacon.send",123,TR("Queued does not confirm transmission"));
    actionRow("radio.tune",112); actionRow("profile.apply",113); actionRow("calibration.start",114);
    if(client.intent.unresolved || !client.command.command_id.empty()) card(TR("Command result"),client.commandLabel(),115,Icon::Info);
  } else if(_page==Tune) {
    selectors(); auto* fields=panel(box,0,y,w,70);
    input(fields,TR("Frequency (Hz)"),_tuneHz,0,4,true);
    dropdown(fields,TR("Mode"),cap?cap->tune_modes:std::vector<std::string>{},_tuneMode,36); y+=74;
    if(cap && cap->tune_mode=="frequency_only") card(TR("Frequency only"),TR("FM only, 10 Hz steps; modulation profile stays unchanged"),-1,Icon::Info);
    actionRow("radio.tune",130,"",true);
    text(box,TR("Review the exact frequency and mode before applying"),4,y,w-8,true,muted); y+=18;
  } else if(_page==Profiles) {
    selectors();
    if(cap) {
      if(_profile.empty() && !cap->profiles.empty()) _profile=cap->profiles[0];
      for(size_t i=0;i<cap->profiles.size();++i) {
        auto* object=panel(box,0,y,w,28,140+int(i)); const bool selected=_profile==cap->profiles[i]; style(object,selected);
        auto* ring=dot(object,12,7,selected?0x1b1b1b:white,14); lv_obj_set_style_bg_opa(ring,LV_OPA_TRANSP,0);
        lv_obj_set_style_border_width(ring,2,0); lv_obj_set_style_border_color(ring,lv_color_hex(selected?0x1b1b1b:white),0);
        if(selected) dot(object,16,11,0x1b1b1b,6);
        text(object,cap->profiles[i],36,5,w-46,false,selected?0x1b1b1b:white); y+=31;
      }
      if(cap->total_profiles.known && cap->total_profiles.value>cap->profiles.size()) card(TR("More profiles exist on PC"),integer(cap->total_profiles),-1,Icon::Bookmark);
    }
    icon(box,Icon::Bookmark,4,y,17,muted); text(box,TR("Profiles are created on PC"),28,y+2,w-32,true,muted); y+=23;
    auto* apply=actionRow("profile.apply",131,"",true); if(_profile.empty()) lv_obj_add_state(apply,LV_STATE_DISABLED);
  } else if(_page==AutoTune) {
    selectors(); auto* fields=panel(box,0,y,w,66);
    input(fields,TR("Peer callsign"),_calPeer,0,2);
    dropdown(fields,TR("Mode"),{TR("Quick"),TR("Full")},_calMode=="full"?TR("Full"):TR("Quick"),34); y+=70;
    auto* status=panel(box,0,y,w,68); icon(status,Icon::Gear,8,8,20,cyan);
    _v3Status=paragraph(status,"",35,5,w-43,true,white); lv_obj_set_height(_v3Status,48);
    _v3Progress=lv_bar_create(status); lv_obj_set_pos(_v3Progress,9,57); lv_obj_set_size(_v3Progress,w-18,8);
    lv_obj_set_style_bg_color(_v3Progress,lv_color_hex(0x263e50),LV_PART_MAIN);
    lv_obj_set_style_bg_color(_v3Progress,lv_color_hex(cyan),LV_PART_INDICATOR);
    lv_obj_set_style_radius(_v3Progress,5,LV_PART_MAIN); lv_obj_set_style_radius(_v3Progress,5,LV_PART_INDICATOR);
    updateCalibration(); y+=72;
    const bool running=radio && radio->calibration.state!="idle" && !radio->calibration.state.empty();
    const int half=(w-6)/2;
    for(unsigned index=0;index<2;++index) {
      const std::string operation=index?"calibration.cancel":"calibration.start";
      auto* object=button(controlTitle(operation),index?half+6:0,y,index?w-half-6:half,
        index?133:132,30,false,box);
      style(object,index?running:!running);
      lv_obj_set_style_text_font(lv_obj_get_child(object,0),&theme::font12(),0);
      const auto reason=controlReason(operation,now);
      auto* explanation=paragraph(box,reason.empty()?" ":reason,8,y+34+int(index)*30,w-16,true,amber);
      lv_obj_set_height(explanation,26);
      _v3Actions.push_back({object,explanation,operation,""});
      if(!reason.empty()) lv_obj_add_state(object,LV_STATE_DISABLED);
    }
    y+=94;
    if(radio) card(TR("Calibration"),radio->calibration.current+"\n"+radio->calibration.message+"\n"+radio->calibration.error+
      "\n"+row(TR("Session ID"),radio->calibration.session_id)+row(TR("Pending offer"),yesNo(!radio->calibration.pending_offer_json.empty())),-1,Icon::Gear);
  } else if(_page==Alerts) {
    lv_obj_set_height(box,std::max(30,viewport-32));
    auto* header=panel(box,0,y,w,28); icon(header,Icon::Bell,9,5,19,cyan);
    text(header,integer(client.station.alerts.unseen)+" "+TR("Unread"),36,5,w-45); y+=32;
    if(client.historyTruncated) card(TR("Older alert history is incomplete"),TR("Some events are no longer available on PC"),-1,Icon::Warning);
    if(!client.historyLoaded && client.history.items.empty()) card(TR("Loading..."),"");
    else if(client.history.items.empty()) card(TR("No items"),"",-1,Icon::Bell);
    for(size_t i=0;i<client.history.items.size();++i) {
      const auto& alert=client.history.items[i]; const bool emergency=alert.code>=1 && alert.code<=3;
      auto* object=panel(box,0,y,w,44,180+int(i));
      if(emergency) icon(object,Icon::Warning,9,10,23,red);
      else dot(object,14,16,alert.seen?grey:alert.code==18?green:amber,13);
      text(object,client.alertLabel(alert.code)+" · "+alert.source,42,5,w-63,false,alert.seen?muted:white);
      auto stamp=timeText(alert.created); if(stamp.size()>16) stamp=stamp.substr(11,5);
      text(object,alert.note+(alert.note.empty()?"":" · ")+stamp,42,24,w-65,true,muted);
      icon(object,Icon::Chevron,w-19,15,13,white); y+=48;
    }
    card(TR("Reload alerts"),"",173,Icon::Bell);
    const int footer=viewport-28;
    auto* previous=button(LV_SYMBOL_LEFT,w/2-72,footer,38,171,28,false,_root.get());
    auto* next=button(LV_SYMBOL_RIGHT,w/2+34,footer,38,172,28,false,_root.get());
    if(_alertPrevious.empty() || client.historyPending()) lv_obj_add_state(previous,LV_STATE_DISABLED);
    if(!client.history.next_offset.known || client.historyPending()) lv_obj_add_state(next,LV_STATE_DISABLED);
    auto* count=text(_root.get(),std::to_string(_alertOffset+1)+" / "+std::to_string(client.history.total),w/2-31,footer+6,62,true);
    lv_obj_set_style_text_align(count,LV_TEXT_ALIGN_CENTER,0);
  } else if(_page==AlertDetail) {
    for(const auto& item:client.history.items) if(item.alert_id==_alert.alert_id) { _alert=item; break; }
    const bool emergency=_alert.code>=1 && _alert.code<=3;
    auto* object=panel(box,0,y,w,32); icon(object,emergency?Icon::Warning:Icon::Bell,9,7,24,emergency?red:cyan);
    auto* heading=paragraph(object,client.alertLabel(_alert.code),42,7,w-51,false,white);
    lv_obj_set_style_text_font(heading,&theme::font16(),0); lv_obj_update_layout(heading);
    int height=14+lv_obj_get_height(heading);
    auto* note=paragraph(object,_alert.note,10,height,w-20,false,white); height+=lv_obj_get_height(note)+9;
    line(object,height); height+=8;
    auto* metadata=paragraph(object,row(TR("Source"),_alert.source)+row(TR("Origin"),_alert.origin.network)+
      row(TR("Radio"),integer(_alert.radio))+timeText(_alert.created),10,height,w-20);
    height+=lv_obj_get_height(metadata)+8; line(object,height); height+=7;
    lv_obj_set_height(object,height); y+=height+5;
    auto* seen=button(_alert.seen?TR("Read"):TR("Mark as read"),0,y,w,174,32,false,box);
    style(seen,!_alert.seen); if(_alert.seen) lv_obj_add_state(seen,LV_STATE_DISABLED); y+=37;
    text(box,TR("Reading does not confirm assistance"),3,y,w-6,true,muted); y+=20;
    card(TR("Alert source"),row(TR("Device"),_alert.origin.device)+row(TR("RF ID"),integer(_alert.rf_id))+row(TR("Forwarded"),yesNo(_alert.forwarded))+
      row(TR("Read"),yesNo(_alert.seen))+"\n"+_alert.alert_id,-1,Icon::Info);
  } else if(_page==CommandResult) {
    const auto& command=client.command; const auto& intent=client.intent;
    const unsigned selected=command.radio?command.radio:intent.radio;
    const std::string operation=command.action.empty()?intent.action:command.action;
    const bool unknown=command.state==guardian::v3::CommandState::Unknown;
    const bool failed=command.state==guardian::v3::CommandState::Failed;
    const uint32_t color=failed?red:command.succeeded() && command.state!=guardian::v3::CommandState::Queued?green:amber;
    auto* object=panel(box,0,y,w,70); icon(object,failed?Icon::Warning:Icon::Info,10,11,27,color);
    auto* heading=paragraph(object,unknown?TR("Result unknown"):client.commandLabel(),46,10,w-56,false,color);
    lv_obj_set_style_text_font(heading,&theme::font16(),0); lv_obj_update_layout(heading);
    int height=std::max(47,18+int(lv_obj_get_height(heading))); line(object,height); height+=10;
    text(object,selected==1?TR("Radio 1"):TR("Radio 2"),10,height,75);
    text(object,controlTitle(operation),88,height,w-98); height+=24;
    std::string detail=command.error.code+(command.error.message.empty()?"":"\n")+command.error.message;
    if(unknown) detail+=(detail.empty()?"":"\n")+std::string(TR("Check the station before deciding; the command may already have run"));
    if(command.state==guardian::v3::CommandState::Queued) detail+=(detail.empty()?"":"\n")+std::string(TR("Queued does not confirm transmission"));
    if(!detail.empty()) { auto* description=paragraph(object,detail,10,height,w-20); height+=lv_obj_get_height(description)+10; }
    lv_obj_set_height(object,height); y+=height+7;
    if(intent.unresolved && unknown) {
      const int half=(w-6)/2;
      auto* back=button(TR("Back to control"),0,y,half,111,32,false,box);
      auto* refresh=button(TR("Refresh station"),half+6,y,w-half-6,160,32,false,box); style(refresh,true);
      (void)back; y+=38;
      auto* retry=card(TR("Retry same command"),TR("Keeps the original token and fields"),161,Icon::Info);
      auto* discard=card(TR("Stop retrying this command"),TR("Does not cancel a command already running on PC"),162,Icon::Info);
      if(!recoveryReason(true,now).empty()) lv_obj_add_state(retry,LV_STATE_DISABLED);
      if(!recoveryReason(false,now).empty()) lv_obj_add_state(discard,LV_STATE_DISABLED);
    } else { auto* done=button(TR("Back to control"),0,y,w,111,32,false,box); style(done,true); y+=36; }
  } else if(_page==Confirm) {
    const auto& intent=client.intent; const unsigned selected=_intentDecision?intent.radio:_radio;
    const std::string operation=_intentDecision?intent.action:_controlAction;
    const std::string fields=_intentDecision?intent.fields:_controlFields;
    const auto reason=_intentDecision?recoveryReason(_intentDecision==1,now):controlReason(operation,now);
    const int bodyHeight=viewport-38-(reason.empty()?0:18);
    lv_obj_set_height(box,std::max(40,bodyHeight));
    auto* object=panel(box,0,0,w,std::max(40,bodyHeight));
    icon(object,Icon::Warning,(w-33)/2,9,33,amber);
    auto* heading=text(object,_intentDecision==2?TR("Stop retrying this command"):TR("Confirm command"),8,48,w-16,false,white);
    lv_obj_set_style_text_font(heading,&theme::font16(),0); lv_obj_set_style_text_align(heading,LV_TEXT_ALIGN_CENTER,0);
    std::string detail=std::string(selected==1?TR("Radio 1"):TR("Radio 2"))+" · "+controlTitle(operation)+"\n";
    JsonDocument doc;
    if(guardian::parseObject(doc,fields)) {
      if(!doc["frequency_hz"].isNull()) detail+=row(TR("Frequency (Hz)"),std::to_string(doc["frequency_hz"].as<uint64_t>()));
      for(const auto& key:{"mode","profile","peer"}) if(doc[key].is<const char*>())
        detail+=row(!strcmp(key,"mode")?TR("Mode"):!strcmp(key,"profile")?TR("Profile"):TR("Peer callsign"),doc[key].as<const char*>());
    }
    if(operation=="control.stop") detail+=TR("Also stops payload transfers and calibration");
    if(_intentDecision==1) detail+='\n'+std::string(TR("Retry uses the same token"));
    if(_intentDecision==2) detail+='\n'+std::string(TR("The original command may already have run"));
    auto* description=paragraph(object,detail,10,75,w-20,false,muted);
    lv_obj_set_style_text_align(description,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_height(object,std::max(bodyHeight,83+int(lv_obj_get_height(description))));
    y=lv_obj_get_height(object)+4;
    if(!reason.empty()) text(_root.get(),reason,3,viewport-53,w-6,true,amber);
    const int half=(w-6)/2;
    button(TR("Cancel"),0,viewport-32,half,164,32,false,_root.get());
    auto* confirm=button(TR("Confirm"),half+6,viewport-32,w-half-6,163,32,false,_root.get()); style(confirm,true);
    if(!reason.empty()) lv_obj_add_state(confirm,LV_STATE_DISABLED);
  }
  if(client.storageFailed) card(TR("Cannot save Guardian state"),TR("Events are not acknowledged until safely saved"),-1,Icon::Warning);
  if(!client.error.empty()) card(TR("Guardian"),client.error,-1,Icon::Info);
}
size_t GuardianAppScreen::CachedPage::bytes() const {
  size_t size=sizeof(CachedPage)+key.size()+revision.size()+signature.size()+body.size()+heading.size();
  for (const auto& row:rows) size+=sizeof(Row)+row.title.size()+row.detail.size()+row.call.size();
  return size;
}
std::string GuardianAppScreen::cacheKey() const {
  return std::to_string(_page)+":"+std::to_string(_page==Contacts?_source:_page==Text?_message:_folder)+":"+std::to_string(_offset);
}
bool GuardianAppScreen::restoreCache() {
  const auto key=cacheKey();
  for (auto it=_cache.begin();it!=_cache.end();++it) if (it->key==key) {
    auto entry=std::move(*it); _cache.erase(it);
    _rows=entry.rows; _revision=entry.revision; _signature=entry.signature;
    _body=entry.body; _heading=entry.heading; _hasNext=entry.hasNext; _next=entry.next; _total=entry.total;
    _cache.push_back(std::move(entry)); _pageLoaded=_cached=true; return true;
  }
  return false;
}
void GuardianAppScreen::saveCache() {
  CachedPage entry; entry.key=cacheKey(); entry.rows=_rows; entry.revision=_revision;
  entry.signature=_signature; entry.body=_body; entry.heading=_heading;
  entry.hasNext=_hasNext; entry.next=_next; entry.total=_total;
  for (auto it=_cache.begin();it!=_cache.end();) {
    if (it->key==entry.key) it=_cache.erase(it); else ++it;
  }
  // Bounded RAM cache: recent pages only, never a second mailbox on flash.
  size_t bytes=entry.bytes(); if (bytes>24*1024) return;
  for (const auto& cached:_cache) bytes+=cached.bytes();
  while (!_cache.empty() && (bytes>24*1024 || _cache.size()>=12)) {
    bytes-=_cache.front().bytes(); _cache.erase(_cache.begin());
  }
  _cache.push_back(std::move(entry));
}
void GuardianAppScreen::invalidateCache() {
  const auto key=cacheKey(); const auto prefix=key.substr(0,key.rfind(':')+1);
  for (auto it=_cache.begin();it!=_cache.end();) {
    if (it->key.compare(0,prefix.size(),prefix)==0) it=_cache.erase(it); else ++it;
  }
}
const char* GuardianAppScreen::noticeText() const {
  if (_info==TR("Waiting for Guardian BLE") || _info==TR("Guardian offline")) return TR("Offline");
  if (_info==TR("Queued on PC. Delivery is handled by Guardian.")) return TR("Queued");
  if (_info==TR("Sending...")) return TR("Sending...");
  if (_info==TR("Loading...")) return TR("Loading...");
  return TR("Details");
}
bool GuardianAppScreen::acceptsRefreshKey() const {
  return _root.get() && _page!=Compose && _page!=Info && _page!=Tune && _page!=AutoTune;
}
void GuardianAppScreen::refreshKey(bool down,uint32_t now) {
  if (!acceptsRefreshKey() || !down) { _rDown=_rFired=false; return; }
  if (!_rDown) { _rAt=now; _rDown=true; }
  if (!_rFired && uint32_t(now-_rAt)>=2000) { _rFired=true; manualRefresh(); }
}
void GuardianAppScreen::manualRefresh() {
  if (!acceptsRefreshKey() || _refreshing) return;
  if (v3Page()) {
    auto& client=guardian::v3::client(); client.refreshStation();
    if (_page==Alerts) client.requestHistory(_alertOffset,client.history.revision);
    _info.clear(); render(); return;
  }
  _refreshing=_refreshStatus=true; _refreshPage=_page==Messages || _page==Contacts || _page==Text;
  _refreshAt=millis(); showRefresh();
}
void GuardianAppScreen::showRefresh() {
  if (!_root.get() || _refreshModal.get()) return;
  auto* overlay=lv_obj_create(_root.get()); _refreshModal.set(overlay);
  lv_obj_remove_style_all(overlay); lv_obj_set_size(overlay,lv_pct(100),lv_pct(100));
  lv_obj_set_style_bg_color(overlay,lv_color_black(),0); lv_obj_set_style_bg_opa(overlay,LV_OPA_80,0);
  auto* box=lv_obj_create(overlay); style(box); lv_obj_set_size(box,190,112); lv_obj_center(box);
  lv_obj_clear_flag(box,LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_style_pad_all(box,0,0);
  auto* spinner=lv_spinner_create(box,900,70); lv_obj_set_size(spinner,28,28); lv_obj_align(spinner,LV_ALIGN_TOP_MID,0,8);
  lv_obj_set_style_arc_color(spinner,lv_color_hex(0xf29d38),LV_PART_INDICATOR);
  auto* text=label(TR("Loading..."),10,44,170,true,box); lv_obj_set_style_text_align(text,LV_TEXT_ALIGN_CENTER,0);
  auto* cancel=lv_btn_create(box); style(cancel); lv_obj_set_size(cancel,100,28); lv_obj_align(cancel,LV_ALIGN_BOTTOM_MID,0,-8);
  auto* caption=lv_label_create(cancel); lv_label_set_text(caption,TR("Cancel")); lv_obj_set_style_text_font(caption,&theme::font12(),0); lv_obj_center(caption);
  lv_obj_add_event_cb(cancel,[](lv_event_t* e) { static_cast<GuardianAppScreen*>(lv_event_get_user_data(e))->finishRefresh(); },LV_EVENT_CLICKED,this);
}
void GuardianAppScreen::finishRefresh() {
  _refreshing=_refreshStatus=_refreshPage=false;
  // The parent can be rebuilt immediately after a response; do not leave a
  // deferred deletion pointing into a tree render() is about to destroy.
  if (auto* modal=_refreshModal.get()) { _refreshModal.set(nullptr); lv_obj_del(modal); }
}
bool GuardianAppScreen::request(const char* operation,const std::string& json) {
  if (_awaiting || guardian::v3::client().busy() || !guardian::rpcStart(json,millis())) return false;
  _operation=operation; _awaiting=true; _requestRefresh=false; _requestPage=_page; _requestEpoch=_epoch;
  return true;
}
void GuardianAppScreen::requestPage(bool next, bool background, bool keepOffset) {
  if (_awaiting) { _needPage=!background; return; }
  if (!(_page==Messages || _page==Contacts || _page==Text)) return;
  JsonDocument doc; _id="gm"+std::to_string(++_serial); doc["id"]=_id;
  const char* op=_page==Contacts?"contacts.list":_page==Text?"message.get":"messages.list";
  doc["op"]=op;
  const uint32_t offset=next?_next:(keepOffset?_offset:0);
  if ((next || keepOffset) && !_revision.empty()) doc["revision"]=_revision;
  doc["offset"]=offset; doc["limit"]=_page==Text?512:_page==Contacts?2:3;
  if (_page==Contacts) doc["source"]=sources[_source];
  else if (_page==Text) doc["msg_id"]=_message;
  else doc["folder"]=folders[_folder];
  if (!request(op,guardian::jsonText(doc))) {
    _needPage=true;
    if (!background && _info!=TR("Waiting for Guardian BLE")) { _info=TR("Waiting for Guardian BLE"); render(); }
    return;
  }
  _background=background; _needPage=false; _lastPoll=millis();
  if (next) _previous.push_back(_offset);
  if (!next && !keepOffset) { _previous.clear(); _revision.clear(); }
  const bool same=_offset==offset && _pageLoaded;
  _offset=offset;
  if (!background) {
    if (!same) { _rows.clear(); _body.clear(); _signature.clear(); _hasNext=false; _pageLoaded=_cached=false; }
    if (same || restoreCache()) _background=true;
    _info.clear(); render();
  }
}
void GuardianAppScreen::send() {
  if (_awaiting) return;
  captureDraft();
  if (!guardian::validateDraft(_draft)) { _info=TR("Enter recipient and message (max 4096 characters)."); render(); return; }
  if (guardian::v3::client().busy() || !guardian::rpcReady()) { _info=TR("Waiting for Guardian BLE"); render(); return; }
  if (!_draft.pending()) _draft.token=guardian::newToken();
  if (!guardian::saveDraft(_draft)) { _info=TR("Cannot save draft. Nothing sent."); render(); return; }
  JsonDocument doc; _id="gm"+std::to_string(++_serial); doc["id"]=_id; doc["op"]="message.queue"; doc["token"]=_draft.token;
  doc["to"]=_draft.to; doc["subject"]=_draft.subject; doc["body"]=_draft.body; doc["priority"]=_draft.priority;
  _info=request("message.queue",guardian::jsonText(doc))?TR("Sending..."):TR("Waiting for Guardian BLE"); render();
}
void GuardianAppScreen::response(const std::string& raw,const std::string& transportError) {
  if (!_awaiting) return;
  _awaiting=false;
  const bool visible=_root.get() && _requestEpoch==_epoch && _requestPage==_page;
  _lastPoll=millis();
  // A background list reply must never rebuild a different page or erase typing.
  if (_operation!="message.queue" && !visible) return;
  JsonDocument doc; std::string error=transportError;
  if (error.empty() && (!guardian::parseObject(doc,raw) || !doc["id"].is<const char*>() ||
      _id!=doc["id"].as<const char*>() || !doc["ok"].is<bool>())) error="protocol_error";
  if (error.empty() && !doc["ok"].as<bool>()) error=doc["error"]["code"] | "protocol_error";
  if (!error.empty()) {
    if (error=="protocol_error") guardian::rpcAbort();
    if (_operation=="message.queue") _info=TR("Send unconfirmed. Check PC; retry uses the same token.");
    else if (error=="list_changed" || error=="message_changed") {
      invalidateCache(); _revision.clear(); _offset=0; _previous.clear(); _needPage=true; _pageLoaded=false;
      const bool manual=_requestRefresh;
      requestPage(); _requestRefresh=manual; return;
    } else _info=errorText(error);
    if (_requestRefresh) finishRefresh();
    _lastPoll=millis();
    if (visible) render(); return;
  }
  auto result=doc["result"].as<JsonObjectConst>();
  if (result.isNull()) { _info=TR("Invalid Guardian response"); if (_requestRefresh) finishRefresh(); if (visible) render(); return; }
  if (_operation=="status.get") {
    const char* flags[]={"tx","rx","radio_connected","vara_connected","control_active"};
    bool valid=result["inbox"].is<uint32_t>() && result["outbox"].is<uint32_t>() && result["unread"].is<uint32_t>();
    for (auto* flag:flags) valid=valid && result[flag].is<bool>();
    for (auto* name:{"tx_percent","rx_percent"})
      valid=valid && (result[name].isNull() || (result[name].is<unsigned>() && result[name].as<unsigned>()<=100));
    if (valid) {
      _queriedStatus.connect(); _queriedStatus.received=true; _queriedStatus.receivedAt=millis();
      auto& status=_queriedStatus.status;
      status.inbox=result["inbox"]; status.outbox=result["outbox"]; status.unread=result["unread"];
      status.flags=1; for (unsigned i=0;i<5;++i) if (result[flags[i]].as<bool>()) status.flags|=2<<i;
      _queriedStatus.txPercent=result["tx_percent"] | uint8_t(255); _queriedStatus.rxPercent=result["rx_percent"] | uint8_t(255);
      _info.clear();
    } else _info=TR("Invalid Guardian response");
    if (_requestRefresh && (!valid || !_refreshPage)) finishRefresh();
    if (visible) render(); return;
  }
  if (_operation=="message.queue") {
    if (result["accepted"].is<bool>() && result["accepted"].as<bool>() && result["msg_id"].is<uint32_t>()) {
      guardian::Draft cleared;
      if (guardian::saveDraft(cleared)) {
        _draft=cleared;
        _cache.clear(); // queued mail changes list membership and counts
        if (visible) { _page=Dashboard; ++_epoch; _info=TR("Queued on PC. Delivery is handled by Guardian."); }
      } else _info=TR("PC accepted the message; local confirmation could not be saved.");
    } else _info=TR("Send unconfirmed. Check PC; retry uses the same token.");
    if (visible) render(); return;
  }
  if (!result["revision"].is<const char*>() ||
      (!result["next_offset"].isNull() && !result["next_offset"].is<uint32_t>())) {
    _info=TR("Invalid Guardian response"); if (_requestRefresh) finishRefresh(); render(); return;
  }
  // A new revision alone must not steal focus from an unchanged list.
  std::string signature;
  if (_operation=="message.get") serializeJson(result,signature);
  else {
    serializeJson(result["items"],signature);
    signature+='|'+std::to_string(result["total"] | 0u);
    signature+='|'+std::to_string(result["next_offset"] | 0u);
  }
  _revision=result["revision"].as<const char*>();
  _pageLoaded=true; _cached=false;
  if (_background && signature==_signature && _info.empty()) {
    saveCache(); if (_requestRefresh) finishRefresh(); return;
  }
  _signature=signature;
  _hasNext=!result["next_offset"].isNull(); _next=result["next_offset"] | 0u;
  if (_hasNext && _next<=_offset) { _hasNext=false; _info=TR("Invalid Guardian response"); if (_requestRefresh) finishRefresh(); render(); return; }
  _info.clear();
  if (_operation=="message.get") {
    if (!result["body"].is<const char*>() || !result["msg_id"].is<uint32_t>() || result["msg_id"].as<uint32_t>()!=_message) {
      _info=TR("Invalid Guardian response"); if (_requestRefresh) finishRefresh(); render(); return;
    }
    _heading=std::string(result["source"] | "")+" > "+(result["final_dest"] | "");
    _body=std::string(result["subject"] | "")+"\n\n"+result["body"].as<const char*>();
  } else {
    _rows.clear(); _total=result["total"] | 0u;
    for (auto item:result["items"].as<JsonArrayConst>()) {
      if (_rows.size()>=size_t(_page==Contacts?2:3)) break;
      Row row;
      if (_operation=="contacts.list") {
        row.call=item["callsign"] | "";
        const bool live=item["live"] | false, saved=item["saved"] | false;
        row.title=row.call;
        row.saved=saved; row.dot=live;
        auto route=[&](const char* via) { return !*via?std::string("-"):row.call==via?std::string(TR("Direct")):std::string(TR("Via"))+" "+via; };
        // Saved and live routes are different fields, even for the same callsign.
        const char* stored=item["next_hop"] | ""; const char* current=item["live_next_hop"] | "";
        if (_source==2) row.detail=route(stored);
        else if (_source==1) row.detail=route(current);
        else {
          if (saved) row.detail=std::string(TR("Saved"))+": "+route(stored);
          if (live) { if (!row.detail.empty()) row.detail+="\n"; row.detail+=std::string(TR("Live"))+": "+route(current); }
        }
        if (item["approved"].is<bool>() && !item["approved"].as<bool>()) row.detail+=" · "+std::string(TR("Unapproved"));
      } else {
        if (!item["msg_id"].is<uint32_t>()) continue;
        row.id=item["msg_id"].as<uint32_t>();
        row.dot=!item["read"].as<bool>();
        row.title=(_folder==1 || _folder==2 || _folder==3)?(item["final_dest"] | ""):(item["source"] | "");
        row.detail=item["subject"] | "";
      }
      _rows.push_back(row);
    }
  }
  saveCache();
  if (_requestRefresh) finishRefresh();
  const lv_coord_t scroll=lv_obj_get_scroll_y(_root.get());
  render();
  if (_background && _root.get()) lv_obj_scroll_to_y(_root.get(),scroll,LV_ANIM_OFF);
}
void GuardianAppScreen::updateDashboard(uint32_t now) {
  if (!_activity) return;
  const auto state=guardian::snapshot(now);
  // Explicit RPC refresh is a local view, never a substitute for the BLE heartbeat.
  const bool online=state.session.fresh(now);
  const auto& session=online && _queriedStatus.fresh(now) &&
    int32_t(_queriedStatus.receivedAt-state.session.receivedAt)>0?_queriedStatus:state.session;
  const auto& s=session.status;
  const uint8_t active=s.flags&6;
  if (!online || !active) _direction=0;
  else if (!(_direction&active)) _direction=(active&4)?4:2;
  const uint8_t percent=_direction==4?session.rxPercent:session.txPercent;
  lv_label_set_text(_activity,!online?TR("Offline"):_direction==4?TR("Receiving (RX)"):_direction==2?TR("Sending (TX)"):TR("Ready"));
  if (online && _direction==4) lv_obj_clear_flag(_arrowRx,LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(_arrowRx,LV_OBJ_FLAG_HIDDEN);
  if (online && _direction==2) lv_obj_clear_flag(_arrowTx,LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(_arrowTx,LV_OBJ_FLAG_HIDDEN);
  char value[32];
  if (!online || !_direction) value[0]=0;
  else if (percent<=100) snprintf(value,sizeof value,"%u %%",percent);
  else strcpy(value,"-");
  lv_label_set_text(_percent,value);
  lv_obj_set_style_text_font(_percent,percent==100?&theme::font16():activityPercentFont(),0);
  if (online && _direction && percent<=100) {
    lv_obj_clear_flag(_bar,LV_OBJ_FLAG_HIDDEN); lv_bar_set_value(_bar,percent,LV_ANIM_OFF);
    lv_obj_set_style_bg_color(_bar,lv_color_hex(_direction==4?0x10bdea:0xffbd45),LV_PART_INDICATOR);
  } else lv_obj_add_flag(_bar,LV_OBJ_FLAG_HIDDEN);
  auto count=[&](lv_obj_t* object,uint32_t number) {
    if (online) snprintf(value,sizeof value,"%lu",(unsigned long)number); else strcpy(value,"-");
    lv_label_set_text(object,value);
    lv_obj_set_style_text_font(object,number>999?&theme::font14():number>99?activityPercentFont():&lv_font_montserrat_28,0);
  };
  count(_inbox,s.inbox); count(_outbox,s.outbox);
  if (online) snprintf(value,sizeof value,TR("%lu new"),(unsigned long)s.unread); else value[0]=0;
  lv_label_set_text(_unread,value); lv_obj_set_style_text_color(_unread,lv_color_hex(accent()),0);
  for (int i=0;i<3;++i) {
    const auto color=online&&(s.flags&(8<<i))?0x32ed53:0x7395b9;
    lv_obj_set_style_bg_color(_links[i],lv_color_hex(color),0);
  }
  if (_alertBadge) {
    const auto& client=guardian::v3::client();
    const auto& unread=client.station.alerts.unseen;
    const std::string badge=client.stationFresh(now) && unread.known?(unread.value>9?"9+":std::to_string(unread.value)):"-";
    lv_label_set_text(_alertBadge,badge.c_str());
  }
}
void GuardianAppScreen::refresh(uint32_t now) {
  auto& client=guardian::v3::client();
  client.tick(now,!_awaiting,v3StationVisible());
  std::string raw,error;
  if (!client.busy() && guardian::rpcTake(raw,error)) response(raw,error);
  // Start a deferred visible page immediately after the background v3 owner
  // releases RPC; visual throttling must not delay request ownership handoff.
  if (_root.get() && !_refreshing && !_awaiting && !client.busy() &&
      guardian::snapshot(now).session.fresh(now) && guardian::rpcReady()) {
    if (_needPage) requestPage(false,_pageLoaded,_offset!=0);
    else if ((_page==Messages || _page==Contacts) && uint32_t(now-_lastPoll)>=5000)
      requestPage(false,true,true);
  }
  if (!_root.get() || (_last && uint32_t(now-_last)<250)) return;
  _last=now;
  const auto state=guardian::snapshot(now); const bool online=state.session.fresh(now);
  if (!state.session.connected) _queriedStatus.disconnect();
  if (_online && !online) {
    if (_page==Messages || _page==Contacts || _page==Text) { _info=TR("Guardian offline"); _needPage=true; render(); }
  }
  _online=online;
  const bool fresh=client.stationFresh(now);
  const bool changed=client.revision!=_v3Revision || fresh!=_v3Fresh || client.busy()!=_v3Busy;
  _v3Revision=client.revision; _v3Fresh=fresh; _v3Busy=client.busy();
  if (v3Page()) {
    if ((_page==Alerts || _page==AlertDetail) && client.historyLoaded && !client.historyPending() &&
        client.loadedHistoryOffset()!=_alertOffset) {
      _alertOffset=client.loadedHistoryOffset(); _alertPrevious.clear();
    }
    const bool form=_page==Tune || _page==AutoTune || _page==Profiles;
    if (changed && !form) {
      const lv_coord_t scroll=lv_obj_get_scroll_y(_v3Scroll.get()?_v3Scroll.get():_root.get()); render();
      lv_obj_scroll_to_y(_v3Scroll.get()?_v3Scroll.get():_root.get(),scroll,LV_ANIM_OFF);
    }
    for (const auto& binding:_v3Actions) {
      auto reason=controlReason(binding.action,now);
      if (_page==Profiles && binding.action=="profile.apply" && _profile.empty()) reason=TR("No items");
      if (reason.empty()) lv_obj_clear_state(binding.object,LV_STATE_DISABLED);
      else lv_obj_add_state(binding.object,LV_STATE_DISABLED);
      const auto text=binding.description+(binding.description.empty() || reason.empty()?"":"\n")+reason;
      lv_label_set_text(binding.reason,text.empty()?" ":text.c_str());
    }
    if (_page==AutoTune) updateCalibration();
  }
  if (_page==Dashboard) updateDashboard(now);
  if (_refreshing) {
    if (!state.session.connected && uint32_t(now-_refreshAt)>=1200) {
      finishRefresh(); _info=TR("Guardian offline"); render();
    } else if (!_awaiting && guardian::rpcReady()) {
      if (_refreshStatus) {
        JsonDocument doc; _id="gm"+std::to_string(++_serial); doc["id"]=_id; doc["op"]="status.get";
        if (request("status.get",guardian::jsonText(doc))) { _requestRefresh=true; _refreshStatus=false; }
      } else if (_refreshPage) {
        requestPage(false,true,true);
        if (_awaiting) { _requestRefresh=true; _refreshPage=false; }
      }
    } else if (!_awaiting && uint32_t(now-_refreshAt)>=8000) {
      finishRefresh(); _info=TR("Waiting for Guardian BLE"); render();
    }
    if (_refreshing) return;
  }
}
void GuardianAppScreen::action(int a) {
  if (a==0) { back(); return; }
  if (a==10) { captureDraft(); guardian::saveDraft(_draft); if (_home) _home(); return; }
  if (a==9) { go(Settings); return; }
  if (a==13 || a==14) { _source=a==13?1:2; go(Contacts); return; }
  if (a==15 && !_draft.pending()) { _contactPicker=true; go(Contacts); return; }
  if (a==11 || a==12) {
    const auto choice=a==11?guardian::Appearance::Blue:guardian::Appearance::Green;
    if (guardian::saveAppearance(choice)) { _appearance=choice; _info.clear(); }
    else _info=TR("Cannot save theme");
    render(); return;
  }
  if (a==27) {
    if (guardian::saveMessageAlert(!_messageAlert)) { _messageAlert=!_messageAlert; _info.clear(); }
    else _info=TR("Cannot save setting");
    render(); return;
  }
  if (a==16) { captureDraft(); _infoReturn=_page; _page=Info; ++_epoch; render(); return; }
  if (a==17) { _page=_infoReturn; ++_epoch; render(); return; }
  if (a==1 || a==2) { _contactPicker=false; go(a==1?Messages:Contacts); return; }
  if (a==3) { go(Compose); _confirmNew=false; _info=_draft.pending()?TR("Check PC before retry if its database or BLE identity changed."):""; render(); return; }
  if (a==18 || a==19) { _folder=a==18?0:1; _contactPicker=false; go(Messages); return; }
  auto& client=guardian::v3::client();
  if (a==100) { go(Station); return; }
  if (a==101 || a==102) { selectRadio(a-100); return; }
  if (a==110) { go(Transfers); return; }
  if (a==111) { go(Controls); return; }
  if (a==112) { selectRadio(_radio); go(Tune); return; }
  if (a==113) { go(Profiles); return; }
  if (a==114) { go(AutoTune); return; }
  if (a==115) { go(CommandResult); return; }
  if (a>=120 && a<=123) {
    const auto* radio=client.radio(_radio);
    const bool vara=radio && radio->modem.vara.command_connected && radio->modem.vara.data_connected;
    control(a==120?(radio && radio->connected?"radio.disconnect":"radio.connect"):
      a==121?(vara?"modem.disconnect":"modem.connect"):
      a==122?(radio && radio->control.active?"control.stop":"control.start"):"beacon.send"); return;
  }
  if (a==130) {
    captureV3Fields(); uint64_t hz=0; bool valid=!_tuneHz.empty();
    for (char c:_tuneHz) { if(c<'0' || c>'9') {valid=false;break;} hz=hz*10+unsigned(c-'0'); if(hz>guardian::v3::MaxFrequency) {valid=false;break;} }
    const auto* cap=client.radioCapabilities(_radio);
    valid=valid && hz>0 && cap && std::find(cap->tune_modes.begin(),cap->tune_modes.end(),_tuneMode)!=cap->tune_modes.end();
    if (valid && cap->tune_mode=="frequency_only") valid=_tuneMode=="FM" && hz%10==0;
    if (!valid) { _info=TR("Enter a supported mode and frequency from 1 to 10000000000 Hz"); render(); return; }
    JsonDocument doc; doc["frequency_hz"]=hz; doc["mode"]=_tuneMode;
    control("radio.tune",guardian::jsonText(doc)); return;
  }
  if (a>=140 && a<160) {
    const auto* cap=client.radioCapabilities(_radio);
    if (cap && size_t(a-140)<cap->profiles.size()) { _profile=cap->profiles[a-140]; render(); } return;
  }
  if (a==131) { JsonDocument doc; doc["profile"]=_profile; control("profile.apply",guardian::jsonText(doc)); return; }
  if (a==132) { captureV3Fields(); JsonDocument doc; doc["peer"]=_calPeer; doc["mode"]=_calMode; control("calibration.start",guardian::jsonText(doc)); return; }
  if (a==133) { control("calibration.cancel"); return; }
  if (a==160) { client.refreshStation(); _info=TR("Loading..."); render(); return; }
  if (a==161 || a==162) {
    if (!client.intent.unresolved) return;
    _intentDecision=a==161?1:2; _confirmReturn=CommandResult; client.refreshStation(); go(Confirm); return;
  }
  if (a==163) {
    if (_intentDecision && !recoveryReason(_intentDecision==1,millis()).empty()) { _info=recoveryReason(_intentDecision==1,millis()); render(); return; }
    bool accepted=false;
    if (_intentDecision==1) accepted=client.retryIntent(millis());
    else if (_intentDecision==2) accepted=client.discardIntent(millis());
    else if (controlReason(_controlAction,millis()).empty()) accepted=client.execute(_radio,_controlAction,_controlFields,millis());
    if (accepted) { const Page page=_intentDecision==2?Controls:CommandResult; _intentDecision=0; go(page); }
    else { _info=client.error.empty()?TR("Command was not sent"):client.error; render(); }
    return;
  }
  if (a==164) { go(_confirmReturn); _intentDecision=0; return; }
  if (a==170) { _alertOffset=0; _alertPrevious.clear(); go(Alerts); return; }
  if ((a==171 || a==172) && client.historyPending()) return;
  if (a==171 && !_alertPrevious.empty()) { _alertOffset=_alertPrevious.back(); _alertPrevious.pop_back(); client.requestHistory(_alertOffset,client.history.revision); render(); return; }
  if (a==172 && client.history.next_offset.known) { _alertPrevious.push_back(_alertOffset); _alertOffset=client.history.next_offset.value; client.requestHistory(_alertOffset,client.history.revision); render(); return; }
  if (a==173) { _alertOffset=0; _alertPrevious.clear(); client.requestHistory(); render(); return; }
  if (a==174) { if (client.seen(_alert.alert_id)) { _alert.seen=true; _info.clear(); } else _info=TR("Cannot save Guardian state"); render(); return; }
  if (a>=180 && size_t(a-180)<client.history.items.size()) { _alert=client.history.items[a-180]; go(AlertDetail); return; }
  if (_awaiting && !_background) return;
  if (a==5) send();
  else if (a==6 && !_previous.empty()) {
    if (_awaiting) return;
    _offset=_previous.back(); _previous.pop_back(); _pageLoaded=false; requestPage(false,false,true);
  } else if (a==7 && _hasNext && !_awaiting) requestPage(true);
  else if (a==8 && _draft.pending()) {
    if (!_confirmNew) { _confirmNew=true; _info=TR("The previous message may be queued on PC. Check it before starting a new message."); }
    else {
      guardian::Draft empty;
      if (guardian::saveDraft(empty)) { _draft=empty; _confirmNew=false; _info.clear(); }
      else _info=TR("Cannot save draft. Nothing sent.");
    }
    render();
  } else if (a>=20 && size_t(a-20)<_rows.size()) {
    const Row row=_rows[a-20];
    if (_page==Contacts) { _contactPicker=false; if (!_draft.pending()) _draft.to=row.call; go(Compose); }
    else { _message=row.id; _heading=row.title; go(Text); }
  }
}
bool GuardianAppScreen::belongs(lv_obj_t* object, lv_obj_t* root) {
  if (!root) return false;
  while (object) { if (object==root) return true; object=lv_obj_get_parent(object); }
  return false;
}
void GuardianAppScreen::clicked(lv_event_t* event) {
  const Binding binding=*static_cast<Binding*>(lv_event_get_user_data(event));
  if (belongs(lv_event_get_target(event),binding.owner->_root.get())) binding.owner->action(binding.action);
}
void GuardianAppScreen::selection(lv_event_t* event) {
  auto* self=static_cast<GuardianAppScreen*>(lv_event_get_user_data(event));
  if (!belongs(lv_event_get_target(event),self->_root.get())) return;
  const unsigned selected=lv_dropdown_get_selected(lv_event_get_target(event));
  if (self->_page==Messages) self->_folder=selected; else self->_source=selected;
  self->go(self->_page);
}
} }

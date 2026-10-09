// SPDX-License-Identifier: GPL-3.0-or-later
#include "GuardianAppScreen.h"
#include "../services/GuardianLink.h"
#include "../services/GuardianRpcLink.h"
#include "../services/GuardianJson.h"
#include "../theme/Theme.h"
#include "../theme/Fonts.h"
#include "../widgets/Styles.h"
#include "../widgets/GuardianIcon.h"
#include "../i18n.h"
#include <cstdio>
#include <cstring>
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
lv_obj_t* GuardianAppScreen::button(const char* title, int x, int y, int w, int action, int h, bool left) {
  if (_bindingCount>=24) return nullptr;
  auto* b=lv_btn_create(_root.get()); style(b);
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
  if (_root.get()) { captureDraft(); if (_loaded) guardian::saveDraft(_draft); }
  if (_hideKeyboard && _root.get()) _hideKeyboard();
  ++_epoch; _root.set(nullptr); _notice=nullptr;
  for (auto*& f:_fields) f=nullptr;
}
void GuardianAppScreen::go(Page page) {
  captureDraft();
  if (_page==Compose) guardian::saveDraft(_draft);
  ++_epoch; _page=page; _info.clear(); _signature.clear(); _lastPoll=millis();
  _offset=0; _previous.clear(); _hasNext=false; _rows.clear(); _body.clear(); _pageLoaded=_cached=false;
  _needPage=page==Messages || page==Contacts || page==Text;
  if (_needPage) restoreCache();
  render();
  if (_needPage && !_awaiting) requestPage();
}
void GuardianAppScreen::render() {
  if (!_root.get()) return;
  if (_hideKeyboard) _hideKeyboard();
  lv_obj_clean(_root.get()); _bindingCount=0; _notice=nullptr;
  _activity=_percent=_bar=_inbox=_unread=_outbox=_arrowRx=_arrowTx=nullptr;
  for (auto*& f:_fields) f=nullptr;
  const char* title=_page==Dashboard?"Guardian":_page==Messages?TR("Messages"):
    _page==Contacts?TR("Network"):_page==Compose?TR("New message"):
    _page==Settings?TR("Guardian appearance"):_page==Text?TR("Message"):"Guardian";
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
  if (_page==Dashboard) {
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
    const int footer=32, row=(usable-footer-3*gap)/3;
    const char* titles[]={TR("Messages"),TR("Network"),TR("Write")};
    const Icon kinds[]={Icon::Mail,Icon::Network,Icon::Pencil};
    for(int i=0;i<3;++i) {
      auto* b=button(titles[i],left+gap,i*(row+gap),right,i+1,row);
      if(i==2) style(b,true);
      auto* text=lv_obj_get_child(b,0); lv_obj_set_width(text,right-46);
      if(lv_txt_get_width(titles[i],strlen(titles[i]),&theme::font14(),0,LV_TEXT_FLAG_NONE)>right-46)
        lv_obj_set_style_text_font(text,&theme::font12(),0);
      lv_obj_align(text,LV_ALIGN_LEFT_MID,34,0); lv_obj_set_style_text_align(text,LV_TEXT_ALIGN_LEFT,0);
      icon(b,kinds[i],8,(row-23)/2,23,i==2?0x1b1b1b:accent());
      icon(b,Icon::Chevron,right-14,(row-12)/2,12,i==2?0x1b1b1b:accent());
    }
    const int halfButton=(right-gap)/2, footY=usable-footer;
    iconButton(LV_SYMBOL_SETTINGS,Icon::Gear,left+gap,footY,halfButton,footer,9,false);
    iconButton(LV_SYMBOL_HOME,Icon::Home,left+gap+halfButton+gap,footY,right-halfButton-gap,footer,10,false);
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
  return _root.get() && _page!=Compose && _page!=Info;
}
void GuardianAppScreen::refreshKey(bool down,uint32_t now) {
  if (!acceptsRefreshKey() || !down) { _rDown=_rFired=false; return; }
  if (!_rDown) { _rAt=now; _rDown=true; }
  if (!_rFired && uint32_t(now-_rAt)>=2000) { _rFired=true; manualRefresh(); }
}
void GuardianAppScreen::manualRefresh() {
  if (!acceptsRefreshKey() || _refreshing) return;
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
  if (_awaiting || !guardian::rpcStart(json,millis())) return false;
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
  if (!guardian::rpcReady()) { _info=TR("Waiting for Guardian BLE"); render(); return; }
  if (!_draft.pending()) _draft.token=guardian::newToken();
  if (!guardian::saveDraft(_draft)) { _info=TR("Cannot save draft. Nothing sent."); render(); return; }
  JsonDocument doc; _id="gm"+std::to_string(++_serial); doc["id"]=_id; doc["op"]="message.queue"; doc["token"]=_draft.token;
  doc["to"]=_draft.to; doc["subject"]=_draft.subject; doc["body"]=_draft.body; doc["priority"]=_draft.priority;
  request("message.queue",guardian::jsonText(doc)); _info=TR("Sending..."); render();
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
  const auto& session=state.session.connected && _queriedStatus.fresh(now) &&
    (!state.session.fresh(now) || int32_t(_queriedStatus.receivedAt-state.session.receivedAt)>0)?_queriedStatus:state.session;
  const bool online=session.fresh(now);
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
}
void GuardianAppScreen::refresh(uint32_t now) {
  std::string raw,error;
  if (guardian::rpcTake(raw,error)) response(raw,error);
  if (!_root.get() || (_last && uint32_t(now-_last)<250)) return;
  _last=now;
  const auto state=guardian::snapshot(now); const bool online=state.session.fresh(now);
  if (!state.session.connected) _queriedStatus.disconnect();
  if (_online && !online) {
    if (_page==Messages || _page==Contacts || _page==Text) { _info=TR("Guardian offline"); _needPage=true; render(); }
  }
  _online=online;
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
  // Passive BLE Status/Progress already updates the dashboard. Only the visible
  // lists poll RPC; composing and reading text never rebuild under the user.
  if (online && guardian::rpcReady() && !_awaiting) {
    if (_needPage) requestPage(false,_pageLoaded,_offset!=0);
    else if ((_page==Messages || _page==Contacts) && uint32_t(now-_lastPoll)>=5000)
      requestPage(false,true,true);
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

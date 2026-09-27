// SPDX-License-Identifier: GPL-3.0-or-later
#include "SimPlatform.h"
#include "screens/TerminalScreen.h"
#include "screens/FullscreenToolView.h"
#include "services/TerminalSession.h"
#include <cstring>
#include <string>
#include <stdexcept>

namespace {
void require(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
lv_obj_t* findLabel(lv_obj_t* object, const char* text) {
  if (lv_obj_check_type(object, &lv_label_class) && !strcmp(lv_label_get_text(object), text)) return object;
  for (uint32_t i=0; i<lv_obj_get_child_cnt(object); ++i)
    if (auto* found=findLabel(lv_obj_get_child(object,i),text)) return found;
  return nullptr;
}
int sends=0, commands=0, releases=0, homes=0;
bool channelChanged=false, sink=false;
lv_obj_t* bound=nullptr;
std::string sent, recipient, executed;
void closeAsync(lv_obj_t** root) { lv_obj_del_async(*root); *root=nullptr; }
void ignoreLog(uint32_t, const char*, const char*) {}
}

void runTerminalRegression(void (*pump)(unsigned)) {
  using ui::TerminalSession;
  sends=commands=releases=homes=0; channelChanged=false;
  TerminalSession session({
    []{ return 1; },
    [](uint32_t index, TerminalSession::Contact& contact) {
      if (index) return false;
      strcpy(contact.name,"Alice"); memset(contact.key,7,sizeof contact.key); return true;
    },
    [](int slot, TerminalSession::Channel& channel) {
      if (slot!=0) return false;
      strcpy(channel.name,channelChanged?"Replacement":"Public"); return true;
    }, 2,
    [](bool channel,const uint8_t* key,int16_t slot,const char* name,const char* text) {
      require(channel ? slot==0 : key && key[0]==7,"Terminal recipient identity lost");
      ++sends; sent=text; recipient=name;
    }, ignoreLog
  });
  require(!session.terminalRunChatCommand(nullptr),"Null terminal command accepted");
  require(!session.terminalRunChatCommand("status"),"CLI command swallowed");
  require(session.terminalRunChatCommand("send hello") && sends==0,"Send without recipient");
  require(session.terminalRunChatCommand("  to ali"),"Contact prefix not resolved");
  require(session.terminalRunChatCommand("hello") && sends==1 && recipient=="Alice","Room text not routed");
  require(!session.terminalRunChatCommand("SET name test") && sends==1,"CLI in room transmitted");
  require(session.terminalRunChatCommand("send status") && sent=="status","Explicit keyword text not sent");
  session.terminalRunChatCommand("exit");
  require(!session.terminalRunChatCommand("hello"),"Leaving room retained recipient");
  session.terminalRunChatCommand("to pub");
  session.terminalRunChatCommand("channel text");
  require(recipient=="Public" && sends==3,"Channel selection failed");
  channelChanged=true;
  session.terminalRunChatCommand("wrong channel");
  require(sends==3,"Replaced channel slot received old conversation");
  session.terminalRunChatCommand("public direct");
  require(sends==4 && recipient=="Replacement","Explicit public command used stale channel");

  const auto roots=lv_obj_get_child_cnt(lv_layer_top());
  ui::screens::TerminalScreen screen({
    []{ return false; }, []{ return 24; }, closeAsync,
    [](lv_obj_t* input){ bound=input; }, []{ bound=nullptr; }, []{}, [](lv_obj_t*){},
    [](bool on){ sink=on; }, [](const char* cmd){ ++commands; executed=cmd; }, ignoreLog
  });
  auto* page=lv_obj_create(lv_layer_top());
  screen.buildTerminal(page);
  require(screen.active() && bound==screen.input() && sink,"Terminal did not attach input/sink");
  lv_textarea_set_text(screen.input(),"status"); screen.terminalSubmit();
  require(commands==1 && executed=="status" && !lv_textarea_get_text(screen.input())[0],"Terminal submit failed");
  screen.openTermCmdPicker();
  auto* oldChoice=findLabel(lv_layer_top(),"set name <new>");
  require(oldChoice!=nullptr,"Terminal picker row missing");
  auto* oldButton=lv_obj_get_parent(oldChoice);
  screen.closeTermCmdPicker(); screen.openTermCmdPicker();
  lv_event_send(oldButton,LV_EVENT_CLICKED,nullptr);
  require(screen.pickerOpen() && !lv_textarea_get_text(screen.input())[0],"Old picker changed current input");
  pump(2);
  auto* choice=findLabel(lv_layer_top(),"set name <new>");
  lv_event_send(lv_obj_get_parent(choice),LV_EVENT_CLICKED,nullptr);
  require(!screen.pickerOpen() && !strcmp(lv_textarea_get_text(screen.input()),"set name "),"Picker template lost");
  pump(2);
  screen.openTermCmdPicker();
  lv_obj_del(page);
  require(!screen.active() && !screen.input() && !screen.pickerOpen() && !sink && !bound,"External deletion left terminal pointers");
  pump(2);
  page=lv_obj_create(lv_layer_top()); screen.buildTerminal(page);
  screen.close(); screen.close();
  require(!sink && !bound && lv_obj_get_child_cnt(page)==0,"Repeated terminal close leaked contents");
  lv_obj_del(page); pump(2);

  ui::screens::FullscreenToolView shell({[]{return 24;},closeAsync,[]{++releases;},[]{},[]{++homes;}});
  char title[]="Files"; shell.open(title); title[0]='X';
  require(!strcmp(shell.title(),"Files"),"Tool shell borrowed title storage");
  auto* oldHome=lv_obj_get_parent(findLabel(shell.root(),LV_SYMBOL_HOME));
  shell.open("Console");
  lv_event_send(oldHome,LV_EVENT_CLICKED,nullptr);
  require(shell.root() && !strcmp(shell.title(),"Console") && homes==0,"Old Home closed new shell");
  pump(2); require(releases==1,"Shell replacement cleanup count");
  lv_obj_del(shell.root());
  require(!shell.root() && !shell.title()[0] && releases==2,"External shell deletion did not release contents");
  shell.open("Files"); shell.close(); pump(2);
  require(releases==3 && lv_obj_get_child_cnt(lv_layer_top())==roots,"Tool lifecycle leaked roots");
}

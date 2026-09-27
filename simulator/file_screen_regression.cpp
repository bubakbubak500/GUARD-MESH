// SPDX-License-Identifier: GPL-3.0-or-later
#include "SimPlatform.h"
#include "screens/FileManagerScreen.h"
#include "services/FileOperations.h"
#include "i18n.h"
#include <cstring>
#include <stdexcept>

namespace {
fs::FS disk;
int confirmed=0, cancelled=0;
int soundSlot=0, soundSlotReads=0, soundSettingsOpened=0;
void (*deleteAction)()=nullptr;
void check(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
lv_obj_t* label(lv_obj_t* object,const char* text,bool prefix=false) {
  if (lv_obj_check_type(object,&lv_label_class)) {
    const char* actual=lv_label_get_text(object);
    if (prefix ? !strncmp(actual,text,strlen(text)) : !strcmp(actual,text)) return object;
  }
  for(uint32_t i=0;i<lv_obj_get_child_cnt(object);++i)
    if(auto* found=label(lv_obj_get_child(object,i),text,prefix)) return found;
  return nullptr;
}
lv_obj_t* button(lv_obj_t* root,const char* text,bool prefix=false) {
  auto* result=label(root,text,prefix); check(result,"File screen control missing");
  return lv_obj_get_parent(result);
}
void click(lv_obj_t* root,const char* text,bool prefix=false) { lv_event_send(button(root,text,prefix),LV_EVENT_CLICKED,nullptr); }
lv_obj_t* overlayFor(lv_obj_t* object) {
  while(lv_obj_get_parent(object)!=lv_layer_top()) object=lv_obj_get_parent(object);
  return object;
}
}
void runFileScreenRegression(void (*pump)(unsigned)) {
  namespace files=ui::screens::files;
  const auto roots=lv_obj_get_child_cnt(lv_layer_top());
  disk=fs::FS{}; disk.enableMemory(); disk.mkdir("/target");
  auto file=disk.open("/note.txt","w"); file.print("original"); file.close();
  // A valid 1x1, 24-bit BMP exercises the viewer's real decoder/buffer ownership.
  const uint8_t bmp[]={66,77,58,0,0,0,0,0,0,0,54,0,0,0,40,0,0,0,1,0,0,0,1,0,0,0,1,0,24,0,0,0,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,0};
  file=disk.open("/image.bmp","w"); file.write(bmp,sizeof bmp); file.close();
  ui::FileOperations ops({malloc,free,[]{}});
  files::Host host{};
  host.task=[]()->UITask*{return nullptr;}; host.keyboard=[]()->lv_obj_t*{return nullptr;}; host.statusbar=host.keyboard;
  host.statusHeight=[]{return 24;}; host.internalFilesystem=[]()->fs::FS*{return &disk;};
  host.internalUsage=[](uint64_t& used,uint64_t& total){used=8;total=4096;};
  host.isSd=[](fs::FS*){return false;}; host.isFlat=host.isSd; host.markSdIo=[]{};
  host.storageRows=[](files::StorageRow*,int){return 0;}; host.operations=&ops;
  host.kbMirrorSyncToReal=[]{}; host.hideKb=[]{};
  host.popupClose=[](lv_obj_t** root){if(*root)lv_obj_del_async(*root);*root=nullptr;};
  host.kbMirrorBind=[](lv_obj_t*){}; host.attachSettingsTaEvents=host.kbMirrorBind;
  host.showConfirm=[](const char*,const char*,void(*action)()){deleteAction=action;};
  host.closeFullscreenView=[]{files::close();}; host.fmSeedLockFolder=[]{}; host.home=[]{};
  host.bindEditor=[](lv_obj_t*){}; host.wallpaperChanged=[](const char*){};
  host.soundSlot=[]{++soundSlotReads; return soundSlot;}; host.openSoundSettings=[]{++soundSettingsOpened;}; host.cancelPrompt=[]{++cancelled;};
  files::configure(host);
  auto* page=lv_obj_create(lv_layer_top()); files::buildFileManager(page);
  files::fmOpenStorage(&disk,"Test","/");
  click(page,"note.txt",true);
  check(files::popupOpen(1) && !strcmp(lv_textarea_get_text(files::editorInput()),"original"),"Editor did not load file");
  auto* oldSave=button(lv_layer_top(),TR("Save"));
  files::fmEditorClose();
  click(page,"note.txt",true);
  lv_textarea_set_text(files::editorInput(),"edited");
  lv_event_send(oldSave,LV_EVENT_CLICKED,nullptr);
  check(files::popupOpen(1),"Stale editor saved/closed replacement");
  pump(2); click(lv_layer_top(),TR("Save")); pump(2);
  file=disk.open("/note.txt","r"); char text[16]={}; file.readBytes(text,sizeof text); file.close();
  check(!strcmp(text,"edited"),"File editor did not save through selected FS");
  disk.limitReads(1); click(page,"note.txt",true);
  check(!files::popupOpen(1),"Editor accepted a short read"); disk.allowReads();
  files::fmTextPrompt("First","old",[](const char*){++confirmed;});
  auto* oldOk=button(lv_layer_top(),"OK");
  files::fmTextPrompt("Second","new",[](const char*){++confirmed;});
  lv_event_send(oldOk,LV_EVENT_CLICKED,nullptr);
  check(confirmed==0 && files::popupOpen(2),"Stale prompt confirmed new operation");
  pump(2); click(lv_layer_top(),"OK"); pump(2);
  check(confirmed==1 && !files::popupOpen(2),"Prompt did not deliver once");
  click(page,"image.bmp",true); check(files::popupOpen(0),"Image viewer did not decode BMP");
  files::fmImageClose(); click(page,"image.bmp",true); pump(2);
  check(files::popupOpen(0),"Old viewer deletion cleared replacement");
  auto* imageRoot=overlayFor(label(lv_layer_top(),"image.bmp   1x1"));
  lv_obj_del(imageRoot); check(!files::popupOpen(0),"External image deletion kept stale root");
  auto* entry=button(page,"note.txt",true);
  lv_event_send(entry,LV_EVENT_LONG_PRESSED,nullptr); click(lv_layer_top(),TR("Copy")); pump(2);
  files::fmOpenStorage(&disk,"Test","/target");
  click(page,LV_SYMBOL_PLUS); click(lv_layer_top(),TR("Paste (copy)"));
  check(files::popupOpen(5),"Paste did not show pending notice");
  lv_obj_del(page); pump(2);
  check(!files::active() && !files::takePendingPaste() && !disk.exists("/target/note.txt"),"Closed browser retained pending copy");
  for(unsigned i=0;i<6;++i) check(!files::popupOpen(i),"File browser leaked popup");
  page=lv_obj_create(lv_layer_top()); files::buildFileManager(page); files::fmOpenStorage(&disk,"Test","/");
  lv_event_send(button(page,"note.txt",true),LV_EVENT_LONG_PRESSED,nullptr);
  click(lv_layer_top(),TR("Delete")); check(deleteAction,"Delete did not request confirmation");
  lv_obj_del(page); pump(2);
  page=lv_obj_create(lv_layer_top()); files::buildFileManager(page); files::fmOpenStorage(&disk,"Test","/");
  deleteAction(); check(disk.exists("/note.txt"),"Old confirmation survived browser close");
  lv_event_send(button(page,"note.txt",true),LV_EVENT_LONG_PRESSED,nullptr);
  click(lv_layer_top(),TR("Delete")); deleteAction();
  check(!disk.exists("/note.txt"),"Confirmed delete did not apply snapshot");
  lv_obj_del(page); pump(2);
  // The WAV dialog commits to the slot named when the file opened, even if the
  // caller changes its selection before confirmation. Exercise the real parser.
  const uint8_t wav[]={82,73,70,70,38,0,0,0,87,65,86,69,102,109,116,32,16,0,0,0,
                       1,0,1,0,64,31,0,0,128,62,0,0,2,0,16,0,100,97,116,97,2,0,0,0,0,0};
  file=disk.open("/sample.wav","w"); file.write(wav,sizeof wav); file.close();
  char previousSounds[3][TOUCH_SOUND_PATH_MAXLEN]{};
  for(int i=0;i<3;++i) touchPrefsGetSoundFile(i,previousSounds[i],sizeof previousSounds[i]);
  const auto previousSpiffs=SPIFFS; SPIFFS=disk;
  page=lv_obj_create(lv_layer_top()); files::buildFileManager(page); files::fmOpenStorage(&disk,"Test","/");
  soundSlot=1; soundSlotReads=0; click(page,"sample.wav",true);
  check(soundSlotReads==1 && label(lv_layer_top(),"Target: Direct (DM) sound"),"WAV target was not captured once");
  auto* staleSet=button(lv_layer_top(),TR(LV_SYMBOL_OK "  Set as notification sound"));
  files::fmSndClose(); soundSlot=2; click(page,"sample.wav",true);
  lv_event_send(staleSet,LV_EVENT_CLICKED,nullptr);
  check(files::popupOpen(4) && !soundSettingsOpened,"Stale WAV dialog applied replacement");
  pump(2); soundSlot=0;
  click(lv_layer_top(),TR(LV_SYMBOL_OK "  Set as notification sound")); pump(2);
  char storedSound[TOUCH_SOUND_PATH_MAXLEN]; touchPrefsGetSoundFile(2,storedSound,sizeof storedSound);
  check(!strcmp(storedSound,"/sample.wav") && soundSettingsOpened==1 && soundSlotReads==2,
        "WAV confirmation used current slot instead of displayed target");
  touchPrefsGetSoundFile(0,storedSound,sizeof storedSound);
  check(!strcmp(storedSound,previousSounds[0]),"WAV confirmation overwrote another notification slot");
  lv_obj_del(page); pump(2); SPIFFS=previousSpiffs;
  for(int i=0;i<3;++i) touchPrefsSetSoundFile(i,previousSounds[i]);
  check(lv_obj_get_child_cnt(lv_layer_top())==roots,"File browser leaked tree");
  files::close();
}

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
#include <stddef.h>
class UITask;
namespace fs { class FS; }
namespace ui { class FileOperations; namespace screens { namespace files {
constexpr size_t FM_IMG_MAX = 4u * 1024 * 1024;
struct StorageRow { char label[80]; bool available; lv_event_cb_t click, hold; lv_event_code_t event; };
struct Host {
  UITask* (*task)();
  lv_obj_t* (*keyboard)();
  lv_obj_t* (*statusbar)();
  int (*statusHeight)();
  fs::FS* (*internalFilesystem)();
  void (*internalUsage)(uint64_t&,uint64_t&);
  bool (*isFlat)(fs::FS*);
  bool (*isSd)(fs::FS*);
  void (*markSdIo)();
  int (*storageRows)(StorageRow*, int);
  FileOperations* operations;
  void (*kbMirrorSyncToReal)();
  void (*hideKb)();
  void (*popupClose)(lv_obj_t**);
  void (*kbMirrorBind)(lv_obj_t*);
  void (*attachSettingsTaEvents)(lv_obj_t*);
  void (*showConfirm)(const char*,const char*,void(*)());
  void (*closeFullscreenView)();
  void (*fmSeedLockFolder)();
  void (*home)();
  void (*bindEditor)(lv_obj_t*);
  void (*wallpaperChanged)(const char*);
  int (*soundSlot)();
  void (*openSoundSettings)();
  void (*cancelPrompt)();
};
// One file browser per device. UI state and callback ownership are private;
// platform code provides storage rows and mount/format commands through Host.
void configure(Host);
void close();
bool active();
fs::FS* currentFilesystem();
lv_obj_t* editorInput();
bool popupOpen(unsigned index); // image, editor, prompt, actions, sound, busy
bool takePendingPaste();
void fmOpenStorage(fs::FS*,const char*,const char*);
void fmRefresh();
void fmShowRoots();
void fmFmtSize64(uint64_t,char*,size_t);
void fmShowBusyOverlay(const char*);
void fmHideFormatOverlay();
void fmPromptClose();
void fmTextPrompt(const char*,const char*,void(*)(const char*));
bool fmDoPaste();
void fmCloseActions();
void fmEditorClose();
void fmImageClose();
void fmSndClose();
void buildFileManager(lv_obj_t*);
} } }

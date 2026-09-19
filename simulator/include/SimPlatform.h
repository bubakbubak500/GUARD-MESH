// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Host-only replacement for the hardware includes at the top of UITask.cpp.
// Screen construction, callbacks and the UITask lifecycle stay in the firmware source.
#include "SimTypes.h"
#include "SimHardware.h"
#include "SimDisplay.h"
#include "SimPrefs.h"
#include <helpers/ContactInfo.h>
#include <helpers/ChannelDetails.h>
#include <helpers/AdvertDataHelpers.h>
#include <helpers/sensors/LPPDataHelpers.h>
#include "helpers/esp32/TouchPrefsStore.h"
#include "helpers/input/HeltecV4CapTouch.h"
#include "helpers/input/TDeckKeyboard.h"
#include "helpers/input/TDeckTrackball.h"
#include "SimMesh.h"
#include "UITask.h"
#include "device_caps.h"
#include "lvgl.h"
#include "TouchSleep.h"
#include "SnakeGame.h"
#include "LuaHost.h"
#include "LuaAppHost.h"
#include "AppPage.h"
#include "ReaderContent.h"
#include "ChannelSenderSplit.h"
#include "PasteHexKey.h"
#include "SdThreat.h"
#include "UsbFilesSession.h"
#include "SenderExtField.h"
#include "BleKeyboard.h"
#include "KeyboardLayouts.h"
#include "i18n.h"
#include "emoji_data.h"
#include "qr_icon.h"
#include "LvglPsramAlloc.h"
#include <cerrno>
#include <ctime>
#define WEB_UI_PORT_STR "8765"
#define FIRMWARE_VERSION "GUARD-MESH simulator"
#define FIRMWARE_BUILD_DATE "desktop"
#include "assets/lockscreen_placeholder_jpg.h"
#include "assets/lockscreen_wallpaper_rgb565.h"
inline int tileFetchPendingLoad() {
  return 0;
}
inline bool tileFetchWorkerActive() {
  return false;
}
inline bool tileBackendSwapRequested() {
  return false;
}
inline bool tileBackendSwapTryBegin() {
  return true;
}
inline void tileBackendSwapFinish() {}
inline bool ensureHistFlushTaskRunning() {
  return false;
}
inline bool mapSdStorageMounted() {
  return false;
}
inline bool mapSdStorageReady() {
  return false;
}
inline fs::FS *mapSdStorage() {
  return &SD;
}
inline fs::FS *s_tile_fs = nullptr;
inline fs::FS *s_tile_fs_default = nullptr;
inline char s_tile_root[64] = {0}, s_tile_root_default[64] = {0};
inline bool s_tiles_fs_ready = false, s_los_busy = false;
inline constexpr int k_tile_fetch_dedup_size = 48;
inline uint32_t s_tile_fetch_dedup[48] = {0};
inline int s_tile_fetch_dedup_head = 0;
inline void blePinSaveCb(lv_event_t *) {}
inline void touchDiagTraceRegister(void (*)(const char *)) {}

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(GUARD_SIMULATOR)
#include "SimPlatform.h"
#else
#include "../UITask.h"
#include "../TouchSleep.h"

#include "../../MyMesh.h"

#include "../device_caps.h"   // CAP_* capability flags (replaces device-name #ifs)

// Port the browser-facing web UI (VNC mirror / remote / terminal viewer page) listens on.
// The T-Display P4's ESP-AT stack allows ONE listening port, so the web UI shares the
// companion TCP port behind a first-byte router (MultiTransportCompanionInterface).
#if defined(HAS_TDISPLAY_P4)
  #define WEB_UI_PORT_STR "5000"
#else
  #define WEB_UI_PORT_STR "8765"
#endif

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cerrno>   // chat-store write diagnostics surface errno (ENFILE vs ENOSPC vs EIO)

#if defined(ESP32)
  #include <time.h>
  #include <SPIFFS.h>
  // Dedicated LittleFS instance for the map tile pack (separate
  // partition — see variants/heltec_v4/partitions_tft_touch.csv). Keeps
  // tiles out of the SPIFFS partition where /new_prefs + /contacts3 +
  // chat history live, so refreshing the tile pack no longer wipes the
  // operator's Profile / Radio settings.
  #include <LittleFS.h>
  #include <esp_heap_caps.h>
  #include <esp_system.h>
  #include <esp_sleep.h>   // esp_deep_sleep_start / ext0 wakeup for the power-off menu
  #include <driver/rtc_io.h>   // rtc_gpio_pullup_en — hold the wake pin's level in deep sleep

  #if CAP_LOCK_SCREEN

    #if defined(TLORA_PAGER)

    #endif
  #endif
  #include <esp_timer.h>
  #include <esp_chip_info.h>
  #include <nvs.h>            // nvs_get_stats() for the About-tab NVS usage line
  #include <Esp.h>
  #include <esp_ota_ops.h>     // A/B slot info + reboot-to-recovery (esp_ota_get_running_partition)
  #include <esp_partition.h>   // find/erase otadata to fall back to the factory(recovery) slot
  #if !defined(HAS_TANMATSU)
  #include <esp_spi_flash.h>   // spi_flash_cache2phys — verify the OTA "running" slot matches reality (beta_21 coredump)
  #endif
  #if !defined(HAS_TANMATSU)
  #include <esp_core_dump.h>   // detect/read/erase the panic coredump for the crash-report export
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>   // xTaskGetCurrentTaskHandleForCPU / pcTaskGetName — Task-WDT crash self-record
  #include <freertos/queue.h>
  #else
  // Tanmatsu's 16M.csv has no coredump partition yet — stub so the crash-export compiles + links.
  #include <esp_err.h>
  static inline esp_err_t esp_core_dump_image_check() { return ESP_FAIL; }
  static inline esp_err_t esp_core_dump_image_get(size_t* a, size_t* s) { (void)a; (void)s; return ESP_FAIL; }
  static inline esp_err_t esp_core_dump_image_erase() { return ESP_FAIL; }
  #endif
#endif
#if defined(HAS_TDECK_GT911) || defined(HAS_TDECK_PRO) || defined(HAS_THINKNODE_M9) || defined(HELTEC_LORA_V4_R8)
  #include <SD.h>             // microSD — T-Deck/M9 on the LoRa SPI, V4-R8 on the TFT SPI
  #include "SdFastClock.h"    // post-mount operating-clock raise (SD_SPI_FAST_HZ boards)
  #include "sd_diskio.h"      // internal Arduino-SD drive helpers (sdcard_init / sd_*_raw)
  extern SPIClass* tdeckSharedSPI();
  // FatFs mkfs (the prebuilt ESP-IDF compiles f_setlabel OUT — FF_USE_LABEL=0 —
  // so the "MESHCOMOD" volume label is written by hand via sd_*_raw, below).
  extern "C" int f_mkfs(const char* path, uint8_t opt, uint32_t au, void* work, unsigned len);
  #ifndef MC_FM_FAT32
    #define MC_FM_FAT32 0x02  // FatFs f_mkfs option: force FAT32
  #endif
  #ifndef PIN_SD_CS
    #define PIN_SD_CS 39      // T-Deck microSD chip-select (M9 sets its own via build flags)
  #endif
#endif
#if defined(HAS_THINKNODE_M9)
  extern SPIClass* m9SharedSPI();
#endif
#if defined(HAS_TDECK_GT911)
  #include <driver/i2s.h>     // T-Deck MAX98357A speaker amp (notification tones)
  // T-Deck I2S audio amp pins (MAX98357A, no MCLK). Overridable via build flags.
  #ifndef PIN_I2S_BCK
    #define PIN_I2S_BCK  7
  #endif
  #ifndef PIN_I2S_WS
    #define PIN_I2S_WS   5
  #endif
  #ifndef PIN_I2S_DOUT
    #define PIN_I2S_DOUT 6
  #endif
#elif defined(TLORA_PAGER)
  #include <SD.h>             // microSD (CS=21) on the shared radio/display SPI bus --
                               // storage + file manager/WAV access. No sd_diskio.h/f_mkfs
                               // here: formatting is deliberately off on this board (the
                               // reasons are on the format-helper guard in the file manager).
  #define PIN_SD_CS PAGER_PIN_SD_CS   // from TLoraPagerBoard.h, already visible via
                                      // MyMesh.h -> target.h -> TLoraPagerBoard.h above
  #include <driver/i2s.h>     // pager ES8311 codec (notification tones + WAV playback)
  #include "Es8311Codec.h"    // PIN_I2S_MCLK/BCK/WS/DOUT/SDIN come from platformio.ini build flags
#elif defined(HAS_TANMATSU) || defined(HAS_TDISPLAY_P4) || defined(HAS_WIO_TRACKER_L2)
  #include <FFat.h>            // internal FAT partition (Tanmatsu 'locfd' / P4 'storage')
  #include <SD_MMC.h>          // microSD on the P4-class boards' SDMMC slot 0; slot 1 = C6 radio
  extern bool g_fs_ok;         // set in main.cpp once the internal FAT is mounted
#endif
#include <Utils.h>
#include <LvglPsramAlloc.h>   // PSRAM-preferred alloc helpers for the map tile cache
#include "../SnakeGame.h"        // Apps → Snake (self-contained game module)
#include "../LuaHost.h"          // Lua app host (LUA_APPS.md; Phase 0 spike gate)
#include "../LuaAppHost.h"       // sandboxed Lua apps (LUA_APPS.md Phase 1; self-gated on CAP_LUA_APPS)

#include "../AppPage.h"          // shared full-screen app-page chrome (both of the above use it)
#include "../ChannelSenderSplit.h"  // host-tested "SenderName: body" split for channel/room posts
#include "../PasteHexKey.h"        // host-tested key extraction for pasted key fields (#526)
#include "../SdThreat.h"           // SD-card malware rules: the mount-time warning + wada.sd.remove
#include "../SenderExtField.h"   // host-tested split/rejoin of a sender across the two on-disk fields
// The split refuses a "SenderName: " prefix wider than the wire can carry, so that cap
// must never sit BELOW what UIMessage::sender can hold — otherwise a name the field
// stores fine gets rejected as implausible and the author lands back in the body.
static_assert(ChannelSenderSplit::kMaxWireName >= (size_t)UITask::MAX_SENDER_NAME,
              "the split would reject a name UIMessage::sender can hold");

#if defined(HAS_TOUCH_UI)
  #include <lvgl.h>
  #include "../../helpers/input/HeltecV4CapTouch.h"
  #if CAP_TRACKBALL
    #include <helpers/input/TDeckTrackball.h>
  #endif
  #if defined(HAS_TDECK_KEYBOARD)
    #include "../../helpers/input/TDeckKeyboard.h"
  #elif defined(HAS_M9_KEYBOARD)
    #include <M9Keyboard.h>
  #endif
  #include "../BleKeyboard.h"   // external Bluetooth keyboard (self-gated on CAP_BLE_KEYBOARD)
  #if CAP_BLE_KEYBOARD
    #include "../../helpers/esp32/MultiTransportCompanionInterface.h"
  #endif
  #if defined(HAS_M9_COMPASS)
    #include <M9Compass.h>           // wada.sys.compass() source (luaHostCompass)
  #endif
  #if defined(HAS_M9_IMU)
    #include <M9Imu.h>               // wada.sys.accel() source (luaHostAccel)
  #endif
  #if defined(HAS_PAGER_KEYBOARD)
    #include "../../helpers/input/PagerKeyboard.h"
  #endif
  #if defined(HAS_PAGER_ENCODER)
    #include <helpers/input/PagerEncoder.h>
  #endif
  #if defined(HAS_ATTAKY_MESH_KEYBOARD)
    #include <AttakyMeshSeriesKeyboard.h>
  #endif
  #if defined(ATTAKY_MESH_SERIES)
    #include <AttakyMeshSeriesKeys.h>
  #endif
  #include "../KeyboardLayouts.h"
  #include "../i18n.h"
  #include "../emoji_data.h"     // baked Noto colour-emoji glyphs (emojiGlyphLookup)
  #include "../qr_icon.h"        // baked recolour-able QR glyph (qr_icon_dsc) for the Chats Share button
  #if defined(HAS_TANMATSU)
    #include <TanmatsuDisplay.h>             // badge-bsp-backed DisplayDriver (P4)
  #elif defined(HAS_TDECK_PRO)
    #include <TDeckProDisplay.h>
  #elif defined(TLORA_PAGER)
    #include <helpers/ui/ST7796LCDDisplay.h>
  #elif defined(HAS_WIO_TRACKER_L2)
    #include <WioTrackerL2Display.h>
    #include <WioTrackerL2Io.h>
  #elif defined(HAS_RAK_TAP_V2)
    #include <LGFXDisplay.h>                 // LovyanGFX FSPI on RAK Tap V2
  #elif defined(HAS_TDISPLAY_P4)
    #if defined(HAS_TDP4_LCD)
      #include <HI8561Display.h>             // HI8561 TFT-LCD (LCD SKU) on the T-Display P4
    #else
      #include <RM69A10Display.h>            // RM69A10 MIPI-DSI AMOLED (default SKU) on the T-Display P4
    #endif
  #else
    #include <helpers/ui/ST7789LCDDisplay.h>
  #endif
  #include <helpers/AdvertDataHelpers.h>
  #include <helpers/sensors/LPPDataHelpers.h>
  #include <helpers/TouchDiagTrace.h>
  #if defined(ESP32)
    #include <Preferences.h>
    #include <helpers/esp32/SdNvsPrefs.h>   // NVS-or-SD prefs backend (Launcher-safe)
    #include "helpers/esp32/WdtHeavyGuard.h" // shared ref-counted core-0 WDT suspend (history/backup saves + core saveContacts)
    // QUOTED on purpose: the vendored core lib ships a STALE copy of this header in
    // its include path, so an angle include picks that up and misses accessors we
    // add here (e.g. the signal-probe prefs). Quotes force the local src/ copy.
    #include "../../helpers/esp32/TouchPrefsStore.h"
    // A block-list slot narrower than a sender name stores a cut name that the full-width
    // RX check can never match again — the block shows in Settings and silently never
    // fires. TouchPrefsStore.h stated this invariant in prose; prose is what let it rot.
    static_assert(TOUCH_IGNORED_NAME_LEN >= UITask::MAX_SENDER_NAME + 1,
                  "block-list slot must hold a full sender name, or blocking silently stops working");
    #include "../../helpers/esp32/WebMirror.h"   // web UI mirror (framebuffer + pointer bridge)
    #include "../../helpers/esp32/WebFileTransferConfig.h"
    #if WADA_WEB_FILE_TRANSFER
      #include "../../helpers/esp32/WebFileTransfer.h"
      #include "../../helpers/esp32/WebFileTransferProtocol.h"
    #endif
    #include "../../helpers/ClockFloorRTC.h"
    extern ClockFloorRTC rtc_clock;   // the board clock (variants/*/target.cpp); its send-timestamp floor is seeded/persisted from here (#89)
    #if defined(MULTI_TRANSPORT_COMPANION)
      #include <WiFi.h>
      #include <HTTPClient.h>
      #include <WiFiClientSecure.h>   // on-device HTTPS for the Reader (text browser); setInsecure(), no cert store
      #if CAP_OTA
        #include <Update.h>   // Arduino OTA writer (native dual-OTA boards; Tanmatsu is IDF/AppFS, no OTA)
      #endif
      #include "../../helpers/esp32/WifiRuntimeStore.h"   // QUOTED: this tree's copy (wifiScan*Active), not the lib's stale one
      #include "helpers/esp32/MqttBridge.h"
      #if defined(HAS_TDISPLAY_P4)
        // T-Display P4: the C6 runs ESP-AT, so Arduino's real WiFi object must NEVER be driven (its
        // mode()/begin() re-init esp_hosted and panic). These facades rebind every WiFi.* below to the
        // c6_at AT-over-SDIO driver — scans/joins become real, status reads come from a cache — and
        // every WiFiClient/WiFiClientSecure to AT+CIP sockets (HTTPClient dispatches virtually, so
        // tiles/version-check/OTA/reader fetch through the C6; TLS runs ON the C6). MUST stay the
        // LAST includes of this block. P4-only: the headers only exist in the P4 build.
        #include <C6WifiShim.h>
        #include <C6Socket.h>
        #define WiFiClient       C6Client
        #define WiFiClientSecure C6ClientSecure
      #endif
    #endif
  #endif
  #if defined(HAS_TANMATSU)
    extern TanmatsuDisplay display;
  #elif defined(HAS_TDECK_PRO)
    extern TDeckProDisplay display;
  #elif defined(TLORA_PAGER)
    extern ST7796LCDDisplay display;
  #elif defined(HAS_WIO_TRACKER_L2)
    extern WioTrackerL2Display display;
  #elif defined(HAS_RAK_TAP_V2) || defined(HELTEC_LORA_V4_R8)
    extern LGFXDisplay display;
  #elif defined(HAS_TDISPLAY_P4)
    extern DISPLAY_CLASS display;            // RM69A10Display (AMOLED) or HI8561Display (LCD) — set in CMakeLists
  #else
    extern ST7789LCDDisplay display;
  #endif
#endif

#endif // GUARD_SIMULATOR platform boundary

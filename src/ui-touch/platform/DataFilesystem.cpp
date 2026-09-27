// SPDX-License-Identifier: GPL-3.0-or-later
#include "DataFilesystem.h"
#include "UiDevice.h"
#include "../services/HistoryFileStore.h"
#if defined(HAS_WIO_TRACKER_L2) || defined(TLORA_PAGER) || defined(HELTEC_LORA_V4_R8)
extern bool g_full_data_on_sd;
#endif
#if defined(HAS_TANMATSU) || defined(HAS_TDISPLAY_P4)
extern bool g_sd_ok;
#endif
namespace ui { namespace platform {
bool DataFilesystem::ready() {
  if (_filesystem != nullptr) return true;   // cache SUCCESS only — a failed resolve MUST stay retryable
#if defined(TLORA_PAGER)
  if (_bootFinalized) {
    // A boot-adopted card owns the active identity/profile. Never fall back to
    // another profile's internal history if that card becomes unavailable. If
    // history failed to resolve during begin(), also never attach it later in
    // this boot: the RAM ring was not loaded from that card, and a subsequent
    // flush could otherwise replace its existing segments with an empty ring.
    if (g_full_data_on_sd) {
      return false;
    }
    // Never adopt unseen card history after the boot loader has finished, but
    // keep retrying the already-loaded internal backend if its first mount was
    // transiently unavailable. This avoids a RAM-only history session without
    // risking a switch to a card whose history was never loaded.
    if (SPIFFS.begin(false)) {
      _filesystem = &SPIFFS;
      _root[0] = '\0';
      return true;
    }
    return false;
  }
#endif
#if defined(HAS_WIO_TRACKER_L2)
  // Follow the boot-time DataStore decision. Never attach a card later in the
  // same boot, because the in-memory ring was not loaded from that profile.
  if (g_full_data_on_sd && SD_MMC.cardType() != CARD_NONE) {
    SD_MMC.mkdir("/meshcomod");
    _filesystem = &SD_MMC;
    strncpy(_root, "/meshcomod", sizeof _root - 1);
    return true;
  }
  if (SPIFFS.begin(false)) { _filesystem = &SPIFFS; _root[0] = '\0'; return true; }
#elif defined(HAS_TANMATSU) || defined(HAS_TDISPLAY_P4)
  // Tanmatsu + T-Display P4: use each board's mounted internal data partition
  // (Tanmatsu FFat 'locfd', P4 LittleFS 'storage'), with SD_MMC as fallback.
  // #167: hot UI data (chat history) lives on the INTERNAL LittleFS -- SD write bursts
  // electrically disturb this board's AMOLED, and the P4's internal FAT layer is broken
  // (see the storage note in tdisplay_p4/main/main.cpp). SD = degraded fallback only;
  // tiles keep using the SD via their own selector.

  if (g_fs_ok) {
#if defined(HAS_TANMATSU)
    _filesystem = &FFat;
#else
    _filesystem = &LittleFS;
#endif
    _root[0] = '\0';
    return true;
  }

  if (g_sd_ok) {
    SD_MMC.mkdir("/meshcomod");
    _filesystem = &SD_MMC;
    strncpy(_root, "/meshcomod", sizeof _root - 1);
    return true;
  }
#elif defined(TLORA_PAGER)
  // Follow the boot loader's primary-profile decision. An established card may
  // legitimately predate the migration marker; falling back to SPIFFS history
  // in that case would expose and update another identity's conversations.
  if (g_full_data_on_sd) {
    if (!(_host.mount && _host.mount())) return false;
    SD.mkdir("/meshcomod");
    _filesystem = &SD;
    strncpy(_root, "/meshcomod", sizeof _root - 1);
    return true;
  }
  // The boot loader kept identity/preferences internal, either by user choice
  // or because reconciliation failed closed. History must follow that same
  // profile decision even when an inserted card carries a valid marker and
  // history from another device. Never use foreign SD history as a fallback.
  if (SPIFFS.begin(false)) {
    _filesystem = &SPIFFS;
    _root[0] = '\0';
    return true;
  }
  return false;
#elif defined(HAS_TDECK_GT911) || defined(HAS_TDECK_PRO)
  // T-Deck / T-Deck Pro: the SD card is the established persistent history store.
  if ((_host.adopt && _host.adopt()) || (_host.mount && _host.mount())) {
    SD.mkdir("/meshcomod");
    _filesystem = &SD;
    strncpy(_root, "/meshcomod", sizeof _root - 1);
    return true;
  }
  if (SPIFFS.begin(false)) { _filesystem = &SPIFFS; _root[0] = '\0'; return true; }
#elif defined(HAS_THINKNODE_M9)
  // ThinkNode M9: same Arduino-SD-on-shared-bus shape as the T-Deck (CAP_SD=1;
  // used to fall into the V4 SPIFFS #else, losing the SD-backed deep message
  // ring despite a mounted card).
  if ((_host.adopt && _host.adopt()) || (_host.mount && _host.mount())) {
    SD.mkdir("/meshcomod");
    _filesystem = &SD;
    strncpy(_root, "/meshcomod", sizeof _root - 1);
    return true;
  }
  if (SPIFFS.begin(false)) { _filesystem = &SPIFFS; _root[0] = '\0'; return true; }
#elif defined(HELTEC_LORA_V4_R8)
  // R8 (Expansion Kit V2, REMOVABLE card): follow the boot adoption decision,
  // never bare card presence — mirrors the pager's profile-keyed shape. When
  // the boot store adopted the card (g_full_data_on_sd: identity/prefs/
  // contacts live there), chat history + battery log + Lua app files follow;
  // otherwise everything stays on internal SPIFFS as before. NB an R8 that
  // adopted a card on an OLDER build carries a day-one /msgs snapshot on the
  // card — Settings > Storage > "Copy internal data to SD" refreshes it.
  if (g_full_data_on_sd) {
    if (!((_host.adopt && _host.adopt()) || (_host.mount && _host.mount()))) return false;
    SD.mkdir("/meshcomod");
    _filesystem = &SD;
    strncpy(_root, "/meshcomod", sizeof _root - 1);
    return true;
  }
  if (SPIFFS.begin(false) || SPIFFS.begin(true)) { _filesystem = &SPIFFS; _root[0] = '\0'; return true; }
#else
  // V4 (no SD): internal SPIFFS. Format-on-fail so a fresh / never-formatted
  // partition becomes usable — that's the V4 history-loss fix. Safe: only formats
  // an unmountable partition (contents were inaccessible anyway), and only when
  // begin(false) already failed.
  if (SPIFFS.begin(false) || SPIFFS.begin(true)) {
    _filesystem = &SPIFFS; _root[0] = '\0';
    return true;
  }
#endif
  // Not ready yet — do NOT cache the failure, so a later call can still resolve.
  return false;
}


bool DataFilesystem::isSd() {
  if (!ready()) return false;
#if defined(HAS_TANMATSU) || defined(HAS_TDISPLAY_P4) || defined(HAS_WIO_TRACKER_L2)
  return _filesystem == &SD_MMC;
#elif defined(HAS_TDECK_GT911) || defined(HAS_TDECK_PRO) || defined(TLORA_PAGER) || defined(HAS_THINKNODE_M9) || defined(HELTEC_LORA_V4_R8)
  return _filesystem == &SD;
#else
  return false;
#endif
}
File DataFilesystem::open(const char* name, const char* mode) {
#if defined(TDP4_POKE_TRACE)
  // #167 hunt: every ui-data open with its issuing core. Writes ([SDW]) were all correctly on
  // core 0 during a flashing manual flow -- so now READS are traced too ([SDR]): a read is the
  // same FATFS/SDMMC critical-section machinery, and UI flows read constantly from core 1.
  if (mode && mode[0] != 'r') printf("[SDW] %lu core%d uiData %s %s\n", (unsigned long)millis(), xPortGetCoreID(), mode, name);
  else                        printf("[SDR] %lu core%d uiData r %s\n", (unsigned long)millis(), xPortGetCoreID(), name);
#endif
  if (!ready()) return File();
  File f = ui::history::FileStore<fs::FS>(*_filesystem, _root).open(name, mode);
#if defined(HAS_TDECK_GT911) || defined(HAS_TDECK_PRO) || defined(TLORA_PAGER) || defined(HAS_THINKNODE_M9) || defined(HELTEC_LORA_V4_R8)
  // A failed WRITE open on the SD-backed history store is the wedge tell (reads
  // fail legitimately on first boot). Called from the loop task AND the core-0
  // history worker — sdNoteIoFailure is a volatile stamp, safe from both.
  if (!f && mode && mode[0] == 'w' && _filesystem == &SD) if (_host.failure) _host.failure();
#endif
  return f;
}
} }

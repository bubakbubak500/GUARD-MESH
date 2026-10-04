// SPDX-License-Identifier: GPL-3.0-or-later
#include "AudioService.h"
#include "../platform/StorageAccess.h"
#include "../platform/UiDevice.h"
#if defined(HAS_TANMATSU)
extern "C" {
#include "bsp/audio.h"
}
#endif
#if defined(HAS_TDISPLAY_P4)
#include <P4Audio.h>
#endif
#if CAP_LUA_AUDIO
static void* wadaMp3Scratch();
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_SCRATCH ((mp3dec_scratch_t*)wadaMp3Scratch())
#define MINIMP3_IMPLEMENTATION
#include "../../../lib/minimp3/minimp3.h"
#undef MINIMP3_IMPLEMENTATION
#undef MINIMP3_SCRATCH
#undef MINIMP3_NO_SIMD
#undef MINIMP3_ONLY_MP3
static void* s_wada_mp3_scratch = nullptr;
static void* wadaMp3Scratch() { return s_wada_mp3_scratch; }
#endif
namespace ui { namespace audio {
static Host host{};
void configure(Host value) { host = value; }
// ---- I2S notification sound (T-Deck MAX98357A amp / pager ES8311 codec) ----
// Synthesized beeps and small WAV playback for UI feedback (message arrived,
// etc), both driven over I2S. The legacy genericBuzzer (RTTTL on a digital
// pin) doesn't apply here — that's for boards with a piezo on a GPIO (the
// Heltec V4), not an I2S speaker. WAV parsing and per-slot dispatch state
// below are shared; T-Deck and the pager each get their own I2S install/
// tone/WAV functions, since the pager additionally drives an ES8311 codec
// over I2C that the T-Deck's plain MAX98357A DAC doesn't have.
#if CAP_AUDIO_STREAM
static uint32_t wavRd32(File& f){ uint8_t b[4]; if(f.read(b,4)!=4) return 0; return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24); }
static uint16_t wavRd16(File& f){ uint8_t b[2]; if(f.read(b,2)!=2) return 0; return (uint16_t)(b[0]|(b[1]<<8)); }
static bool wavParse(File& f, uint16_t* pch, uint32_t* prate, uint32_t* pdata) {
  char tag[4];
  if (f.read((uint8_t*)tag,4)!=4 || memcmp(tag,"RIFF",4)) return false;
  const uint32_t riff_size = wavRd32(f);
  const uint64_t riff_end = 8u + (uint64_t)riff_size;
  if (riff_size < 4 || riff_end > f.size() || riff_end > 0xFFFFFFFFu) return false;
  if (f.read((uint8_t*)tag,4)!=4 || memcmp(tag,"WAVE",4)) return false;
  uint16_t fmt=0, ch=0, bits=0; uint32_t rate=0, dlen=0; bool hf=false, hd=false;
  while ((uint64_t)f.position() + 8u <= riff_end) {
    if (f.read((uint8_t*)tag,4)!=4) break;
    uint32_t csz = wavRd32(f);
    if (!memcmp(tag,"fmt ",4)) {
      if (csz < 16) return false;
      fmt = wavRd16(f); ch = wavRd16(f); rate = wavRd32(f); wavRd32(f); wavRd16(f); bits = wavRd16(f);
      const uint64_t skip = (uint64_t)(csz - 16) + (csz & 1u);
      const uint64_t next = (uint64_t)f.position() + skip;
      if (next > riff_end || (skip && !f.seek((uint32_t)next))) return false;
      hf = true;
    } else if (!memcmp(tag,"data",4)) {
      if ((uint64_t)f.position() + csz > riff_end) return false;
      dlen = csz; hd = true; break;
    } else {
      const uint64_t skip = (uint64_t)csz + (csz & 1u);
      const uint64_t next = (uint64_t)f.position() + skip;
      if (next > riff_end || (skip && !f.seek((uint32_t)next))) return false;
    }
  }
  if (!hf || !hd || fmt != 1 || bits != 16 || (ch != 1 && ch != 2) || rate < 8000 || rate > 48000)
    return false;
  if (pch) *pch = ch; if (prate) *prate = rate; if (pdata) *pdata = dlen;
  return true;
}
#endif

#if CAP_LUA_AUDIO
static volatile bool s_lua_audio_active = false;
static volatile uint32_t s_lua_audio_storage_pending = 0;
static volatile bool s_lua_audio_storage_active = false;
static inline bool luaAudioActive() {
  return __atomic_load_n(&s_lua_audio_active, __ATOMIC_ACQUIRE);
}
bool storageBusy() {
  return __atomic_load_n(&s_lua_audio_storage_pending, __ATOMIC_ACQUIRE) != 0 ||
         __atomic_load_n(&s_lua_audio_storage_active, __ATOMIC_ACQUIRE);
}
#endif

#if defined(HELTEC_LORA_V4_R8) || defined(HAS_THINKNODE_M9) || defined(HAS_TDECK_PRO)
#endif
#if defined(HAS_TDECK_GT911) || defined(TLORA_PAGER)
static constexpr int kI2sSampleRate = 16000;
static constexpr i2s_port_t kI2sPort = I2S_NUM_0;
// The tile fetcher's in-flight counter (defined later in the file). We skip
// beeping while tiles are downloading — the I2S DMA buffers + Wi-Fi RX DMA + a
// tile decode all contend for the scarce internal DMA RAM, and this build is
// already tight enough that tile downloads can OOM-reboot on their own.
#if defined(HAS_TDECK_GT911) || defined(TLORA_PAGER)
#endif
#if defined(HAS_TDECK_GT911)
// I2S is installed ON DEMAND for the duration of a tone and uninstalled after.
// Holding the driver resident permanently kept ~2 KB of internal DMA RAM, which
// shrank the margin the tile-fetch worker relies on and made tile downloads
// OOM-reboot. Transient install keeps steady-state internal RAM untouched.
static bool tdeckAudioInstallRate(int rate) {
  // Heap pre-flight. i2s_driver_install ESP_ERROR_CHECKs its internal DMA + timer
  // allocations and abort()s the firmware on NO_MEM — this is the "esp_timer_create
  // ESP_ERR_NO_MEM" crash testers hit toggling sound while BLE + Wi-Fi are both up
  // and internal DRAM is exhausted. Skip the chime silently when it's too tight
  // rather than crash. (Buffer sizing is separate; this is just a guard.)
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 16 * 1024) return false;
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = rate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;   // MAX98357A is mono
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  if (i2s_driver_install(kI2sPort, &cfg, 0, nullptr) != ESP_OK) return false;
  i2s_pin_config_t pins = {};
  pins.mck_io_num   = I2S_PIN_NO_CHANGE;
  pins.bck_io_num   = PIN_I2S_BCK;
  pins.ws_io_num    = PIN_I2S_WS;
  pins.data_out_num = PIN_I2S_DOUT;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(kI2sPort, &pins) != ESP_OK) { i2s_driver_uninstall(kI2sPort); return false; }
  return true;
}
static bool tdeckAudioInstall() { return tdeckAudioInstallRate(kI2sSampleRate); }

// Render `freq` Hz for `ms` ms into the already-installed I2S as a 16-bit sine
// with a short fade-in/out so it doesn't click.
static void tdeckPlayToneRaw(int freq, int ms, int vol = 9000) {
  const int total = (kI2sSampleRate * ms) / 1000;
  const int fade = total / 8 > 0 ? total / 8 : 1;
  int16_t buf[128];
  int written_total = 0;
  double phase = 0.0;
  const double step = 2.0 * M_PI * (double)freq / (double)kI2sSampleRate;
  while (written_total < total) {
    int n = 0;
    for (; n < 128 && written_total < total; ++n, ++written_total) {
      double amp = (double)vol;
      if (written_total < fade)            amp *= (double)written_total / fade;
      else if (written_total > total-fade) amp *= (double)(total - written_total)/fade;
      buf[n] = (int16_t)(sin(phase) * amp);
      phase += step;
      if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
    }
    size_t bw = 0;
    i2s_write(kI2sPort, buf, n * sizeof(int16_t), &bw, pdMS_TO_TICKS(200));
  }
  i2s_zero_dma_buffer(kI2sPort);
}
#endif  // HAS_TDECK_GT911

#if defined(TLORA_PAGER)
// Same on-demand-install rationale as the T-Deck (above), plus this board's
// ES8311 codec: I2S clocks (incl. MCLK) must already be toggling before the
// codec's PLL will lock, so codec register writes happen after i2s_set_pin.
// The codec chip itself stays powered across chimes (its own begin() runs
// once, lazily, on first use) -- only the I2S driver and the codec's DAC
// power/format state (start()/suspend()) are cycled per playback, matching
// the amp's AMP_EN toggle in TLoraPagerBoard (see pagerNotifyTaskFn below).
static Es8311Codec s_pager_codec;
static bool        s_pager_codec_begun = false;

static bool pagerAudioInstallRate(int rate) {
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 16 * 1024) return false;
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = rate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;   // mono synth/WAV buffer
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  if (i2s_driver_install(kI2sPort, &cfg, 0, nullptr) != ESP_OK) return false;
  i2s_pin_config_t pins = {};
  pins.mck_io_num   = PIN_I2S_MCLK;
  pins.bck_io_num   = PIN_I2S_BCK;
  pins.ws_io_num    = PIN_I2S_WS;
  pins.data_out_num = PIN_I2S_DOUT;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(kI2sPort, &pins) != ESP_OK) { i2s_driver_uninstall(kI2sPort); return false; }
  if (!s_pager_codec_begun) {
    s_pager_codec_begun = s_pager_codec.begin(Wire, 0x18);
    if (!s_pager_codec_begun) { i2s_driver_uninstall(kI2sPort); return false; }
  }
  if (!s_pager_codec.start((uint32_t)rate)) { i2s_driver_uninstall(kI2sPort); return false; }
  return true;
}
static bool pagerAudioInstall() { return pagerAudioInstallRate(kI2sSampleRate); }
static void pagerAudioUninstall() {
  s_pager_codec.setMute(true);
  s_pager_codec.suspend();
  i2s_zero_dma_buffer(kI2sPort);
  i2s_driver_uninstall(kI2sPort);
}

// Render `freq` Hz for `ms` ms into the already-installed I2S as a 16-bit
// sine with a short fade-in/out so it doesn't click. Unlike the T-Deck's
// tdeckPlayToneRaw, loudness comes from the codec's hardware volume register
// (set by the caller before this runs), not software sample scaling, so
// there's no `vol` parameter here -- always render at a fixed safe level.
static void pagerPlayToneRaw(int freq, int ms) {
  const int total = (kI2sSampleRate * ms) / 1000;
  const int fade = total / 8 > 0 ? total / 8 : 1;
  int16_t buf[128];
  int written_total = 0;
  double phase = 0.0;
  const double step = 2.0 * M_PI * (double)freq / (double)kI2sSampleRate;
  const double amp0 = 22000.0;
  while (written_total < total) {
    int n = 0;
    for (; n < 128 && written_total < total; ++n, ++written_total) {
      double amp = amp0;
      if (written_total < fade)            amp *= (double)written_total / fade;
      else if (written_total > total-fade) amp *= (double)(total - written_total)/fade;
      buf[n] = (int16_t)(sin(phase) * amp);
      phase += step;
      if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
    }
    size_t bw = 0;
    i2s_write(kI2sPort, buf, n * sizeof(int16_t), &bw, pdMS_TO_TICKS(200));
  }
  i2s_zero_dma_buffer(kI2sPort);
}
#endif  // TLORA_PAGER

// ---- Custom WAV notification playback (T-Deck I2S) -------------------------
// Stream a small PCM WAV (internal SPIFFS or "sd:"-prefixed SD) straight to the
// amp. Supports PCM 16-bit, mono or stereo (downmixed to the mono amp), sample
// rate read from the header; capped to ~6 s so a stray big file can't hold the
// notify task. Any problem -> false, and the caller falls back to the chime.
static bool wavOpen(const char* prefpath, File& f) {
  if (!prefpath || !prefpath[0]) return false;
  fs::FS* fsp = &SPIFFS; const char* fp = prefpath;
#if defined(HAS_TDECK_GT911) || defined(TLORA_PAGER)   // only the T-Deck/pager sound picker ever writes an "sd:"-prefixed pref
  // No fmSdTryMount() here: this runs on the throwaway notify task, and changing
  // the SD VFS lifecycle from a second task races the loop task's own SD use.
  // If the card
  // isn't mounted the open below fails fast and the chime falls back; mounting
  // is owned by boot adoption / sdHealthTick's reinsert watch / the FM paths.
  if (!strncmp(prefpath, "sd:", 3)) { fsp = &SD; fp = prefpath + 3; }
#endif
  f = fsp->open(fp, FILE_READ);
  if (!f || f.isDirectory()) { if (f) f.close(); return false; }
  return true;
}
bool supportsWav(const char* prefpath) {
  ui::platform::StorageLease storage;
  if (!storage.acquired()) return false;
  File f; if (!wavOpen(prefpath, f)) return false;
  bool ok = wavParse(f, nullptr, nullptr, nullptr);
  f.close();
  return ok;
}

// ---- Shared notification-task state (T-Deck + pager) -----------------------
static volatile bool s_notify_playing = false;
static volatile int  s_notify_slot    = TOUCH_SND_MSG;   // which per-event sound to play this round
static char          s_notify_path[TOUCH_SOUND_PATH_MAXLEN] = {0};   // caller-resolved WAV path (avoid NVS in the task)
static volatile int  s_notify_vol     = 9000;    // meaning is board-specific: T-Deck = software amplitude, pager = 0-100 hw volume pct

#if defined(HAS_TDECK_GT911)
static bool tdeckPlayWavFile(const char* prefpath, int vol) {
  ui::platform::StorageLease storage;
  if (!storage.acquired()) return false; // notification falls back to its built-in tone
  File f; if (!wavOpen(prefpath, f)) return false;
  uint16_t ch=0; uint32_t rate=0, dlen=0;
  if (!wavParse(f, &ch, &rate, &dlen)) { f.close(); return false; }
  const uint32_t frameBytes = (uint32_t)ch * 2u;
  const uint32_t maxBytes = rate * frameBytes * 6u;   // ~6 s cap
  if (dlen > maxBytes) dlen = maxBytes;
  if (!tdeckAudioInstallRate((int)rate)) { f.close(); return false; }
  const float gain = (float)vol / 13000.0f;
  int16_t in[256], out[256];
  uint32_t remaining = dlen;
  while (remaining >= frameBytes) {
    size_t want = sizeof(in);
    if (want > remaining) want = remaining - (remaining % frameBytes);
    int got = f.read((uint8_t*)in, want);
    if (got <= 0) break;
    int frames = got / (int)frameBytes;
    for (int i = 0; i < frames; ++i) {
      int32_t smp = (ch == 2) ? (((int32_t)in[2*i] + in[2*i+1]) / 2) : in[i];
      smp = (int32_t)(smp * gain);
      if (smp > 32767) smp = 32767; else if (smp < -32768) smp = -32768;
      out[i] = (int16_t)smp;
    }
    size_t bw = 0;
    i2s_write(kI2sPort, out, (size_t)frames * sizeof(int16_t), &bw, pdMS_TO_TICKS(300));
    remaining -= (uint32_t)got;
  }
  i2s_zero_dma_buffer(kI2sPort);
  i2s_driver_uninstall(kI2sPort);
  f.close();
  return true;
}

// The chime body: install I2S, play the notes, uninstall. ~300 ms of blocking
// i2s_write + driver setup/teardown — run on its own throwaway task (below).
static void tdeckNotifyTaskFn(void* arg) {
  (void)arg;
  const int v = s_notify_vol;
  // Custom WAV for this slot (if set + valid) — installs/uninstalls I2S itself.
  // Path was resolved on the caller thread (s_notify_path) to keep NVS access
  // off this short-lived task.
  bool played = (s_notify_path[0] && tdeckPlayWavFile(s_notify_path, v));
  if (!played && tdeckAudioInstall()) {         // built-in chime fallback (distinct per slot)
    if (s_notify_slot == TOUCH_SND_MEN) {        // @-mention: bright 3-note rising arpeggio
      tdeckPlayToneRaw(1318, 70,  v);            // E6
      tdeckPlayToneRaw(1760, 70,  v);            // A6
      tdeckPlayToneRaw(2349, 130, v);            // D7
    } else if (s_notify_slot == TOUCH_SND_DM) {  // direct message: distinct rising fifth
      tdeckPlayToneRaw(1047, 90,  v);            // C6
      tdeckPlayToneRaw(1568, 120, v);            // G6
    } else {                                     // message: the original two-note chime
      tdeckPlayToneRaw(880,  90,  v);            // A5
      tdeckPlayToneRaw(1318, 110, v);            // E6
    }
    i2s_driver_uninstall(kI2sPort);
  }
  s_notify_playing = false;
  vTaskDelete(nullptr);
}

// Notification chime. Spawned on a short-lived task so the ~300 ms of I2S work
// does NOT freeze the UI — newMsgImpl (the caller) runs on the UI/mesh thread, so
// playing synchronously locked the screen for the whole chime. Skips while tiles
// download (DMA-RAM contention → reboot) and won't stack itself. Caller checks the
// sound pref.
static void tdeckPlayNotifySlot(int slot) {
  if ((host.pendingTiles ? host.pendingTiles() : 0) > 0) return;   // don't fight the Wi-Fi/tile DMA buffers
  if (s_notify_playing) return;           // already chiming — don't stack tasks/I2S
  s_notify_slot = slot;
  s_notify_vol = (int)touchPrefsGetSoundVolume() * 130;   // 0..100 -> 0..13000 amplitude
  touchPrefsGetSoundFile(slot, s_notify_path, sizeof s_notify_path);   // resolve here, not in the task
  s_notify_playing = true;
  // bigger stack than the tone-only chime: WAV path uses a File handle + buffers.
  if (xTaskCreate(tdeckNotifyTaskFn, "notify", 8192, nullptr, 3, nullptr) != pdPASS) {
    s_notify_playing = false;             // couldn't spawn (low DRAM) — skip the chime
  }
}
// Preview an arbitrary WAV (not yet saved to a slot) on the notify task.
static void tdeckPreviewWavFile(const char* prefpath) {
  if ((host.pendingTiles ? host.pendingTiles() : 0) > 0 || s_notify_playing) return;
  strncpy(s_notify_path, prefpath, sizeof s_notify_path - 1);
  s_notify_path[sizeof s_notify_path - 1] = '\0';
  s_notify_slot = TOUCH_SND_MSG;          // if the file fails, the task plays the msg chime
  s_notify_vol  = (int)touchPrefsGetSoundVolume() * 130;
  s_notify_playing = true;
  if (xTaskCreate(tdeckNotifyTaskFn, "notify", 8192, nullptr, 3, nullptr) != pdPASS)
    s_notify_playing = false;
}
#endif  // HAS_TDECK_GT911

#if defined(TLORA_PAGER)
static bool pagerPlayWavFile(const char* prefpath, int volPct) {
  ui::platform::StorageLease storage;
  if (!storage.acquired()) return false;
  File f; if (!wavOpen(prefpath, f)) return false;
  uint16_t ch=0; uint32_t rate=0, dlen=0;
  if (!wavParse(f, &ch, &rate, &dlen)) { f.close(); return false; }
  const uint32_t frameBytes = (uint32_t)ch * 2u;
  const uint32_t maxBytes = rate * frameBytes * 6u;   // ~6 s cap, matches the T-Deck path
  if (dlen > maxBytes) dlen = maxBytes;
  if (!pagerAudioInstallRate((int)rate)) { f.close(); return false; }
  s_pager_codec.setVolumePercent((uint8_t)volPct);
  s_pager_codec.setMute(false);
  int16_t in[256], out[256];
  uint32_t remaining = dlen;
  while (remaining >= frameBytes) {
    size_t want = sizeof(in);
    if (want > remaining) want = remaining - (remaining % frameBytes);
    int got = f.read((uint8_t*)in, want);
    if (got <= 0) break;
    int frames = got / (int)frameBytes;
    for (int i = 0; i < frames; ++i) {
      // Downmix to mono -- no software gain (unlike the T-Deck path): the
      // codec's hardware volume register, already set above, is doing that.
      out[i] = (ch == 2) ? (int16_t)(((int32_t)in[2*i] + in[2*i+1]) / 2) : in[i];
    }
    size_t bw = 0;
    i2s_write(kI2sPort, out, (size_t)frames * sizeof(int16_t), &bw, pdMS_TO_TICKS(300));
    remaining -= (uint32_t)got;
  }
  i2s_zero_dma_buffer(kI2sPort);
  pagerAudioUninstall();
  f.close();
  return true;
}

// Mirrors tdeckNotifyTaskFn, bracketed with the amp's AMP_EN toggle (muted/
// off at idle to save battery -- see TLoraPagerBoard::setAmpEnabled()).
static void pagerNotifyTaskFn(void* arg) {
  (void)arg;
  const int volPct = s_notify_vol;
  board.setAmpEnabled(true);
  bool played = (s_notify_path[0] && pagerPlayWavFile(s_notify_path, volPct));
  if (!played && pagerAudioInstall()) {
    s_pager_codec.setVolumePercent((uint8_t)volPct);
    s_pager_codec.setMute(false);
    if (s_notify_slot == TOUCH_SND_MEN) {        // @-mention: bright 3-note rising arpeggio
      pagerPlayToneRaw(1318, 70);                // E6
      pagerPlayToneRaw(1760, 70);                // A6
      pagerPlayToneRaw(2349, 130);               // D7
    } else if (s_notify_slot == TOUCH_SND_DM) {  // direct message: distinct rising fifth
      pagerPlayToneRaw(1047, 90);                // C6
      pagerPlayToneRaw(1568, 120);               // G6
    } else {                                     // message: the original two-note chime
      pagerPlayToneRaw(880,  90);                // A5
      pagerPlayToneRaw(1318, 110);               // E6
    }
    pagerAudioUninstall();
  }
  board.setAmpEnabled(false);
  s_notify_playing = false;
  vTaskDelete(nullptr);
}

static void pagerPlayNotifySlot(int slot) {
  if ((host.pendingTiles ? host.pendingTiles() : 0) > 0) return;
  if (s_notify_playing) return;
  s_notify_slot = slot;
  s_notify_vol = (int)touchPrefsGetSoundVolume();   // 0..100 -> the codec's hw volume register
  touchPrefsGetSoundFile(slot, s_notify_path, sizeof s_notify_path);
  s_notify_playing = true;
  if (xTaskCreate(pagerNotifyTaskFn, "notify", 8192, nullptr, 3, nullptr) != pdPASS) {
    s_notify_playing = false;
  }
}
static void pagerPreviewWavFile(const char* prefpath) {
  if ((host.pendingTiles ? host.pendingTiles() : 0) > 0 || s_notify_playing) return;
  strncpy(s_notify_path, prefpath, sizeof s_notify_path - 1);
  s_notify_path[sizeof s_notify_path - 1] = '\0';
  s_notify_slot = TOUCH_SND_MSG;
  s_notify_vol  = (int)touchPrefsGetSoundVolume();
  s_notify_playing = true;
  if (xTaskCreate(pagerNotifyTaskFn, "notify", 8192, nullptr, 3, nullptr) != pdPASS)
    s_notify_playing = false;
}
#endif  // TLORA_PAGER

#endif  // HAS_TDECK_GT911 || TLORA_PAGER

// ---- Unified UI notification sound (T-Deck I2S / pager codec / Heltec V4 + Elecrow M9
//      piezo / T-Display P4 ES8311 codec) ----
// One source of truth: device_caps.h computes the same board list as CAP_SOUND.
#if CAP_SOUND
  #define HAS_UI_SOUND 1
#endif

#if defined(HAS_TDISPLAY_P4)
// T-Display P4: ES8311 codec + NS4150B speaker (P4Audio.cpp in the variant). Same three
// chime patterns as the T-Deck, rendered on a throwaway task so the ~300 ms of blocking
// I2S writes never stalls the UI/mesh loop. Loudness = amplitude from the volume pref
// (the codec DAC volume stays fixed).

static volatile bool s_p4_snd_playing = false;
static volatile int  s_p4_snd_slot    = TOUCH_SND_MSG;
static volatile int  s_p4_snd_vol     = 9000;
static void p4NotifyTaskFn(void*) {
  const int v = s_p4_snd_vol;
  if (s_p4_snd_slot == TOUCH_SND_MEN) {        // @-mention: bright 3-note rising arpeggio
    p4AudioTone(1318, 70,  v);                 // E6
    p4AudioTone(1760, 70,  v);                 // A6
    p4AudioTone(2349, 130, v);                 // D7
  } else if (s_p4_snd_slot == TOUCH_SND_DM) {  // direct message: distinct rising fifth
    p4AudioTone(1047, 90,  v);                 // C6
    p4AudioTone(1568, 120, v);                 // G6
  } else {                                     // message: the original two-note chime
    p4AudioTone(880,  90,  v);                 // A5
    p4AudioTone(1318, 110, v);                 // E6
  }
  s_p4_snd_playing = false;
  vTaskDelete(nullptr);
}
static void p4PlayNotifySlot(int slot) {
  if (s_p4_snd_playing) return;                // already chiming — don't stack tasks
  const int pct = (int)touchPrefsGetSoundVolume();
  if (pct <= 0) return;
  s_p4_snd_slot = slot;
  s_p4_snd_vol  = pct * 130;                   // 0..100 -> 0..13000 amplitude
  s_p4_snd_playing = true;
  if (xTaskCreate(p4NotifyTaskFn, "notify", 4096, nullptr, 3, nullptr) != pdPASS) {
    s_p4_snd_playing = false;                  // couldn't spawn — skip the chime
  }
}
#endif

#if defined(HELTEC_V4_BUZZER_PIN) || defined(THINKNODE_M9_BUZZER_PIN)
#if defined(HELTEC_V4_BUZZER_PIN)
  #define UI_BUZZER_PIN HELTEC_V4_BUZZER_PIN
#else
  #define UI_BUZZER_PIN THINKNODE_M9_BUZZER_PIN
#endif
// Simple piezo buzzer, GPIO-driven via Arduino tone()/noTone() (Heltec V4 expansion kit,
// or the ThinkNode M9's onboard buzzer/BUZZER_EN). Plays a short two-note chime via LEDC
// on a throwaway task so the ~210 ms doesn't stall the UI thread.
static volatile bool s_v4_beep_playing = false;
static volatile bool s_v4_mention = false;
static void v4BeepTaskFn(void* arg) {
  (void)arg;
  // "Loud alerts" shifts the same chime into the piezo's resonant band. A bare
  // piezo on a GPIO has no volume control -- tone() is a fixed-duty square wave,
  // so amplitude is whatever the part does at that pitch -- but output near
  // mechanical resonance (around 4 kHz on typical parts) is far higher than at
  // the 1-2.6 kHz used here. Same three-note shape, so it still reads as the
  // same chime rather than a different alert (#388).
#if defined(ESP32)
  const bool loud = touchPrefsGetLoudAlerts();
#else
  const bool loud = false;
#endif
  if (s_v4_mention) {
    tone(UI_BUZZER_PIN, loud ? 3400 : 1500);  vTaskDelay(pdMS_TO_TICKS(110));
    tone(UI_BUZZER_PIN, loud ? 3900 : 2000);  vTaskDelay(pdMS_TO_TICKS(110));
    tone(UI_BUZZER_PIN, loud ? 4200 : 2600);  vTaskDelay(pdMS_TO_TICKS(170));
  } else {
    tone(UI_BUZZER_PIN, loud ? 3200 : 1000);  vTaskDelay(pdMS_TO_TICKS(160));
    tone(UI_BUZZER_PIN, loud ? 3700 : 1500);  vTaskDelay(pdMS_TO_TICKS(160));
    tone(UI_BUZZER_PIN, loud ? 4100 : 2000);  vTaskDelay(pdMS_TO_TICKS(200));
  }
  noTone(UI_BUZZER_PIN);
  pinMode(UI_BUZZER_PIN, INPUT);   // high-Z → no idle current / no buzz
  s_v4_beep_playing = false;
  vTaskDelete(nullptr);
}
static void v4BuzzerBeep(bool mention) {
  if (s_v4_beep_playing) return;
  s_v4_mention = mention;
  s_v4_beep_playing = true;
  if (xTaskCreate(v4BeepTaskFn, "v4beep", 2048, nullptr, 3, nullptr) != pdPASS)
    s_v4_beep_playing = false;
}
#endif

#if defined(HAS_TANMATSU)
#endif
// Play the platform's notification chime. Caller checks the buzzer/sound pref.
void playSlot(int slot) {
#if CAP_LUA_AUDIO
  if (luaAudioActive()) return;
#endif
#if defined(HAS_TDECK_GT911)
  tdeckPlayNotifySlot(slot);
#elif defined(HAS_TDISPLAY_P4)
  p4PlayNotifySlot(slot);                // ES8311 codec chimes (same patterns as the T-Deck)
#elif defined(TLORA_PAGER)
  pagerPlayNotifySlot(slot);
#elif defined(HELTEC_V4_BUZZER_PIN) || defined(THINKNODE_M9_BUZZER_PIN)
  v4BuzzerBeep(slot == TOUCH_SND_MEN);   // mention = higher trill; msg/DM = the lower chime
#elif defined(HAS_TANMATSU)
  beep();   // single codec tick on these boards (no per-slot sounds)
#endif
  (void)slot;
}
// Notification chimes. playNotification = generic message slot; playMention = @-mention slot.
void playNotification()  { playSlot(TOUCH_SND_MSG); }
void playMention() { playSlot(TOUCH_SND_MEN); }
// Preview an arbitrary WAV file (not yet saved to a slot) -- used by the
// sound picker's "play" button before the user commits to a choice.
void previewWav(const char* path) {
#if CAP_LUA_AUDIO
  if (luaAudioActive()) return;
#endif
#if defined(HAS_TDECK_GT911)
  tdeckPreviewWavFile(path);
#elif defined(TLORA_PAGER)
  pagerPreviewWavFile(path);
#endif
  (void)path;
}

#if defined(HAS_TANMATSU)
// Defined in the HAS_TANMATSU apply block far below; forward-declared so the
// Sound control-center chip (ccSoundCb) can chime through the I2S codec.
// Live codec volume (ES8156). Defined far below with the CC volume slider; forward-
// declared so the Sound settings page (buildDeviceSettings, above it) can apply the
// volume live as the user adjusts it.
#elif defined(HAS_TDISPLAY_P4)
// Same forward-declaration for the P4 (defined in its apply block far below): the
// Sound settings volume buttons keep the CC slider state in sync through it.
#endif


#if defined(HAS_TANMATSU)
static uint8_t s_volume_pct = 70;
static SemaphoreHandle_t s_tan_audio_mutex = nullptr;
static SemaphoreHandle_t tanAudioMutex() {
  if (!s_tan_audio_mutex) s_tan_audio_mutex = xSemaphoreCreateMutex();
  return s_tan_audio_mutex;
}
// The audio subsystem (ES8156 codec + I2S) is brought up by bsp_device_initialize()
// at boot — so here we only set the codec volume and toggle the speaker amplifier.
void setVolume(uint8_t pct) {
  if (pct > 100) pct = 100;
  s_volume_pct = pct;                              // store the slider value
  // The bottom ~half of the codec's range sits below the speaker's audible threshold,
  // so map the slider into the audible band (≈50..100% of the codec) and mute at 0.
  bsp_audio_set_volume(pct == 0 ? 0.0f : 50.0f + (float)pct * 0.5f);
  bsp_audio_set_amplifier(pct > 0);
}
// Short sine "tick" through the I2S codec so adjusting the volume slider is audible.
// The codec's I2S DMA REPLAYS its last buffer on TX underrun (no auto-clear), so a bare
// tone would loop forever — we follow it with a block of silence LARGER than the DMA ring,
// which leaves every descriptor zeroed once played out, and the tone stops cleanly.
void beep() {
  if (s_volume_pct == 0) return;
  SemaphoreHandle_t mutex = tanAudioMutex();
  if (!mutex || xSemaphoreTake(mutex, 0) != pdTRUE) return;
  static uint32_t s_last_beep = 0;             // throttle: holding the slider ramps fast, but
  if (millis() - s_last_beep < 200) { xSemaphoreGive(mutex); return; } // don't machine-gun a tone on every repeat step
  s_last_beep = millis();
  i2s_chan_handle_t h = nullptr;
  if (bsp_audio_get_i2s_handle(&h) != ESP_OK || !h) { xSemaphoreGive(mutex); return; }
  const int rate = 44100, freq = 880;
  const int tone  = rate * 30 / 1000;             // ~30 ms tone …
  const int total = tone + rate * 50 / 1000;      // … then ~50 ms silence (> the DMA ring) flushes the loop
  const int fade  = rate * 4 / 1000;              // 4 ms in/out fade kills the click
  static int16_t buf[3600 * 2];                   // stereo (L,R), ~80 ms — the P4 has RAM to spare
  const int n = total > 3600 ? 3600 : total;
  for (int i = 0; i < n; i++) {
    int16_t s = 0;
    if (i < tone) {
      float env = 1.0f;
      if (i < fade)              env = (float)i / fade;
      else if (i >= tone - fade) env = (float)(tone - i) / fade;
      s = (int16_t)(9000.0f * env * sinf(2.0f * 3.14159265f * freq * i / rate));
    }
    buf[2 * i] = buf[2 * i + 1] = s;
  }
  size_t wr = 0;
  i2s_channel_write(h, buf, (size_t)n * 2 * sizeof(int16_t), &wr, 200 / portTICK_PERIOD_MS);
  xSemaphoreGive(mutex);
}

#elif defined(HAS_TDISPLAY_P4)
static uint8_t s_volume_pct = 70;
void setVolume(uint8_t pct) {
  if (pct > 100) pct = 100;
  s_volume_pct = pct;   // play paths read the persisted pref; this keeps the slider live
}

#endif
#if CAP_LUA_AUDIO
namespace {
enum class LuaAudioCommandKind : uint8_t { Play, Pause, Resume, Stop, Release };
enum class LuaAudioState : uint8_t { Stopped, Playing, Paused, Ended, Error };
enum class LuaAudioRunResult : uint8_t { Ended, Stopped, Replaced, Released, Error, Deferred };
enum class LuaAudioTaskState : uint8_t { Stopped, Starting, Running, Stopping };
enum class LuaAudioFormat : uint8_t { Wav, Mp3 };

struct LuaAudioCommand {
  LuaAudioCommandKind kind = LuaAudioCommandKind::Stop;
  fs::FS* fs = nullptr;
  uint32_t owner = 0;
  LuaAudioFormat format = LuaAudioFormat::Wav;
  char path[224] = "";
  char shown[192] = "";
  char source[8] = "";
};

struct LuaAudioStatus {
  uint32_t owner = 0;
  LuaAudioState state = LuaAudioState::Stopped;
  char path[192] = "";
  char source[8] = "";
  char format[8] = "";
  char error[40] = "";
};

struct LuaAudioSinkSession {
  uint32_t output_rate = 0;
  float gain = 1.0f;
  bool open = false;
#if defined(HAS_TANMATSU)
  i2s_chan_handle_t handle = nullptr;
  SemaphoreHandle_t mutex = nullptr;
#endif
};

class LuaAudioStorageLease {
 public:
  LuaAudioStorageLease() {
    __atomic_store_n(&s_lua_audio_storage_active, true, __ATOMIC_RELEASE);
    if (__atomic_load_n(&s_lua_audio_storage_pending, __ATOMIC_ACQUIRE) != 0)
      __atomic_fetch_sub(&s_lua_audio_storage_pending, 1u, __ATOMIC_ACQ_REL);
  }
  ~LuaAudioStorageLease() {
    __atomic_store_n(&s_lua_audio_storage_active, false, __ATOMIC_RELEASE);
  }
};

static QueueHandle_t s_lua_audio_queue = nullptr;
static TaskHandle_t s_lua_audio_task = nullptr;
static LuaAudioTaskState s_lua_audio_task_state = LuaAudioTaskState::Stopped;
static portMUX_TYPE s_lua_audio_mux = portMUX_INITIALIZER_UNLOCKED;
static LuaAudioStatus s_lua_audio_status;

static void luaAudioCopy(char* out, size_t cap, const char* value) {
  if (!cap) return;
  snprintf(out, cap, "%s", value ? value : "");
}

static void luaAudioSetTrackStatus(const LuaAudioCommand& command, LuaAudioState state,
                                   const char* error = nullptr) {
  portENTER_CRITICAL(&s_lua_audio_mux);
  s_lua_audio_status.owner = command.owner;
  s_lua_audio_status.state = state;
  luaAudioCopy(s_lua_audio_status.path, sizeof s_lua_audio_status.path, command.shown);
  luaAudioCopy(s_lua_audio_status.source, sizeof s_lua_audio_status.source, command.source);
  luaAudioCopy(s_lua_audio_status.format, sizeof s_lua_audio_status.format,
               command.format == LuaAudioFormat::Mp3 ? "mp3" : "wav");
  luaAudioCopy(s_lua_audio_status.error, sizeof s_lua_audio_status.error, error);
  portEXIT_CRITICAL(&s_lua_audio_mux);
}

static void luaAudioSetState(uint32_t owner, LuaAudioState state, const char* error = nullptr) {
  portENTER_CRITICAL(&s_lua_audio_mux);
  if (s_lua_audio_status.owner == owner) {
    s_lua_audio_status.state = state;
    luaAudioCopy(s_lua_audio_status.error, sizeof s_lua_audio_status.error, error);
  }
  portEXIT_CRITICAL(&s_lua_audio_mux);
}

static bool luaAudioSinkBegin(uint32_t source_rate, LuaAudioSinkSession* sink,
                              const char** error) {
  if (!sink) return false;
  const int volume = (int)touchPrefsGetSoundVolume();
  if (volume <= 0) { if (error) *error = "muted"; return false; }

#if defined(HAS_TDECK_GT911)
  if (s_notify_playing) { if (error) *error = "busy"; return false; }
  if (!tdeckAudioInstallRate((int)source_rate)) {
    if (error) *error = "audio unavailable";
    return false;
  }
  sink->output_rate = source_rate;
  sink->gain = (float)volume / 100.0f;
#elif defined(TLORA_PAGER)
  if (s_notify_playing) { if (error) *error = "busy"; return false; }
  board.setAmpEnabled(true);
  if (!pagerAudioInstallRate((int)source_rate)) {
    board.setAmpEnabled(false);
    if (error) *error = "audio unavailable";
    return false;
  }
  s_pager_codec.setVolumePercent((uint8_t)volume);
  s_pager_codec.setMute(false);
  sink->output_rate = source_rate;
#elif defined(HAS_TDISPLAY_P4)
  if (!p4AudioStreamBegin()) {
    if (error) *error = "audio unavailable";
    return false;
  }
  sink->output_rate = p4AudioStreamRate();
  sink->gain = (float)volume / 100.0f;
#elif defined(HAS_TANMATSU)
  sink->mutex = tanAudioMutex();
  if (!sink->mutex || xSemaphoreTake(sink->mutex, pdMS_TO_TICKS(300)) != pdTRUE) {
    if (error) *error = "busy";
    return false;
  }
  if (bsp_audio_get_i2s_handle(&sink->handle) != ESP_OK || !sink->handle) {
    xSemaphoreGive(sink->mutex);
    sink->mutex = nullptr;
    if (error) *error = "audio unavailable";
    return false;
  }
  setVolume((uint8_t)volume);
  sink->output_rate = 44100;
#endif

  sink->open = sink->output_rate != 0;
  return sink->open;
}

static bool luaAudioSinkWrite(LuaAudioSinkSession* sink, const int16_t* samples, size_t frames) {
  if (!sink || !sink->open || !samples || !frames) return false;
#if defined(HAS_TDECK_GT911) || defined(TLORA_PAGER)
  size_t written = 0;
  const size_t bytes = frames * sizeof(int16_t);
  return i2s_write(kI2sPort, samples, bytes, &written, pdMS_TO_TICKS(300)) == ESP_OK &&
         written == bytes;
#elif defined(HAS_TDISPLAY_P4)
  return p4AudioStreamWrite(samples, frames);
#elif defined(HAS_TANMATSU)
  static int16_t stereo[256 * 2];
  if (frames > 256) return false;
  for (size_t i = 0; i < frames; ++i)
    stereo[2 * i] = stereo[2 * i + 1] = samples[i];
  size_t written = 0;
  const size_t bytes = frames * 2 * sizeof(int16_t);
  return i2s_channel_write(sink->handle, stereo, bytes, &written, pdMS_TO_TICKS(300)) == ESP_OK &&
         written == bytes;
#endif
}

static void luaAudioSinkEnd(LuaAudioSinkSession* sink) {
  if (!sink || !sink->open) return;
#if defined(HAS_TDECK_GT911)
  i2s_zero_dma_buffer(kI2sPort);
  i2s_driver_uninstall(kI2sPort);
#elif defined(TLORA_PAGER)
  pagerAudioUninstall();
  board.setAmpEnabled(false);
#elif defined(HAS_TDISPLAY_P4)
  p4AudioStreamEnd();
#elif defined(HAS_TANMATSU)
  static const int16_t silence[256 * 2] = {};
  for (int i = 0; i < 16; ++i) {
    size_t written = 0;
    i2s_channel_write(sink->handle, silence, sizeof silence, &written, pdMS_TO_TICKS(300));
  }
  if (sink->mutex) xSemaphoreGive(sink->mutex);
  sink->handle = nullptr;
  sink->mutex = nullptr;
#endif
  sink->open = false;
}

static bool luaAudioControlMatches(const LuaAudioCommand& control, uint32_t owner) {
  return control.kind == LuaAudioCommandKind::Play || control.owner == owner;
}

static bool luaAudioPollControl(const LuaAudioCommand& command,
                                LuaAudioCommand* replacement,
                                LuaAudioRunResult* result) {
  LuaAudioCommand control;
  if (xQueueReceive(s_lua_audio_queue, &control, 0) != pdTRUE ||
      !luaAudioControlMatches(control, command.owner)) return false;

  if (control.kind == LuaAudioCommandKind::Play) {
    if (replacement) *replacement = control;
    if (result) *result = LuaAudioRunResult::Replaced;
    return true;
  }
  if (control.kind == LuaAudioCommandKind::Stop) {
    if (result) *result = LuaAudioRunResult::Stopped;
    return true;
  }
  if (control.kind == LuaAudioCommandKind::Release) {
    if (result) *result = LuaAudioRunResult::Released;
    return true;
  }
  if (control.kind != LuaAudioCommandKind::Pause) return false;

  luaAudioSetState(command.owner, LuaAudioState::Paused);
  for (;;) {
    if (xQueueReceive(s_lua_audio_queue, &control, portMAX_DELAY) != pdTRUE) continue;
    if (!luaAudioControlMatches(control, command.owner)) continue;
    if (control.kind == LuaAudioCommandKind::Resume) {
      luaAudioSetState(command.owner, LuaAudioState::Playing);
      return false;
    }
    if (control.kind == LuaAudioCommandKind::Play) {
      if (replacement) *replacement = control;
      if (result) *result = LuaAudioRunResult::Replaced;
      return true;
    }
    if (control.kind == LuaAudioCommandKind::Release) {
      if (result) *result = LuaAudioRunResult::Released;
      return true;
    }
    if (control.kind == LuaAudioCommandKind::Stop) {
      if (result) *result = LuaAudioRunResult::Stopped;
      return true;
    }
  }
}

static LuaAudioRunResult luaAudioRunTrack(const LuaAudioCommand& command,
                                          LuaAudioCommand* replacement) {
  ui::platform::StorageLease access;
  if (!access.acquired()) return LuaAudioRunResult::Deferred;
  LuaAudioStorageLease storage_lease;
  File file = command.fs ? command.fs->open(command.path, "r") : File();
  if (!file || file.isDirectory()) {
    if (file) file.close();
    luaAudioSetTrackStatus(command, LuaAudioState::Error, "not found");
    return LuaAudioRunResult::Error;
  }

  uint16_t channels = 0;
  uint32_t source_rate = 0, data_len = 0;
  if (!wavParse(file, &channels, &source_rate, &data_len) ||
      data_len < (uint32_t)channels * sizeof(int16_t)) {
    file.close();
    luaAudioSetTrackStatus(command, LuaAudioState::Error, "unsupported format");
    return LuaAudioRunResult::Error;
  }

  LuaAudioSinkSession sink;
  const char* sink_error = nullptr;
  __atomic_store_n(&s_lua_audio_active, true, __ATOMIC_RELEASE);
  if (!luaAudioSinkBegin(source_rate, &sink, &sink_error)) {
    __atomic_store_n(&s_lua_audio_active, false, __ATOMIC_RELEASE);
    file.close();
    luaAudioSetTrackStatus(command, LuaAudioState::Error,
                           sink_error ? sink_error : "audio unavailable");
    return LuaAudioRunResult::Error;
  }

  luaAudioSetTrackStatus(command, LuaAudioState::Playing);
  LuaAudioRunResult result = LuaAudioRunResult::Ended;
  const char* run_error = nullptr;
  bool interrupted = false;
  uint32_t remaining = data_len;
  uint64_t resample_phase = 0;
  int16_t input[256];
  int16_t output[256];
  size_t output_count = 0;
  const uint32_t frame_bytes = (uint32_t)channels * sizeof(int16_t);

  while (remaining >= frame_bytes && !interrupted) {
    if (luaAudioPollControl(command, replacement, &result)) break;

    size_t want = sizeof input;
    if (want > remaining) want = remaining;
    want -= want % frame_bytes;
    const int got = file.read((uint8_t*)input, want);
    if (got <= 0) {
      result = LuaAudioRunResult::Error;
      run_error = "read failed";
      break;
    }
    const size_t frames = (size_t)got / frame_bytes;
    for (size_t i = 0; i < frames; ++i) {
      int32_t sample = channels == 2
        ? ((int32_t)input[2 * i] + input[2 * i + 1]) / 2
        : input[i];
      sample = (int32_t)((float)sample * sink.gain);
      if (sample > 32767) sample = 32767;
      else if (sample < -32768) sample = -32768;

      resample_phase += sink.output_rate;
      while (resample_phase >= source_rate) {
        output[output_count++] = (int16_t)sample;
        resample_phase -= source_rate;
        if (output_count == sizeof output / sizeof output[0]) {
          if (!luaAudioSinkWrite(&sink, output, output_count)) {
            result = LuaAudioRunResult::Error;
            run_error = "output failed";
            interrupted = true;
            break;
          }
          output_count = 0;
        }
      }
      if (interrupted) break;
    }
    remaining -= (uint32_t)got;
  }

  if (!interrupted && result == LuaAudioRunResult::Ended && output_count &&
      !luaAudioSinkWrite(&sink, output, output_count)) {
    result = LuaAudioRunResult::Error;
    run_error = "output failed";
  }

  luaAudioSinkEnd(&sink);
  file.close();
  __atomic_store_n(&s_lua_audio_active, false, __ATOMIC_RELEASE);

  if (result == LuaAudioRunResult::Ended)
    luaAudioSetState(command.owner, LuaAudioState::Ended);
  else if (result == LuaAudioRunResult::Stopped || result == LuaAudioRunResult::Released)
    luaAudioSetState(command.owner, LuaAudioState::Stopped);
  else if (result == LuaAudioRunResult::Error)
    luaAudioSetState(command.owner, LuaAudioState::Error, run_error ? run_error : "playback failed");
  return result;
}

struct LuaMp3Work {
  mp3dec_t decoder;
  mp3dec_scratch_t scratch;
  uint8_t input[16 * 1024];
  mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
};

static uint32_t luaAudioReadLe32(const uint8_t* value) {
  return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
         ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static uint32_t luaAudioMp3DataEnd(File& file) {
  uint32_t end = (uint32_t)file.size();
  uint8_t footer[128];
  if (end >= 128 && file.seek(end - 128) && file.read(footer, 3) == 3 &&
      !memcmp(footer, "TAG", 3)) {
    end -= 128;
  }
  if (end >= 32 && file.seek(end - 32) && file.read(footer, 32) == 32 &&
      !memcmp(footer, "APETAGEX", 8)) {
    const uint32_t version = luaAudioReadLe32(footer + 8);
    const uint32_t tag_size = luaAudioReadLe32(footer + 12);
    const uint32_t flags = luaAudioReadLe32(footer + 20);
    const uint64_t total_size = (uint64_t)tag_size + ((flags & 0x80000000u) ? 32u : 0u);
    if ((version == 1000 || version == 2000) && tag_size >= 32 && total_size <= end)
      end -= (uint32_t)total_size;
  }
  file.seek(0);
  return end;
}

static LuaAudioRunResult luaAudioRunMp3Track(const LuaAudioCommand& command,
                                             LuaAudioCommand* replacement) {
  ui::platform::StorageLease access;
  if (!access.acquired()) return LuaAudioRunResult::Deferred;
  LuaAudioStorageLease storage_lease;
  File file = command.fs ? command.fs->open(command.path, "r") : File();
  if (!file || file.isDirectory()) {
    if (file) file.close();
    luaAudioSetTrackStatus(command, LuaAudioState::Error, "not found");
    return LuaAudioRunResult::Error;
  }

  LuaMp3Work* work = (LuaMp3Work*)heap_caps_malloc(
    sizeof(LuaMp3Work), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!work) {
    file.close();
    luaAudioSetTrackStatus(command, LuaAudioState::Error, "out of memory");
    return LuaAudioRunResult::Error;
  }
  s_wada_mp3_scratch = &work->scratch;
  mp3dec_init(&work->decoder);
  const uint32_t data_end = luaAudioMp3DataEnd(file);
  if (!data_end) {
    s_wada_mp3_scratch = nullptr;
    heap_caps_free(work);
    file.close();
    luaAudioSetTrackStatus(command, LuaAudioState::Error, "unsupported format");
    return LuaAudioRunResult::Error;
  }

  __atomic_store_n(&s_lua_audio_active, true, __ATOMIC_RELEASE);
  luaAudioSetTrackStatus(command, LuaAudioState::Playing);
  LuaAudioRunResult result = LuaAudioRunResult::Ended;
  const char* run_error = nullptr;
  LuaAudioSinkSession sink;
  uint32_t source_rate = 0;
  uint64_t resample_phase = 0;
  int16_t output[256];
  size_t output_count = 0;
  size_t input_start = 0, input_count = 0;
  bool eof = false, decoded_any = false, interrupted = false;

  while (!interrupted) {
    if (luaAudioPollControl(command, replacement, &result)) break;

    if (!eof && input_count < 4096) {
      if (input_start && input_start + input_count + 4096 > sizeof work->input) {
        memmove(work->input, work->input + input_start, input_count);
        input_start = 0;
      }
      size_t room = sizeof work->input - (input_start + input_count);
      const uint32_t position = (uint32_t)file.position();
      const uint32_t remaining = position < data_end ? data_end - position : 0;
      if (room > remaining) room = remaining;
      const int got = room ? file.read(work->input + input_start + input_count, room) : 0;
      if (got > 0) {
        input_count += (size_t)got;
        eof = file.position() >= data_end;
      } else if (file.position() < data_end) {
        result = LuaAudioRunResult::Error;
        run_error = "read failed";
        break;
      } else {
        eof = true;
      }
    }

    if (!input_count) {
      if (!decoded_any) {
        result = LuaAudioRunResult::Error;
        run_error = "unsupported format";
      }
      break;
    }

    mp3dec_frame_info_t info = {};
    const int samples = mp3dec_decode_frame(&work->decoder,
      work->input + input_start, (int)input_count, work->pcm, &info);
    if (info.frame_bytes < 0 || (size_t)info.frame_bytes > input_count) {
      result = LuaAudioRunResult::Error;
      run_error = "decode failed";
      break;
    }
    if (info.frame_bytes > 0) {
      input_start += (size_t)info.frame_bytes;
      input_count -= (size_t)info.frame_bytes;
      if (!input_count) input_start = 0;
    } else if (eof) {
      if (!decoded_any) {
        result = LuaAudioRunResult::Error;
        run_error = "unsupported format";
      }
      break;
    } else if (input_start + input_count == sizeof work->input) {
      result = LuaAudioRunResult::Error;
      run_error = "decode failed";
      break;
    } else {
      continue;
    }

    if (samples <= 0) continue;
    if (info.layer != 3 || (info.channels != 1 && info.channels != 2) ||
        info.hz < 8000 || info.hz > 48000) {
      result = LuaAudioRunResult::Error;
      run_error = "unsupported format";
      break;
    }
    if (!sink.open) {
      const char* sink_error = nullptr;
      if (!luaAudioSinkBegin((uint32_t)info.hz, &sink, &sink_error)) {
        result = LuaAudioRunResult::Error;
        run_error = sink_error ? sink_error : "audio unavailable";
        break;
      }
      source_rate = (uint32_t)info.hz;
    } else if (source_rate != (uint32_t)info.hz) {
      result = LuaAudioRunResult::Error;
      run_error = "sample rate changed";
      break;
    }

    decoded_any = true;
    for (int i = 0; i < samples; ++i) {
      int32_t sample = info.channels == 2
        ? ((int32_t)work->pcm[2 * i] + work->pcm[2 * i + 1]) / 2
        : work->pcm[i];
      sample = (int32_t)((float)sample * sink.gain);
      if (sample > 32767) sample = 32767;
      else if (sample < -32768) sample = -32768;

      resample_phase += sink.output_rate;
      while (resample_phase >= source_rate) {
        output[output_count++] = (int16_t)sample;
        resample_phase -= source_rate;
        if (output_count == sizeof output / sizeof output[0]) {
          if (!luaAudioSinkWrite(&sink, output, output_count)) {
            result = LuaAudioRunResult::Error;
            run_error = "output failed";
            interrupted = true;
            break;
          }
          output_count = 0;
        }
      }
      if (interrupted) break;
    }
  }

  if (!interrupted && result == LuaAudioRunResult::Ended && output_count &&
      !luaAudioSinkWrite(&sink, output, output_count)) {
    result = LuaAudioRunResult::Error;
    run_error = "output failed";
  }

  luaAudioSinkEnd(&sink);
  s_wada_mp3_scratch = nullptr;
  heap_caps_free(work);
  file.close();
  __atomic_store_n(&s_lua_audio_active, false, __ATOMIC_RELEASE);

  if (result == LuaAudioRunResult::Ended)
    luaAudioSetState(command.owner, LuaAudioState::Ended);
  else if (result == LuaAudioRunResult::Stopped || result == LuaAudioRunResult::Released)
    luaAudioSetState(command.owner, LuaAudioState::Stopped);
  else if (result == LuaAudioRunResult::Error)
    luaAudioSetState(command.owner, LuaAudioState::Error,
                     run_error ? run_error : "playback failed");
  return result;
}

static void luaAudioWorker(void*) {
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  bool release = false;
  while (!release) {
    LuaAudioCommand command;
    if (xQueueReceive(s_lua_audio_queue, &command, portMAX_DELAY) != pdTRUE) continue;
    if (command.kind == LuaAudioCommandKind::Release) {
      luaAudioSetState(command.owner, LuaAudioState::Stopped);
      break;
    }
    if (command.kind == LuaAudioCommandKind::Stop) {
      luaAudioSetState(command.owner, LuaAudioState::Stopped);
      continue;
    }
    if (command.kind != LuaAudioCommandKind::Play) continue;

    for (;;) {
      LuaAudioCommand replacement;
      const LuaAudioRunResult result = command.format == LuaAudioFormat::Mp3
        ? luaAudioRunMp3Track(command, &replacement)
        : luaAudioRunTrack(command, &replacement);
      if (result == LuaAudioRunResult::Deferred) {
        // Keep the captured track while a mount/format owns admission. Still
        // process Stop/Release/replace so a closed volume cannot park teardown.
        LuaAudioRunResult control = LuaAudioRunResult::Deferred;
        if (luaAudioPollControl(command, &replacement, &control)) {
          __atomic_fetch_sub(&s_lua_audio_storage_pending, 1u, __ATOMIC_ACQ_REL);
          if (control == LuaAudioRunResult::Replaced) { command = replacement; continue; }
          luaAudioSetState(command.owner, LuaAudioState::Stopped);
          release = control == LuaAudioRunResult::Released;
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
        continue;
      }
      if (result == LuaAudioRunResult::Replaced) {
        command = replacement;
        continue;
      }
      release = result == LuaAudioRunResult::Released;
      break;
    }
  }

  __atomic_store_n(&s_lua_audio_active, false, __ATOMIC_RELEASE);
  __atomic_store_n(&s_lua_audio_storage_pending, 0u, __ATOMIC_RELEASE);
  __atomic_store_n(&s_lua_audio_storage_active, false, __ATOMIC_RELEASE);
  xQueueReset(s_lua_audio_queue);
  portENTER_CRITICAL(&s_lua_audio_mux);
  s_lua_audio_task = nullptr;
  s_lua_audio_task_state = LuaAudioTaskState::Stopped;
  portEXIT_CRITICAL(&s_lua_audio_mux);
  vTaskDelete(nullptr);
}

static bool luaAudioReturnError(char* error, size_t cap, const char* message) {
  luaAudioCopy(error, cap, message);
  return false;
}

static bool luaAudioPathEndsWith(const char* path, const char* suffix) {
  if (!path || !suffix) return false;
  const size_t path_len = strlen(path), suffix_len = strlen(suffix);
  if (path_len < suffix_len) return false;
  path += path_len - suffix_len;
  for (size_t i = 0; i < suffix_len; ++i) {
    char a = path[i], b = suffix[i];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}
}  // namespace

bool play(fs::FS* fs, const char* path, const char* shown, const char* source,
                      uint32_t owner, char* error, size_t error_cap) {
  if (!fs) return luaAudioReturnError(error, error_cap, "no storage");
  if (!path || !path[0] || strlen(path) >= 224)
    return luaAudioReturnError(error, error_cap, "bad path");
  if ((!host.muted || host.muted()) || touchPrefsGetSoundVolume() == 0)
    return luaAudioReturnError(error, error_cap, "muted");

  ui::platform::StorageLease storage;
  if (!storage.acquired()) return luaAudioReturnError(error, error_cap, "storage busy");
  File probe = fs->open(path, "r");
  if (!probe || probe.isDirectory()) {
    if (probe) probe.close();
    return luaAudioReturnError(error, error_cap, "not found");
  }
  uint16_t channels = 0;
  uint32_t rate = 0, data_len = 0;
  LuaAudioFormat format;
  bool supported = false;
  if (luaAudioPathEndsWith(shown, ".wav")) {
    format = LuaAudioFormat::Wav;
    supported = wavParse(probe, &channels, &rate, &data_len) && data_len > 0;
  } else if (luaAudioPathEndsWith(shown, ".mp3")) {
    format = LuaAudioFormat::Mp3;
    supported = probe.size() > 0;
  }
  probe.close();
  storage.release();
  if (!supported) return luaAudioReturnError(error, error_cap, "unsupported format");

  if (!s_lua_audio_queue) s_lua_audio_queue = xQueueCreate(4, sizeof(LuaAudioCommand));
  if (!s_lua_audio_queue) return luaAudioReturnError(error, error_cap, "out of memory");

  bool start_task = false;
  portENTER_CRITICAL(&s_lua_audio_mux);
  if (s_lua_audio_task_state == LuaAudioTaskState::Stopped) {
    s_lua_audio_task_state = LuaAudioTaskState::Starting;
    start_task = true;
  } else if (s_lua_audio_task_state != LuaAudioTaskState::Running) {
    portEXIT_CRITICAL(&s_lua_audio_mux);
    return luaAudioReturnError(error, error_cap, "busy");
  }
  portEXIT_CRITICAL(&s_lua_audio_mux);

  TaskHandle_t task = nullptr;
  if (start_task) {
    if (xTaskCreate(luaAudioWorker, "lua-audio", 8192, nullptr, 3, &task) != pdPASS) {
      portENTER_CRITICAL(&s_lua_audio_mux);
      s_lua_audio_task_state = LuaAudioTaskState::Stopped;
      portEXIT_CRITICAL(&s_lua_audio_mux);
      return luaAudioReturnError(error, error_cap, "out of memory");
    }
    portENTER_CRITICAL(&s_lua_audio_mux);
    s_lua_audio_task = task;
    s_lua_audio_task_state = LuaAudioTaskState::Running;
    portEXIT_CRITICAL(&s_lua_audio_mux);
  }

  LuaAudioCommand command;
  command.kind = LuaAudioCommandKind::Play;
  command.fs = fs;
  command.owner = owner;
  command.format = format;
  luaAudioCopy(command.path, sizeof command.path, path);
  luaAudioCopy(command.shown, sizeof command.shown, shown);
  luaAudioCopy(command.source, sizeof command.source, source);
  __atomic_fetch_add(&s_lua_audio_storage_pending, 1u, __ATOMIC_ACQ_REL);
  if (xQueueSend(s_lua_audio_queue, &command, 0) != pdPASS) {
    __atomic_fetch_sub(&s_lua_audio_storage_pending, 1u, __ATOMIC_ACQ_REL);
    if (start_task) xTaskNotifyGive(task);
    return luaAudioReturnError(error, error_cap, "busy");
  }
  luaAudioSetTrackStatus(command, LuaAudioState::Playing);
  if (start_task) xTaskNotifyGive(task);
  return true;
}

bool pause(uint32_t owner, bool pause) {
  LuaAudioState state;
  TaskHandle_t task;
  LuaAudioTaskState task_state;
  portENTER_CRITICAL(&s_lua_audio_mux);
  state = s_lua_audio_status.owner == owner ? s_lua_audio_status.state : LuaAudioState::Stopped;
  task = s_lua_audio_task;
  task_state = s_lua_audio_task_state;
  portEXIT_CRITICAL(&s_lua_audio_mux);
  if (!task || task_state != LuaAudioTaskState::Running ||
      (pause ? state != LuaAudioState::Playing : state != LuaAudioState::Paused)) return false;
  LuaAudioCommand command;
  command.kind = pause ? LuaAudioCommandKind::Pause : LuaAudioCommandKind::Resume;
  command.owner = owner;
  return xQueueSend(s_lua_audio_queue, &command, 0) == pdPASS;
}

bool stop(uint32_t owner, bool release) {
  TaskHandle_t task;
  LuaAudioTaskState task_state;
  portENTER_CRITICAL(&s_lua_audio_mux);
  task = s_lua_audio_task;
  task_state = s_lua_audio_task_state;
  if (release && task && task_state == LuaAudioTaskState::Running)
    s_lua_audio_task_state = LuaAudioTaskState::Stopping;
  portEXIT_CRITICAL(&s_lua_audio_mux);
  if (!task || task_state == LuaAudioTaskState::Stopped) {
    luaAudioSetState(owner, LuaAudioState::Stopped);
    return true;
  }
  if (task_state != LuaAudioTaskState::Running) return false;

  LuaAudioCommand command;
  command.kind = release ? LuaAudioCommandKind::Release : LuaAudioCommandKind::Stop;
  command.owner = owner;
  const BaseType_t queued = release
    ? xQueueSendToFront(s_lua_audio_queue, &command, pdMS_TO_TICKS(100))
    : xQueueSend(s_lua_audio_queue, &command, 0);
  if (queued != pdPASS) {
    if (release) {
      portENTER_CRITICAL(&s_lua_audio_mux);
      if (s_lua_audio_task_state == LuaAudioTaskState::Stopping)
        s_lua_audio_task_state = LuaAudioTaskState::Running;
      portEXIT_CRITICAL(&s_lua_audio_mux);
    }
    return false;
  }
  if (!release) return true;

  for (int i = 0; i < 160; ++i) {
    vTaskDelay(pdMS_TO_TICKS(5));
    portENTER_CRITICAL(&s_lua_audio_mux);
    task = s_lua_audio_task;
    portEXIT_CRITICAL(&s_lua_audio_mux);
    if (!task) return true;
  }
  return false;
}

void status(uint32_t owner, char* state, size_t state_cap,
                        char* path, size_t path_cap, char* source, size_t source_cap,
                        char* format, size_t format_cap, char* error, size_t error_cap) {
  LuaAudioStatus status;
  portENTER_CRITICAL(&s_lua_audio_mux);
  status = s_lua_audio_status;
  portEXIT_CRITICAL(&s_lua_audio_mux);
  if (status.owner != owner) status = LuaAudioStatus();

  const char* state_name = "stopped";
  if (status.state == LuaAudioState::Playing) state_name = "playing";
  else if (status.state == LuaAudioState::Paused) state_name = "paused";
  else if (status.state == LuaAudioState::Ended) state_name = "ended";
  else if (status.state == LuaAudioState::Error) state_name = "error";
  luaAudioCopy(state, state_cap, state_name);
  luaAudioCopy(path, path_cap, status.path);
  luaAudioCopy(source, source_cap, status.source);
  luaAudioCopy(format, format_cap, status.format);
  luaAudioCopy(error, error_cap, status.error);
}
#endif

bool notificationActive() {
#if defined(HAS_TDECK_GT911) || defined(TLORA_PAGER)
  return s_notify_playing;
#elif defined(HAS_TDISPLAY_P4)
  return s_p4_snd_playing;
#elif defined(HELTEC_V4_BUZZER_PIN) || defined(THINKNODE_M9_BUZZER_PIN)
  return s_v4_beep_playing;
#else
  return false;
#endif
}
#if !CAP_LUA_AUDIO
bool storageBusy() { return false; }
#endif
#if !defined(HAS_TDECK_GT911) && !defined(TLORA_PAGER)
bool supportsWav(const char*) { return false; }
#endif
uint8_t volume() {
#if defined(HAS_TANMATSU) || defined(HAS_TDISPLAY_P4)
  return s_volume_pct;
#else
  return touchPrefsGetSoundVolume();
#endif
}
} }
#if CAP_LUA_AUDIO
// Preserve the external Lua host ABI; its implementation belongs to the service.
bool luaHostAudioPlay(fs::FS* fs, const char* path, const char* shown, const char* source,
                      uint32_t owner, char* error, size_t capacity) {
  return ui::audio::play(fs, path, shown, source, owner, error, capacity);
}
bool luaHostAudioPause(uint32_t owner, bool paused) { return ui::audio::pause(owner, paused); }
bool luaHostAudioStop(uint32_t owner, bool release) { return ui::audio::stop(owner, release); }
void luaHostAudioStatus(uint32_t owner, char* state, size_t sc, char* path, size_t pc,
                        char* source, size_t src, char* format, size_t fc, char* error, size_t ec) {
  ui::audio::status(owner, state, sc, path, pc, source, src, format, fc, error, ec);
}
#endif

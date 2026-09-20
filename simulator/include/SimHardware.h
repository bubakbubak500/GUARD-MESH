// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Offline HAL: allocation/timing use the host, hardware jobs are unavailable.
// In particular, task creation must fail rather than execute an RF/SD worker.
#include "FS.h"
#define MALLOC_CAP_INTERNAL 0
#define MALLOC_CAP_8BIT 0
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_DMA 0
#define RTC_NOINIT_ATTR
#define ESP_OK 0
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
#define M_PI 3.14159265358979323846
#define WIRE_DBG(...) ((void)0)
#define PIN_I2S_BCK 7
#define PIN_I2S_WS 5
#define PIN_I2S_DOUT 6
#define PIN_SD_CS 39
#define PIN_TFT_CS 12
#define PIN_TFT_DC 11
#define PIN_TFT_SCL 40
#define PIN_TFT_SDA 41
#define MSBFIRST 0
#define SPI_MODE0 0
#define MC_FM_FAT32 2
void simRequestRestart();
using UBaseType_t = unsigned int;
using TickType_t = uint32_t;
using StackType_t = uint32_t;
inline void *heap_caps_malloc(size_t n, int) {
  return malloc(n);
}
inline void *heap_caps_calloc(size_t n, size_t s, int) {
  return calloc(n, s);
}
inline void heap_caps_free(void *p) {
  free(p);
}
inline size_t heap_caps_get_free_size(int) {
  return 64 * 1024 * 1024;
}
inline size_t heap_caps_get_total_size(int) {
  return 128 * 1024 * 1024;
}
inline size_t heap_caps_get_largest_free_block(int) {
  return 32 * 1024 * 1024;
}
inline void vTaskDelete(void *) {}
inline void vTaskDelay(unsigned n) {
  delay(n);
}
inline unsigned uxTaskGetStackHighWaterMark(void *) {
  return 64 * 1024;
}
inline int xTaskCreate(void (*)(void *), const char *, int, void *, int, void *) {
  return 0;
}
inline void feedLoopWDT() {}
inline void wdtHeavyBegin() {}
inline void wdtHeavyEnd() {}
struct LoopWdtGuard {};
struct WdtHeavyGuard {};
using i2s_port_t = int;
using i2s_mode_t = int;
enum {
  I2S_NUM_0,
  I2S_MODE_MASTER,
  I2S_MODE_TX,
  I2S_BITS_PER_SAMPLE_16BIT,
  I2S_CHANNEL_FMT_ONLY_LEFT,
  I2S_COMM_FORMAT_STAND_I2S,
  I2S_PIN_NO_CHANGE
};
struct i2s_config_t {
  int mode, sample_rate, bits_per_sample, channel_format, communication_format, intr_alloc_flags,
      dma_buf_count, dma_buf_len;
  bool use_apll, tx_desc_auto_clear;
  int fixed_mclk;
};
struct i2s_pin_config_t {
  int mck_io_num, bck_io_num, ws_io_num, data_out_num, data_in_num;
};
inline int i2s_driver_install(int, void *, int, void *) {
  return -1;
}
inline int i2s_set_pin(int, void *) {
  return -1;
}
inline int i2s_driver_uninstall(int) {
  return 0;
}
inline int i2s_zero_dma_buffer(int) {
  return 0;
}
inline int i2s_write(int, const void *, size_t, size_t *done, unsigned) {
  *done = 0;
  return -1;
}
#define HSPI 0
struct SPISettings {
  SPISettings(int, int, int) {}
};
struct SPIClass {
  SPIClass(int = 0) {}
  void begin(int, int, int, int) {}
  void end() {}
  void setFrequency(int) {}
  void setBitOrder(int) {}
  void setDataMode(int) {}
  void beginTransaction(SPISettings) {}
  void endTransaction() {}
  uint8_t transfer(uint8_t) { return 0; }
};
template <class... A> bool sdTryFastClock(A...) {
  return false;
}
template <class... A> bool sdcard_init(A...) {
  return false;
}
inline SPIClass *tdeckSharedSPI() {
  static SPIClass spi;
  return &spi;
}
inline int sd_fs(uint8_t) {
  return -1;
}
inline int sd_read_raw(uint8_t, uint8_t *, uint32_t) {
  return -1;
}
inline int sd_write_raw(uint8_t, const uint8_t *, uint32_t) {
  return -1;
}
inline void sdcard_uninit(uint8_t) {}
inline int f_mkfs(const char *, uint8_t, uint32_t, void *, unsigned) {
  return -1;
}
struct SimEsp {
  void restart() { simRequestRestart(); }
  unsigned getFreePsram() { return 64 * 1024 * 1024; }
  unsigned getFreeHeap() { return 64 * 1024 * 1024; }
  unsigned getPsramSize() { return 64 * 1024 * 1024; }
  unsigned getHeapSize() { return 64 * 1024 * 1024; }
  unsigned getFlashChipSize() { return 16 * 1024 * 1024; }
};
inline SimEsp ESP;
inline int getCpuFrequencyMhz() {
  return 0;
}
inline void delayMicroseconds(unsigned n) {
  std::this_thread::sleep_for(std::chrono::microseconds(n));
}
inline size_t strlcpy(char *d, const char *s, size_t n) {
  size_t len = strlen(s);
  if (n) {
    size_t k = std::min(len, n - 1);
    memcpy(d, s, k);
    d[k] = 0;
  }
  return len;
}
inline tm *localtime_r(const time_t *t, tm *result) {
  localtime_s(result, t);
  return result;
}
inline int setenv(const char *k, const char *v, int) {
  return _putenv_s(k, v);
}
#define WL_CONNECTED 3
#define WIFI_OFF 0
#define WIFI_CONFIG_SSID_MAX 33
#define WIFI_CONFIG_PWD_MAX 65
struct SimIP {
  operator uint32_t() const { return 0; }
  String toString() const { return "0.0.0.0"; }
};
struct SimWifi {
  int status() { return 0; }
  SimIP localIP() { return {}; }
  String SSID() { return ""; }
  int RSSI() { return 0; }
  int getMode() { return WIFI_OFF; }
};
inline SimWifi WiFi;
inline bool wifiScanIsActive() {
  return false;
}
inline void wifiConfigGetSsid(char *b, size_t n) {
  if (n)
    b[0] = 0;
}
inline bool wifiConfigSetSsid(const char *) {
  return false;
}
inline bool wifiConfigSetPwd(const char *) {
  return false;
}
inline void wifiConfigClear() {}
inline void wifiConfigRequestApply() {}
inline void wifiConfigSetRadioEnabled(bool) {}

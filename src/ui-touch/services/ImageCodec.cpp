// SPDX-License-Identifier: GPL-3.0-or-later
#include "ImageCodec.h"
#include "../platform/UiDevice.h"
#include "src/extra/libs/sjpg/tjpgd.h"
namespace ui { namespace images {
static char s_jpgScaleErr[52] = {};
const char* lastError() { return s_jpgScaleErr; }
void clearError() { s_jpgScaleErr[0] = '\0'; }
// Minimal BMP -> RGB565 decoder for the image viewer. Handles the common
// uncompressed layouts: 16-bpp 565 (what our screenshots write) and 24-bpp BGR,
// both bottom-up and top-down. Returns an lvglPsramAlloc'd buffer (freed via
// lvglPsramFree), or nullptr on anything unsupported.
uint8_t* decodeBmpToRgb565(const uint8_t* bmp, size_t len, int* out_w, int* out_h) {
  if (!bmp || !out_w || !out_h || len < 54 || bmp[0] != 'B' || bmp[1] != 'M') return nullptr;
  auto rd32 = [&](size_t o) -> uint32_t {
    return (uint32_t)bmp[o] | ((uint32_t)bmp[o+1] << 8) | ((uint32_t)bmp[o+2] << 16) | ((uint32_t)bmp[o+3] << 24);
  };
  auto rd16 = [&](size_t o) -> uint16_t { return (uint16_t)(bmp[o] | (bmp[o+1] << 8)); };
  const uint32_t data_off = rd32(10);
  const int32_t  w        = (int32_t)rd32(18);
  const int32_t  h_raw    = (int32_t)rd32(22);
  const uint16_t bpp      = rd16(28);
  const uint32_t comp     = rd32(30);
  if (w <= 0 || w > 1024) return nullptr;
  if (h_raw == INT32_MIN) return nullptr;
  const bool    top_down = (h_raw < 0);
  const int32_t h        = top_down ? -h_raw : h_raw;
  if (h <= 0 || h > 1024)        return nullptr;
  if (bpp != 16 && bpp != 24)    return nullptr;
  if (comp != 0 && comp != 3)    return nullptr;          // 0 BI_RGB, 3 BI_BITFIELDS
  const uint32_t row_bytes = (((uint32_t)w * bpp + 31) / 32) * 4;
  if ((uint64_t)data_off + (uint64_t)row_bytes * (uint32_t)h > len) return nullptr;
  uint16_t* out = (uint16_t*)lvglPsramAlloc((size_t)w * (size_t)h * sizeof(uint16_t));
  if (!out) return nullptr;
  for (int32_t y = 0; y < h; ++y) {
    const int32_t  srcrow = top_down ? y : (h - 1 - y);   // BMP rows are bottom-up by default
    const uint8_t* row    = bmp + data_off + (size_t)srcrow * row_bytes;
    uint16_t*      dst    = out + (size_t)y * w;
    if (bpp == 16) {
      memcpy(dst, row, (size_t)w * 2);                    // assume RGB565 (our screenshots)
    } else {
      for (int32_t x = 0; x < w; ++x)                     // 24-bpp stored BGR
        dst[x] = lv_color_make(row[x*3+2], row[x*3+1], row[x*3+0]).full;
    }
  }
  *out_w = (int)w; *out_h = (int)h;
  return (uint8_t*)out;
}


// Decode a JPEG (in-memory) into a freshly-PSRAM-alloced RGB565 buffer.
// Returns NULL on failure. Caller owns the buffer; lvglPsramFree() it.
// The decode is one-shot via LVGL's image decoder API (which routes to
// SJPG's tinyjpeg backend since data starts with 0xFFD8).
uint8_t* decodeJpegToRgb565(const uint8_t* jpeg, size_t jpeg_len,
                                    int* out_w, int* out_h) {
  if (!jpeg || !jpeg_len || !out_w || !out_h) return nullptr;
  lv_img_dsc_t src;
  memset(&src, 0, sizeof(src));
  src.header.cf = LV_IMG_CF_RAW;
  src.data      = jpeg;
  src.data_size = jpeg_len;

  lv_img_decoder_dsc_t dec;
  memset(&dec, 0, sizeof(dec));
  // Color arg is only used by the alpha-only-image decoders to colorize
  // the alpha channel. We're decoding RGB JPEGs; it's ignored.
  if (lv_img_decoder_open(&dec, &src, lv_color_make(0, 0, 0), 0) != LV_RES_OK) {
    return nullptr;
  }
  const int w = dec.header.w;
  const int h = dec.header.h;
  if (w <= 0 || h <= 0 || w > 1024 || h > 1024) {
    lv_img_decoder_close(&dec);
    return nullptr;
  }
  const size_t buf_size = (size_t)w * h * sizeof(lv_color_t);  // RGB565 out
  uint8_t* buf = (uint8_t*)lvglPsramAlloc(buf_size);
  if (!buf) {
    lv_img_decoder_close(&dec);
    return nullptr;
  }
  // The decoder output format depends on the codec:
  //   • SJPG (JPEG)  → LV_IMG_CF_TRUE_COLOR       = 2 B/px (pure RGB565)
  //   • lv_png (PNG) → LV_IMG_CF_TRUE_COLOR_ALPHA = 3 B/px at depth 16
  //                    (RGB565 low, RGB565 high, alpha)
  // Our output buffer is pure RGB565 (2 B/px) and the lv_img widget is
  // tagged LV_IMG_CF_TRUE_COLOR, so for PNG we must drop the per-pixel
  // alpha byte. Copying the raw 3 B/px stream as if it were 2 B/px is
  // exactly what produced the RGB565 "static / noise" tiles.
  if (dec.img_data) {
    if (dec.header.cf == LV_IMG_CF_TRUE_COLOR_ALPHA) {
      const uint8_t* s = (const uint8_t*)dec.img_data;
      const size_t px = (size_t)w * h;
      for (size_t i = 0; i < px; ++i) {
        buf[i * 2 + 0] = s[i * 3 + 0];   // RGB565 low
        buf[i * 2 + 1] = s[i * 3 + 1];   // RGB565 high  (alpha s[i*3+2] dropped)
      }
    } else {
      memcpy(buf, dec.img_data, buf_size);   // already RGB565
    }
  } else {
    // Streaming fallback (unused by SJPG for standalone JPEGs, but
    // harmless to keep).
    for (int y = 0; y < h; ++y) {
      if (lv_img_decoder_read_line(&dec, 0, y, w,
              buf + (size_t)y * w * sizeof(lv_color_t)) != LV_RES_OK) {
        lvglPsramFree(buf);
        lv_img_decoder_close(&dec);
        return nullptr;
      }
    }
  }
  lv_img_decoder_close(&dec);
  *out_w = w;
  *out_h = h;
  return buf;
}

// Downscaling JPEG decode straight through tjpgd (LVGL's SJPG wrapper decodes at
// full resolution only). Picks a 1/1..1/8 scale so the output fits within max_dim,
// then converts tjpgd's RGB888 blocks to LVGL RGB565. Lets the file-manager viewer
// and the lock wallpaper accept images bigger than 1024 px without a multi-MB
// full-res buffer.

struct TjpgIo { const uint8_t* data; size_t len; size_t pos; uint16_t* out; int ow; int oh; };
extern "C" {
static size_t tjpgInCb(JDEC* jd, uint8_t* buf, size_t n) {
  TjpgIo* io = (TjpgIo*)jd->device;
  size_t avail = io->len - io->pos;
  if (n > avail) n = avail;
  if (buf) memcpy(buf, io->data + io->pos, n);   // buf==NULL: skip n bytes
  io->pos += n;
  return n;
}
static int tjpgOutCb(JDEC* jd, void* bitmap, JRECT* r) {
  TjpgIo* io = (TjpgIo*)jd->device;
  const uint8_t* s = (const uint8_t*)bitmap;     // RGB888, row-major within the rect
  for (int y = (int)r->top; y <= (int)r->bottom; ++y) {
    uint16_t* d = (y < io->oh) ? (io->out + (size_t)y * io->ow + (int)r->left) : nullptr;
    for (int x = (int)r->left; x <= (int)r->right; ++x) {
      if (d && x < io->ow) { lv_color_t c = lv_color_make(s[0], s[1], s[2]); *d++ = c.full; }
      s += 3;
    }
  }
  return 1;   // continue
}
}  // extern "C"
uint8_t* decodeJpegScaledToRgb565(const uint8_t* jpeg, size_t jpeg_len,
                                         int* out_w, int* out_h, int max_dim,
                                         uint8_t* dst_buf, size_t dst_cap) {
  if (!jpeg || !jpeg_len || !out_w || !out_h) return nullptr;
  s_jpgScaleErr[0] = '\0';
  // tjpgd work area (~3.1 KB used). Lazy PSRAM (internal fallback), cached — keeps
  // this ~4 KB out of scarce internal DRAM. JD_FASTDECODE=0; matches LVGL's TJPGD_WORKBUFF_SIZE.
  static const size_t kJpgWork = 4096;
  static uint8_t* pool = nullptr;
  if (!pool) { pool = (uint8_t*)heap_caps_malloc(kJpgWork, MALLOC_CAP_SPIRAM);
               if (!pool) pool = (uint8_t*)heap_caps_malloc(kJpgWork, MALLOC_CAP_8BIT); }
  if (!pool) { snprintf(s_jpgScaleErr, sizeof s_jpgScaleErr, "Out of memory."); return nullptr; }
  TjpgIo io; io.data = jpeg; io.len = jpeg_len; io.pos = 0; io.out = nullptr; io.ow = 0; io.oh = 0;
  JDEC jd;
  JRESULT rp = jd_prepare(&jd, tjpgInCb, pool, kJpgWork, &io);
  if (rp != JDR_OK) {
    if (rp == JDR_FMT3 || rp == JDR_FMT2)   // tjpgd can't decode progressive / unusual-sampling JPEGs
      snprintf(s_jpgScaleErr, sizeof s_jpgScaleErr, "Progressive JPEG.\nRe-save as a standard JPEG.");
    else
      snprintf(s_jpgScaleErr, sizeof s_jpgScaleErr, "Decode error %d.", (int)rp);
    WIRE_DBG("[JPG] jd_prepare rc=%d\n", (int)rp);
    return nullptr;
  }
  int scale = 0;
  while (scale < 3 && (((int)jd.width >> scale) > max_dim || ((int)jd.height >> scale) > max_dim)) scale++;
  const int ow = (int)jd.width >> scale, oh = (int)jd.height >> scale;
  WIRE_DBG("[JPG] %ux%u -> 1/%d -> %dx%d (cap %d)\n", jd.width, jd.height, 1 << scale, ow, oh, max_dim);
  if (ow <= 0 || oh <= 0 || ow > max_dim || oh > max_dim) {
    snprintf(s_jpgScaleErr, sizeof s_jpgScaleErr, "%ux%u too big", jd.width, jd.height);
    return nullptr;
  }
  const size_t need = (size_t)ow * oh * sizeof(uint16_t);
  uint16_t* buf; bool owns;
  if (dst_buf) {                                  // caller-provided output (map tile pool)
    if (need > dst_cap) { snprintf(s_jpgScaleErr, sizeof s_jpgScaleErr, "%dx%d > buf", ow, oh); return nullptr; }
    buf = (uint16_t*)dst_buf; owns = false;
  } else {
    buf = (uint16_t*)lvglPsramAlloc(need); owns = true;
    if (!buf) { snprintf(s_jpgScaleErr, sizeof s_jpgScaleErr, "no memory (%dx%d)", ow, oh); return nullptr; }
  }
  io.out = buf; io.ow = ow; io.oh = oh;
  JRESULT rd = jd_decomp(&jd, tjpgOutCb, (uint8_t)scale);
  if (rd != JDR_OK) {
    snprintf(s_jpgScaleErr, sizeof s_jpgScaleErr, "decomp err %d", (int)rd);
    WIRE_DBG("[JPG] jd_decomp rc=%d\n", (int)rd);
    if (owns) lvglPsramFree(buf);
    return nullptr;
  }
  *out_w = ow; *out_h = oh;
  return (uint8_t*)buf;
}

// Decode a PNG to a PSRAM RGB565 buffer via lodepng DIRECTLY. LVGL's lv_png
// decoder produces RGB565 noise on this board, so we bypass it: lodepng ->
// RGBA8888 (its malloc lands in PSRAM thanks to the >4 KB SPIRAM-malloc
// threshold), then a manual RGBA->RGB565 using lv_color_make() so the display's
// colour format (incl. byte-swap) is honoured. This is what lets the SD map use
// standard /maps/osm/{z}/{x}/{y}.png tiles (the Meshtastic/MeshCore convention).
extern "C" unsigned lodepng_decode32(unsigned char** out, unsigned* w, unsigned* h,
                                      const unsigned char* in, size_t insize);
uint8_t* decodePngToRgb565(const uint8_t* png, size_t png_len, int* out_w, int* out_h,
                                  uint8_t* dst_buf, size_t dst_cap) {
  if (!png || !png_len || !out_w || !out_h) return nullptr;
  unsigned char* rgba = nullptr; unsigned w = 0, h = 0;
  if (lodepng_decode32(&rgba, &w, &h, png, png_len) != 0 || !rgba) { if (rgba) free(rgba); return nullptr; }
  if (w == 0 || h == 0 || w > 1024 || h > 1024) { free(rgba); return nullptr; }
  const size_t npx = (size_t)w * h;
  uint16_t* rgb; bool owns;
  if (dst_buf) {                                  // caller-provided output (map tile pool)
    if (npx * sizeof(uint16_t) > dst_cap) { free(rgba); return nullptr; }
    rgb = (uint16_t*)dst_buf; owns = false;
  } else {
    rgb = (uint16_t*)lvglPsramAlloc(npx * sizeof(uint16_t)); owns = true;
    if (!rgb) { free(rgba); return nullptr; }
  }
  (void)owns;
  for (size_t i = 0; i < npx; i++) rgb[i] = lv_color_make(rgba[i*4+0], rgba[i*4+1], rgba[i*4+2]).full;
  free(rgba);   // lodepng allocates with the system malloc/free
  *out_w = (int)w; *out_h = (int)h;
  return (uint8_t*)rgb;
}

} }

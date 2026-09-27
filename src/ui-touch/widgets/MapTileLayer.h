// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>
namespace ui { namespace widgets {
// UI-thread-owned decoded tile pool. Storage/network access is supplied by the
// host; the layer owns descriptors, widgets and pixel buffers. Parent deletion
// invalidates widgets before children are destroyed, so a later reset is safe.
class MapTileLayer {
public:
  struct Host {
    bool (*load)(uint8_t, int32_t, int32_t, uint8_t**, size_t*, bool*);
    void (*retry)(uint8_t, int32_t, int32_t, bool corrupt);
    unsigned (*pending)();
    void (*prepare)(uint16_t*, int, int);
  };
  struct View {
    double lat, lon;
    uint8_t zoom;
    int width, height, capacity, margin;
    bool night, progressive;
  };
  struct Result { int wanted = 0, placed = 0, missing = 0; };
  explicit MapTileLayer(Host host) : _host(host) {}
  ~MapTileLayer();
  MapTileLayer(const MapTileLayer&) = delete;
  MapTileLayer& operator=(const MapTileLayer&) = delete;
  Result render(lv_obj_t* parent, const View&);
  void configure(Host host) { clear(); _host = host; }
  void clear();
  int buffers() const;
private:
  static constexpr int capacity = 25;
  struct Tile {
    uint8_t z = 0;
    int32_t x = 0, y = 0;
    uint8_t* pixels = nullptr;
    lv_img_dsc_t descriptor{};
    lv_obj_t* image = nullptr;
  };
  Host _host;
  Tile* _tiles = nullptr;
  lv_obj_t* _parent = nullptr;
  bool _night = false;
  void release(Tile&, bool freePixels);
  void adopt(lv_obj_t*);
  static void parentDeleted(lv_event_t*);
  static void imageDeleted(lv_event_t*);
};
} }

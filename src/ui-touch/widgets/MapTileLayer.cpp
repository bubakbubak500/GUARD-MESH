// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapTileLayer.h"
#include "../platform/UiPlatform.h"
#include <LvglPsramAlloc.h>
#include "../models/MapProjection.h"
#include "../services/ImageCodec.h"
#include <cmath>
#include <new>
namespace ui { namespace widgets {
void MapTileLayer::imageDeleted(lv_event_t* event) {
  auto* tile = static_cast<Tile*>(lv_event_get_user_data(event));
  tile->image = nullptr;
}
void MapTileLayer::parentDeleted(lv_event_t* event) {
  auto* self = static_cast<MapTileLayer*>(lv_event_get_user_data(event));
  self->_parent = nullptr;
  // DELETE on the parent precedes child deletion. Child callbacks remain
  // installed and clear each image pointer when LVGL actually destroys it.
}
void MapTileLayer::release(Tile& tile, bool freePixels) {
  if (tile.image) lv_obj_del(tile.image);
  lv_img_cache_invalidate_src(&tile.descriptor);
  if (freePixels && tile.pixels) {
    lvglPsramFree(tile.pixels);
    tile.pixels = nullptr;
  }
}
void MapTileLayer::clear() {
  if (_tiles) for (int i = 0; i < capacity; ++i) release(_tiles[i], true);
}
MapTileLayer::~MapTileLayer() {
  clear();
  if (_parent) lv_obj_remove_event_cb_with_user_data(_parent, parentDeleted, this);
  if (_tiles) {
    for (int i = 0; i < capacity; ++i) _tiles[i].~Tile();
    lvglPsramFree(_tiles);
  }
}
void MapTileLayer::adopt(lv_obj_t* parent) {
  if (parent == _parent) return;
  clear();
  if (_parent) lv_obj_remove_event_cb_with_user_data(_parent, parentDeleted, this);
  _parent = parent;
  if (parent) lv_obj_add_event_cb(parent, parentDeleted, LV_EVENT_DELETE, this);
}
int MapTileLayer::buffers() const {
  int count = 0;
  if (_tiles) for (int i = 0; i < capacity; ++i) if (_tiles[i].pixels) ++count;
  return count;
}
MapTileLayer::Result MapTileLayer::render(lv_obj_t* parent, const View& view) {
  Result result;
  adopt(parent);
  if (!parent || !_host.load || view.width <= 0 || view.height <= 0 || view.capacity <= 0) return result;
  if (!_tiles) {
    _tiles = static_cast<Tile*>(lvglPsramAlloc(sizeof(Tile) * capacity));
    if (!_tiles) return result;
    for (int i = 0; i < capacity; ++i) new (&_tiles[i]) Tile();
  }
  if (_night != view.night) {
    for (int i = 0; i < capacity; ++i) release(_tiles[i], false);
    _night = view.night;
  }
  const int cap = view.capacity < capacity ? view.capacity : capacity;
  double wx, wy;
  maps::latLonToWorldPx(view.lat, view.lon, view.zoom, &wx, &wy);
  const int32_t cx = static_cast<int32_t>(std::floor(wx / 256.0));
  const int32_t cy = static_cast<int32_t>(std::floor(wy / 256.0));
  struct Wanted { int32_t x, y; double distance; bool placed; } wanted[capacity];
  int count = 0;
  for (int dy = -maps::gridRadius(view.height); dy <= maps::gridRadius(view.height); ++dy) {
    for (int dx = -maps::gridRadius(view.width); dx <= maps::gridRadius(view.width); ++dx) {
      const int32_t x = cx + dx, y = cy + dy;
      const int sx = static_cast<int>(x * 256.0 - wx + view.width / 2);
      const int sy = static_cast<int>(y * 256.0 - wy + view.height / 2);
      if (sx + 256 <= -view.margin || sx >= view.width + view.margin ||
          sy + 256 <= -view.margin || sy >= view.height + view.margin) continue;
      const double mx = sx + 128 - view.width / 2, my = sy + 128 - view.height / 2;
      wanted[count++] = {x, y, mx * mx + my * my, false};
    }
  }
  if (count > cap) {
    for (int a = 0; a < cap; ++a) {
      int best = a;
      for (int b = a + 1; b < count; ++b) if (wanted[b].distance < wanted[best].distance) best = b;
      if (best != a) { auto swap = wanted[a]; wanted[a] = wanted[best]; wanted[best] = swap; }
    }
    count = cap;
  }
  result.wanted = count;
  auto position = [&](Tile& tile) {
    lv_obj_set_pos(tile.image, static_cast<lv_coord_t>(tile.x * 256.0 - wx + view.width / 2),
                              static_cast<lv_coord_t>(tile.y * 256.0 - wy + view.height / 2));
  };
  for (int i = 0; i < capacity; ++i) {
    auto& tile = _tiles[i];
    if (!tile.image) continue;
    bool keep = false;
    for (int k = 0; k < count; ++k) {
      if (!wanted[k].placed && tile.z == view.zoom && tile.x == wanted[k].x && tile.y == wanted[k].y) {
        wanted[k].placed = keep = true; position(tile); break;
      }
    }
    if (!keep) release(tile, false);
  }
  // A smaller caller budget must also shrink buffers retained from an earlier
  // frame; a cap on new allocations alone does not bound the existing pool.
  int allocated = buffers();
  for (int i = 0; i < capacity && allocated > cap; ++i) {
    if (!_tiles[i].image && _tiles[i].pixels) { release(_tiles[i], true); --allocated; }
  }
  constexpr size_t bytes = 256u * 256u * sizeof(lv_color_t);
  uint32_t lastPaint = 0;
  bool painted = false;
  for (int k = 0; k < count; ++k) {
    if (wanted[k].placed) { ++result.placed; continue; }
    Tile* tile = nullptr;
    for (int i = 0; i < capacity; ++i) if (!_tiles[i].image && _tiles[i].pixels) { tile = &_tiles[i]; break; }
    if (!tile) for (int i = 0; i < capacity; ++i) if (!_tiles[i].image) { tile = &_tiles[i]; break; }
    if (!tile) continue;
    if (!tile->pixels && allocated < cap) {
      tile->pixels = static_cast<uint8_t*>(lvglPsramAlloc(bytes));
      if (tile->pixels) ++allocated;
    }
    if (!tile->pixels) continue;
    uint8_t* encoded = nullptr; size_t size = 0; bool repairable = false;
    if (!_host.load(view.zoom, wanted[k].x, wanted[k].y, &encoded, &size, &repairable) || !encoded) {
      if (encoded) lvglPsramFree(encoded);
      if (_host.retry) _host.retry(view.zoom, wanted[k].x, wanted[k].y, false);
      continue;
    }
    int width = 0, height = 0;
    uint8_t* rgb = size >= 4 && encoded[0] == 0x89 && encoded[1] == 'P' && encoded[2] == 'N' && encoded[3] == 'G'
      ? images::decodePngToRgb565(encoded, size, &width, &height, tile->pixels, bytes)
      : images::decodeJpegScaledToRgb565(encoded, size, &width, &height, 256, tile->pixels, bytes);
    lvglPsramFree(encoded);
    if (!rgb) {
      if (repairable && _host.retry) _host.retry(view.zoom, wanted[k].x, wanted[k].y, true);
      continue;
    }
    if (view.night) for (int p = 0; p < width * height; ++p)
      reinterpret_cast<uint16_t*>(rgb)[p] = ~reinterpret_cast<uint16_t*>(rgb)[p];
    if (_host.prepare) _host.prepare(reinterpret_cast<uint16_t*>(rgb), width, height);
    tile->z = view.zoom; tile->x = wanted[k].x; tile->y = wanted[k].y;
    tile->descriptor = {};
    tile->descriptor.header.cf = LV_IMG_CF_TRUE_COLOR;
    tile->descriptor.header.w = width; tile->descriptor.header.h = height;
    tile->descriptor.data = rgb;
    tile->descriptor.data_size = width * height * sizeof(lv_color_t);
    lv_img_cache_invalidate_src(&tile->descriptor);
    tile->image = lv_img_create(parent);
    lv_obj_add_event_cb(tile->image, imageDeleted, LV_EVENT_DELETE, tile);
    lv_img_set_src(tile->image, &tile->descriptor);
    position(*tile); lv_obj_move_background(tile->image);
    ++result.placed;
    if (view.progressive && (!_host.pending || !_host.pending())) {
      if (!painted || static_cast<uint32_t>(platform::milliseconds() - lastPaint) >= 350) {
        lv_refr_now(nullptr); lastPaint = platform::milliseconds(); painted = true;
      }
    }
    platform::yieldUiWork();
  }
  result.missing = result.wanted - result.placed;
  return result;
}
} }

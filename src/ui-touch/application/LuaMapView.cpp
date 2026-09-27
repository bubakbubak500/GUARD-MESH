// SPDX-License-Identifier: GPL-3.0-or-later
#include "LuaMapView.h"
#include "../platform/UiDevice.h"
#include "../models/MapProjection.h"
#include <new>
namespace { ui::maps::LuaMapHost host{}; }
void ui::maps::configureLuaMap(LuaMapHost value) { host = value; }
#if CAP_LUA_SDK_EXT
using namespace ui::maps;
struct AppMapView {
  lv_obj_t* cont   = nullptr;   // clips to the app's requested rectangle
  lv_obj_t* tiles  = nullptr;   // tile images
  lv_obj_t* ovl    = nullptr;   // markers + lines, always above the tiles
  int       w = 0, h = 0;
  double    lat = 0, lon = 0;
  uint8_t   zoom = 13;
  int       placed = 0;         // tiles actually drawn at the last render
  ui::widgets::MapTileLayer layer{host.tiles};
};

void* luaHostMapCreate(int x, int y, int w, int h) {
  AppMapView* v = new (std::nothrow) AppMapView();
  if (!v) return nullptr;
  v->w = w; v->h = h;
  extern lv_obj_t* luaHostAppBody();
  lv_obj_t* body = luaHostAppBody();
  if (!body) { delete v; return nullptr; }
  v->cont = lv_obj_create(body);
  lv_obj_remove_style_all(v->cont);
  lv_obj_set_pos(v->cont, (lv_coord_t)x, (lv_coord_t)y);
  lv_obj_set_size(v->cont, (lv_coord_t)w, (lv_coord_t)h);
  lv_obj_set_style_bg_color(v->cont, lv_color_hex(0x11161B), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(v->cont, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(v->cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(v->cont, LV_OBJ_FLAG_CLICKABLE);
  // Two layers so overlays can never be painted over by a tile that happens to
  // be created later -- the exact bug the map tab fixes with move_background.
  v->tiles = lv_obj_create(v->cont);
  lv_obj_remove_style_all(v->tiles);
  lv_obj_set_pos(v->tiles, 0, 0);
  lv_obj_set_size(v->tiles, (lv_coord_t)w, (lv_coord_t)h);
  lv_obj_clear_flag(v->tiles, LV_OBJ_FLAG_SCROLLABLE);
  v->ovl = lv_obj_create(v->cont);
  lv_obj_remove_style_all(v->ovl);
  lv_obj_set_pos(v->ovl, 0, 0);
  lv_obj_set_size(v->ovl, (lv_coord_t)w, (lv_coord_t)h);
  lv_obj_clear_flag(v->ovl, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(v->cont, [](lv_event_t* event) {
    auto* view = static_cast<AppMapView*>(lv_event_get_user_data(event));
    view->cont = view->tiles = view->ovl = nullptr;
  }, LV_EVENT_DELETE, v);
  return v;
}

void luaHostMapDestroy(void* vp) {
  AppMapView* v = (AppMapView*)vp;
  if (!v) return;
  v->layer.clear();
  if (v->cont) lv_obj_del(v->cont);      // takes the two layers and every overlay with it
  delete v;
}

void luaHostMapSet(void* vp, double lat, double lon, int zoom) {
  AppMapView* v = (AppMapView*)vp;
  if (!v) return;
  v->lat = lat; v->lon = lon;
  if (zoom >= 1 && zoom <= 19) v->zoom = (uint8_t)zoom;
}
// Zoom alone. Separate from the call above because 0,0 is a real coordinate,
// so there is no lat/lon value that can mean "leave the centre alone".
void luaHostMapSetZoom(void* vp, int zoom) {
  AppMapView* v = (AppMapView*)vp;
  if (v && zoom >= 1 && zoom <= 19) v->zoom = (uint8_t)zoom;
}
int luaHostMapZoom(void* vp) { AppMapView* v = (AppMapView*)vp; return v ? v->zoom : 0; }
int luaHostMapPlaced(void* vp) { AppMapView* v = (AppMapView*)vp; return v ? v->placed : 0; }

// lat/lon -> pixel within the view. Deliberately NOT clamped: an app placing
// its own labels needs to know a point is off-view, and a clamped coordinate
// would silently pile everything onto the edge.
void luaHostMapToScreen(void* vp, double lat, double lon, int* px, int* py) {
  AppMapView* v = (AppMapView*)vp;
  if (!v) { *px = *py = 0; return; }
  double cwx, cwy, wx, wy;
  latLonToWorldPx(v->lat, v->lon, v->zoom, &cwx, &cwy);
  latLonToWorldPx(lat, lon, v->zoom, &wx, &wy);
  *px = (int)lround(wx - cwx + v->w / 2.0);
  *py = (int)lround(wy - cwy + v->h / 2.0);
}
void luaHostMapToLatLon(void* vp, int px, int py, double* lat, double* lon) {
  AppMapView* v = (AppMapView*)vp;
  if (!v) { *lat = *lon = 0; return; }
  double cwx, cwy;
  latLonToWorldPx(v->lat, v->lon, v->zoom, &cwx, &cwy);
  worldPxToLatLon(cwx + (px - v->w / 2.0), cwy + (py - v->h / 2.0), v->zoom, lat, lon);
}

void luaHostMapClearOverlay(void* vp) {
  AppMapView* v = (AppMapView*)vp;
  if (v && v->ovl) lv_obj_clean(v->ovl);
}

int luaHostMapMarker(void* vp, double lat, double lon, uint32_t color, int size) {
  AppMapView* v = (AppMapView*)vp;
  if (!v || !v->ovl) return 0;
  int px, py;
  luaHostMapToScreen(vp, lat, lon, &px, &py);
  if (size < 3) size = 3;
  if (size > 24) size = 24;
  // Off-view markers are dropped rather than created off-screen: they would be
  // invisible LVGL objects accumulating on every redraw of a panning map.
  if (px < -size || py < -size || px > v->w + size || py > v->h + size) return 0;
  lv_obj_t* d = lv_obj_create(v->ovl);
  lv_obj_remove_style_all(d);
  lv_obj_set_size(d, size, size);
  lv_obj_set_pos(d, px - size / 2, py - size / 2);
  lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(d, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(d, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(d, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(d, lv_color_hex(0x0E1216), LV_PART_MAIN);
  lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
  return 1;
}

int luaHostMapLine(void* vp, double la1, double lo1, double la2, double lo2,
                   uint32_t color, int width) {
  AppMapView* v = (AppMapView*)vp;
  if (!v || !v->ovl) return 0;
  int x1, y1, x2, y2;
  luaHostMapToScreen(vp, la1, lo1, &x1, &y1);
  luaHostMapToScreen(vp, la2, lo2, &x2, &y2);
  // Both ends off the same side means the segment cannot cross the view.
  if ((x1 < 0 && x2 < 0) || (y1 < 0 && y2 < 0) ||
      (x1 > v->w && x2 > v->w) || (y1 > v->h && y2 > v->h)) return 0;
  // LVGL does not copy a line's point array, so it has to outlive the object.
  // Attaching it as the object's user_data and freeing it on DELETE keeps the
  // lifetime tied to the widget instead of to a fixed slot table.
  lv_point_t* pts = (lv_point_t*)lv_mem_alloc(sizeof(lv_point_t) * 2);
  if (!pts) return 0;
  pts[0].x = (lv_coord_t)x1; pts[0].y = (lv_coord_t)y1;
  pts[1].x = (lv_coord_t)x2; pts[1].y = (lv_coord_t)y2;
  lv_obj_t* ln = lv_line_create(v->ovl);
  lv_line_set_points(ln, pts, 2);
  lv_obj_set_user_data(ln, pts);
  lv_obj_add_event_cb(ln, [](lv_event_t* e) {
    void* p = lv_obj_get_user_data(lv_event_get_target(e));
    if (p) lv_mem_free(p);
  }, LV_EVENT_DELETE, nullptr);
  lv_obj_set_style_line_color(ln, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_line_width(ln, width < 1 ? 1 : (width > 8 ? 8 : width), LV_PART_MAIN);
  lv_obj_set_style_line_opa(ln, LV_OPA_COVER, LV_PART_MAIN);
  return 1;
}

// Render the tiles covering the view. Same slot-reuse idea as the map tab: a
// small pan shares most tiles, so only newcomers are read and decoded.
void luaHostMapRender(void* vp) {
  auto* v = static_cast<AppMapView*>(vp);
  if (!v || !v->tiles) return;
  v->placed = v->layer.render(v->tiles, {v->lat, v->lon, v->zoom, v->w, v->h,
      4, 0, host.night && host.night(), false}).placed;
  if (v->ovl) lv_obj_move_foreground(v->ovl);
}

#endif

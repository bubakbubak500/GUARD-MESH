// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapScreen.h"
#include "../UITask.h"
#include "../device_caps.h"
#include "../i18n.h"
#include "../../helpers/esp32/TouchPrefsStore.h"
#include <helpers/ContactInfo.h>
#include <helpers/AdvertDataHelpers.h>
#include <LvglPsramAlloc.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include "../models/MapProjection.h"
#include "../theme/Theme.h"
#include "../theme/Fonts.h"
#include "../widgets/Styles.h"
#include <atomic>
namespace ui { namespace screens { namespace map {
using namespace ui::maps;
using namespace ui::theme;
using namespace ui::widgets;
static Host host{};
static void watchRoot(lv_obj_t** root);
static bool acceptsEvent(lv_event_t* event);
static void mapStorageToggleCb(lv_event_t* event) {
  if (acceptsEvent(event) && host.mapOptTilesSdCb) host.mapOptTilesSdCb(event);
}
static void rootDeleted(lv_event_t* event);
static MapTileLayer mapTileLayer({});
static bool s_map_render_pending = false;
static void* allocateZeroed(size_t size) {
  void* value = lvglPsramAlloc(size);
  if (value) memset(value, 0, size);
  return value;
}
void freeMapTiles() {
  mapTileLayer.clear();
}
static lv_obj_t* s_map_canvas      = nullptr;   // full-screen tile surface (behind the tabview)
static lv_obj_t* s_map_page        = nullptr;   // the Map tab page (holds touch catcher + overlay UI)
static lv_obj_t* s_map_touch       = nullptr;   // transparent full-page touch catcher for pan/drag
static lv_obj_t* s_map_info_lbl    = nullptr;   // coords read-out (bottom-left corner)
static lv_obj_t* s_map_count_lbl   = nullptr;   // marker / download count (bottom-right corner)
static lv_obj_t* s_map_status_lbl  = nullptr;
static lv_obj_t* s_map_zoom_lbl    = nullptr;   // zoom + tile path at center (top-left, under © OSM)
static lv_obj_t* s_map_zoom_slider = nullptr;   // zoom slider overlay (toggled by the zoom button)
static lv_obj_t* s_map_zoom_val    = nullptr;   // live "z<level>" readout centred above the slider

// ----- Mercator helpers + tile cache -----
//
// Standard Web-Mercator slippy tiles. World is laid out as a square of
// 2^zoom × 2^zoom tiles, each tile 256×256 px. A coordinate's "world pixel"
// is its position in the full virtual canvas (e.g. at zoom 14 the world is
// 4_194_304 px on a side). Tile (tx, ty) covers world px (tx*256, ty*256)
// → (tx*256+255, ty*256+255).
//
// All transforms below use doubles since 1e-7 degrees of longitude already
// exceeds float precision at high zoom.
constexpr int     k_map_tile_size     = 256;
// Map viewport size. Not constexpr: set to the real tab content area in
// makeMapTab() so the map fills the screen in either orientation (240x226
// portrait / full-width landscape). The tile projection + marker math read
// these, so they must reflect the actual canvas. The 3x3 tile grid below
// covers 768 px and is generous enough for either viewport.
static int        k_map_canvas_w      = 240;
static int        k_map_canvas_h      = 226;
// Manual zoom-button range. Wide on purpose: the zoom in/out buttons are gated by
// mapZoomReachable() (a tile exists in the SD /maps/osm pack or LittleFS cache, OR
// Wi-Fi can fetch it), so the buttons stop at the edge of the tiles you actually
// have — these are just the hard floor/ceiling. Previously capped at 12..16, which
// stranded anyone whose pack went wider. 19 is OSM's max zoom; 3 is continent-scale.
// Minimum gap between the progressive per-tile repaints in renderMapTiles. Each
// repaint is a full-screen composite plus the two transparent chrome bars (~115 ms
// on the M9), so painting after every tile dominated the cold open. 350 ms still
// gives ~3 visible fill-in steps across a ~1 s read+decode pass.

// 3×3 grid covers a 768×768-px slab — ~130 px of pan buffer on each side
// of the 240×226 viewport. We tried 5×5 (25 tiles, 3.2 MB PSRAM) for the
// bigger buffer but it pushed PSRAM usage to the edge on the S3 boards —
// some tile decodes silently failed alloc and stayed black. So the radius
// is now sized PER AXIS from the actual canvas (mapComputeGridRadius):
// small screens (V4/T-Deck 240/320) stay at radius 1 (3 tiles/axis = 9
// total, ~1.15 MB), while the wide Tanmatsu (800×480) needs a horizontal
// radius of 2 so the 3-tile/768-px span doesn't leave an uncovered strip
// at one edge of the 800-px width. The static slot array is sized to the
// worst case (k_map_grid_radius_max); only tiles actually loaded allocate
// their 128 KB PSRAM buffer, so the extra slots cost only pointer-sized
// struct entries in .bss, not PSRAM, on the small boards.
constexpr int     k_map_grid_radius_max = 2;   // max tiles/axis we ever request (Tanmatsu)
constexpr int     k_map_visible_tiles_max =
    (2 * k_map_grid_radius_max + 1) * (2 * k_map_grid_radius_max + 1);
// Runtime per-axis radius (recomputed from k_map_canvas_w/h when the map
// tab is built — see mapComputeGridRadius). Default to 1 for the common
// small-screen case before the map tab exists.
static int        s_map_grid_rx = 1;           // tiles each side, horizontal
static int        s_map_grid_ry = 1;           // tiles each side, vertical

// Per-tile: we hold the *decoded* RGB565 in PSRAM, not the JPEG. Decoding
// happens ONCE at load time (in our control). The lv_img widget is then
// CF_TRUE_COLOR, so every subsequent redraw is a pure blit — no decoder
// call. Crucial for live-pan, where LVGL otherwise re-decodes all 9 JPEGs
// on every PRESSING tick (~67 Hz).
//
// Memory cost: 256 × 256 × 2 B = 128 KB per tile × 9 visible = ~1.15 MB
// from PSRAM (we have 8 MB, so this is fine).
// Recompute the per-axis tile radius from the current canvas size. Enough
// tiles must straddle the center so the grid covers the FULL viewport even
// when the center coordinate sits at the very edge of its center tile:
// worst case the center is 256 px from one side of its tile, so each side
// needs ceil((canvas/2) / 256) tiles → ceil(canvas / 512). Clamped to
// [1, k_map_grid_radius_max]. Small screens (≤512 px) stay at 1; the
// 800-px-wide Tanmatsu lands on rx = 2 (5 tiles = 1280 px ≥ 800).
static void mapComputeGridRadius() {
  auto r = [](int dim) -> int {
    int n = (dim + 511) / 512;                 // ceil(dim / 512)
    if (n < 1) n = 1;
    if (n > k_map_grid_radius_max) n = k_map_grid_radius_max;
    return n;
  };
  s_map_grid_rx = r(k_map_canvas_w);
  s_map_grid_ry = r(k_map_canvas_h);
}
// Pan layer: a single transparent lv_obj that holds tile widgets + marker
// widgets. Live-pan just translates THIS — one set_pos = one invalidation,
// regardless of how many children sit on top.
static lv_obj_t* s_map_pan_layer = nullptr;
// Last renderMapTiles() gap count (visible tiles that weren't on disk).
// Drives the compact download/Wi-Fi hint in the bottom info bar.
static int       s_map_last_missing = 0;
// --- Render-side tile diagnostics (serial is unreadable on the companion build).
// The MTC fetch counters cover DOWNLOAD; these cover READ-BACK + DECODE + PLACE,
// so the always-visible zoom label can pinpoint where the pipeline breaks:
//   fr  = free KB in the tiles partition (0 == cache full, no room to write)
//   d   = tiles that ended up placed (kept or freshly decoded) / tiles wanted
// Declared unconditionally so renderMapTiles/refreshMapInfoLabel reference them
// on every board; only the DOWNLOAD counters are MTC-gated. (host.cache().freeKb is
// declared earlier, before tilesFsLowSpace which writes it.)
static int       s_tile_dec_ok   = 0;                  // tiles placed in the last render pass
static int       s_tile_dec_want = 0;                  // tiles wanted (grid size) in the last pass

// Release a slot's WIDGET but KEEP its 128 KB PSRAM buffer for the next tile that
// lands here. Per-render tile churn used to malloc/free 128 KB up to nine times a
// frame, which fragmented the 2 MB-PSRAM V4 down to <128 KB blocks so fresh tiles
// couldn't allocate (only the top rows rendered). Reusing the buffer in place = zero
// churn = no fragmentation. Full teardown (freeMapTileSlot) still frees the buffer.



static double   s_map_center_lat = 0.0;
static double   s_map_center_lon = 0.0;
static std::atomic<uint8_t> s_map_zoom{k_map_zoom_default};
static bool     s_map_view_inited = false;  // first map open did the recenter+zoom-snap; after that, remember the user's view (issue #5)
static bool       s_map_follow     = false; // auto-follow: recenter on self whenever the GPS coords change
static lv_obj_t*  s_map_follow_btn = nullptr;
// Map pan mode (M9): the Map key on the Map tab toggles it — arrows then pan
// via mapNudge, Map/Back exits. Lives here with the map state because
// mapAutoFollowTick must pause while it's active: auto-follow compares the GPS
// fix against the MAP CENTER, so the pan itself creates the delta and follow
// snapped the view back within one 250 ms tick of every nudge — no GPS
// movement needed. Follow (if on) resumes, by design, the moment pan exits.
static bool s_m9_map_pan = false;
// Map zoom control style: false = slider (default, toggled by the on-map button),
// true = a +/- button pair (issue #26). Loaded from prefs at boot; toggled in the
// Map options popup. mapZoomControlsApply() positions/shows the right controls.
static bool       s_map_zoom_buttons   = false;
static lv_obj_t*  s_map_btn_zoomtoggle = nullptr;   // "+/-" slider-toggle (slider mode)
static lv_obj_t*  s_map_btn_zoomin     = nullptr;   // "+" (buttons mode)
static lv_obj_t*  s_map_btn_zoomout    = nullptr;   // "-" (buttons mode)
static lv_obj_t*  s_map_btn_recenter   = nullptr;   // GPS recenter (shifts down in buttons mode)
static lv_obj_t*  s_map_btn_contacts   = nullptr;   // contacts list (shifts down in buttons mode)
static bool     s_map_has_pack   = false;   // toggles placeholder visibility
static void mapSetHasPack(bool has_pack) {
  if (s_map_has_pack == has_pack) return;
  s_map_has_pack = has_pack;
#if defined(HAS_WIO_TRACKER_L2)
  // L2's empty canvas is dark, while loaded day tiles are light. Refresh the
  // transparent map chrome when that background changes under the controls.
  if (host.getActiveTab() == host.tabIndex) host.applyMapChrome(true);
#endif
}

// lat/lon → world pixel at given zoom (Web Mercator).


// (kept for the pan/zoom step — converts a touch position back to lat/lon.)



static uint8_t bestAvailableZoom(double lat, double lon) {
#if defined(ESP32)
  if (!host.mapTileSourceReady()) return 0;
  for (int z = (int)k_map_zoom_max; z >= (int)k_map_zoom_min; --z) {
    double wx, wy;
    latLonToWorldPx(lat, lon, (uint8_t)z, &wx, &wy);
    const int32_t tx = (int32_t)floor(wx / 256.0);
    const int32_t ty = (int32_t)floor(wy / 256.0);
    if (host.tileExistsAt((uint8_t)z, (long)tx, (long)ty)) return (uint8_t)z;
  }
  return 0;
#else
  (void)lat; (void)lon;
  return 0;
#endif
}

// Coalesce map changes until UITask has unwound the LVGL input/timer stack.
// Tile decoding and progressive lv_refr_now() calls from RELEASED overflowed
// the T-Deck loopTask stack while dragging the map (field crash 2026-09-29).
void renderMapTiles() {
  if (s_map_canvas) s_map_render_pending = true;
}

static void renderMapTilesNow() {
  if (!s_map_canvas) return;
  if (s_map_pan_layer) lv_obj_set_pos(s_map_pan_layer, 0, 0);
  renderMapMarkers();

  // Fall back to placeholder when no usable GPS center.
  if (s_map_center_lat == 0.0 && s_map_center_lon == 0.0) {
    freeMapTiles();
    mapSetHasPack(false);
    if (s_map_status_lbl) {
      lv_label_set_text(s_map_status_lbl,
          TR("Map — set your location\nin Settings \xe2\x86\x92 Profile to\nshow the map here."));
      lv_obj_clear_flag(s_map_status_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }

  // (Auto-snap to best available zoom moved to onMapTabActivated. Here we
  // honour whatever the user picked via the zoom buttons — even if the
  // requested zoom has no tiles, we show the empty grid + "no tile pack"
  // hint so the user can tap − to go back, instead of silently fighting
  // the buttons.)

  mapComputeGridRadius();
  const size_t psram = host.externalTotal();
  const bool lowMemory = psram && psram < 4u * 1024 * 1024;
  const auto rendered = mapTileLayer.render(s_map_pan_layer ? s_map_pan_layer : s_map_canvas,
      {s_map_center_lat, s_map_center_lon, s_map_zoom, k_map_canvas_w, k_map_canvas_h,
       lowMemory ? 4 : 25, lowMemory ? 0 : 256, host.night(), true});
  s_tile_dec_want = rendered.wanted;
  s_tile_dec_ok = rendered.placed;
  const bool any_loaded = rendered.placed != 0;
  const int n_missing = rendered.missing;

#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  // Warm the min/max zoom cache for this location (overview + detail) in the
  // background. Queued after the visible tiles above so they keep priority.
  host.queueZoomPackForCenter();
#endif

  // ---- Status overlay ----
  // Cases:
  //   • some tiles rendered (any_loaded) → map is usable; hide the overlay.
  //     Missing edge tiles just fill in as they download.
  //   • nothing rendered (panned into an un-tiled area) → show a clear,
  //     human message that depends on whether we can actually fetch.
  mapSetHasPack(any_loaded);
  // Remember the gap count so the bottom info bar can show a compact
  // "downloading" / "Wi-Fi off" hint even when the map is partially loaded
  // (any_loaded true) — that's the common "panned toward the edge of my
  // saved area" case where a big centered overlay would be too intrusive.
  s_map_last_missing = n_missing;

  bool wifi_up = false;
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  wifi_up = (host.online());
#endif

  if (!s_map_status_lbl) return;

  if (any_loaded) {
    lv_obj_add_flag(s_map_status_lbl, LV_OBJ_FLAG_HIDDEN);
    return;
  }

#if defined(ESP32)
#if CAP_MICROSD
  // host.style() == 0: SD packs are OSM-only. In topo the loader reads the
  // online /tiles/topo cache, so pointing the user at /maps/osm would be wrong
  // and would hide the download progress/diagnostics below.
  if (host.cache().sdTiles && host.cache().sdBackend && host.style() == 0) {
    lv_label_set_text(s_map_status_lbl,
        TR("Map tiles: microSD\n\n"
        "/maps/osm/z/x/y.png\n"
        "(or /tiles/z/x/y.jpg)\n\n"
        "Map appears when a tile\nfor this area is found."));
  } else
#endif
  if (!host.cache().ready) {
#if defined(HAS_TDECK_GT911) || defined(HAS_TDECK_PRO) || defined(TLORA_PAGER) || defined(HAS_THINKNODE_M9)
    // Pager included: under the launcher there's no "tiles" partition, so point the user at the
    // microSD fallback rather than the (launcher-wrong) "reflash the tiles partition" advice.
    // M9 included: its cache PREFERS the built-in 16 GB microSD (every unit ships with one), so
    // this state almost always means the card didn't mount — reflash advice would be the wrong fix.
    if (host.cache().cardPresent)
      lv_label_set_text(s_map_status_lbl,
          TR("Map storage error.\n\nSD card detected but the\ntile cache didn't mount.\nReboot to retry."));
    else
      lv_label_set_text(s_map_status_lbl,
          TR("No map storage.\n\nInsert an SD card to cache\nWi-Fi tiles (or reflash to\nrestore the tiles partition)."));
#elif defined(HAS_TANMATSU)
    // Tanmatsu has no tiles partition by design (see the FFat fallback in begin()).
    // Never tell the user to "reflash the partition" — the map runs network-only.
    if (wifi_up)
      lv_label_set_text(s_map_status_lbl,
          TR("Loading map tiles\xe2\x80\xa6\n\nKeep Wi-Fi connected.\nTiles appear as they arrive."));
    else
      lv_label_set_text(s_map_status_lbl,
          TR("No map tiles here.\n\nConnect to Wi-Fi (Settings \xe2\x86\x92 Wi-Fi)\nto download this area."));
#else
    lv_label_set_text(s_map_status_lbl,
        TR("Map storage error.\nReflash the tiles partition."));
#endif
  } else if (wifi_up) {
    // Wi-Fi up: the missing tiles were just queued for download. Reassure
    // the user it's working — the screen repaints (s_tile_fetch_dirty) as
    // tiles land. We also surface the fetch counters + last HTTP code here:
    // serial is unreadable on this build (companion protocol owns the UART),
    // so this on-screen line is the only window into whether fetches actually
    // SUCCEED. "ok" climbing = tiles are being written; "fail" climbing with a
    // non-200 "http" (e.g. 301 = HTTP→HTTPS redirect the device can't follow,
    // 403/-1 = blocked/connect fail) means the server/proxy path is the problem,
    // not the on-device renderer.
#if defined(MULTI_TRANSPORT_COMPANION)
    char dl[288];
    snprintf(dl, sizeof dl,
        "Downloading map tiles\xe2\x80\xa6  (%s)\n"
        "ok %u   fail %u   http %d   wr %c\n"
        "open-fail %u   short-wr %u\n\n"
        "Keep Wi-Fi connected.\nTiles appear as they arrive.",
#if CAP_MICROSD
  (host.cache().sdBackend ? "SD cache" : "flash cache"),
#else
        (host.cache().sdBackend ? "SD cache" : "flash cache"),
#endif
        (unsigned)host.cache().ok, (unsigned)host.cache().failed,
        (int)host.cache().http, (char)host.cache().write,
        (unsigned)host.cache().openFailures, (unsigned)host.cache().shortWrites);
    lv_label_set_text(s_map_status_lbl, dl);
#else
    lv_label_set_text(s_map_status_lbl,
        TR("Downloading map tiles\xe2\x80\xa6\n\n"
        "Keep Wi-Fi connected.\nTiles appear as they arrive\n"
        "and are saved for offline use."));
#endif
  } else {
    // No Wi-Fi and no saved tiles here — be explicit about the fix.
    lv_label_set_text(s_map_status_lbl,
        TR("No saved map tiles here.\n\n"
        "Connect to Wi-Fi (Settings \xe2\x86\x92 Wi-Fi)\n"
        "to download this area.\n"
        "Saved tiles stay available offline."));
  }
#else
  lv_label_set_text(s_map_status_lbl, TR("No tiles (non-ESP32)"));
#endif
  lv_obj_clear_flag(s_map_status_lbl, LV_OBJ_FLAG_HIDDEN);
}

// ----- Markers -----
//
// Self marker = small white crosshair; contact markers = colored dots
// sized for finger taps. Plotted as children of s_map_canvas so they
// stack on top of the tile layer. The cache below is flat (linear scan
// when handling marker taps) — fine for at-most-32 visible markers.
struct MapMarker {
  int       mesh_idx;   // ContactInfo index for real contacts; -1 for self
  lv_obj_t* obj;
};
// Was 32, which with slot 0 reserved for self meant only 31 contact dots could ever
// render while the bottom-right label counted every positioned contact with no cap at
// all. Past that the map silently stopped drawing and the label kept counting, so the
// two disagreed with nothing on screen to say why (reported as "new contacts never
// appear on the map, but the number is right"). Markers are only built for contacts
// INSIDE the viewport (see the cull in the fill loop), so this is the number visible
// at once, not the contact count.
// Hard ceiling on simultaneously-drawn contact dots. Was 32, which with slot 0 reserved
// for self meant only 31 could ever render while the bottom-right label counted every
// positioned contact with no cap at all — so past 31 the map silently stopped drawing
// and the label kept counting, with nothing on screen to explain the difference
// (reported as "new contacts never appear on the map, but the number is right").
// Markers are only built for contacts INSIDE the viewport, so this is how many are
// visible at once, not the contact count. Costs 8 bytes of static RAM per slot.

#if CAP_LUA_SDK_EXT
// ---------------------------------------------------------------------------
// App map views (wada.map) -- a real slippy map an app can put on its page.
//
// Apps could previously draw geography only onto their own canvas, from
// scratch, with no basemap. This gives them the firmware's actual tiles,
// projection and cache.
//
// It does NOT share the map tab's tile pool. That pool is sized and culled for
// a full-screen viewport and is rebuilt on pan/zoom; borrowing slots from it
// would mean an app and the map tab evicting each other's tiles at whatever
// rate they happened to redraw. A view owns a small pool of its own instead,
// capped at kAppMapTilesMax so the cost is bounded and predictable: 128 KB per
// tile, exactly like the map tab, and freed the moment the view is collected.
#endif  // CAP_LUA_SDK_EXT

constexpr int k_map_markers_max = 256;
// The self->contact link LINES keep their own, much smaller ceiling: each one holds a
// persistent 2-point array LVGL does not copy, so they cost 12 bytes a slot rather than
// 8, and a screen with hundreds of dotted lines on it is unreadable long before it is
// slow. Raising the dot ceiling deliberately does not drag this up with it.
constexpr int k_map_links_max = 32;
// How many contact dots the last rebuild actually drew, and how many wanted to be
// drawn. The status label reports the shortfall instead of hiding it.
static int s_map_markers_drawn = 0;
static int s_map_markers_wanted = 0;
static MapMarker s_map_markers[k_map_markers_max] = {};
// Effective dot limit: the user's setting (Map settings), clamped to the array. 0 in
// the setting means "no limit", i.e. everything in view up to the firmware ceiling.
static int mapMarkerCap() {
#if defined(ESP32)
  const uint16_t want = touchPrefsGetMapMarkerCap();
  if (want == 0 || want > (uint16_t)(k_map_markers_max - 1)) return k_map_markers_max - 1;
  return (int)want;
#else
  return k_map_markers_max - 1;
#endif
}

// Dotted self->contact link lines (toggled by the on-map button). Each line
// keeps its own persistent 2-point array — LVGL does not copy the points.
static bool       s_map_show_links = true;
static lv_obj_t*  s_map_link_objs[k_map_links_max] = {};
static lv_point_t s_map_link_pts[k_map_links_max][2];

// Per-element visibility of the map's on-screen text/markers (persisted; coords +
// contacts default shown, the tile line default hidden since prefs v50). Coords =
// bottom-left read-out, TileXYZ = the zoom + tile path line, Contacts = the contact markers.
static bool s_map_show_coords    = true;
static bool s_map_show_tilexyz   = false;
static bool s_map_show_contacts  = true;
static bool s_map_tile_debug     = false;  // developer: tile-pipeline diagnostic overlay on the zoom line (off by default)
static bool s_map_direct_only    = false;  // when true, only 0-hop (directly-heard) contacts appear

// Apply the coords/tile-line visibility flags to the two corner labels. Safe to
// call before the labels exist (null-guarded); invoked at map build + on toggle.
static void applyMapTextVis() {
  auto vis = [](lv_obj_t* o, bool show) {
    if (!o) return;
    if (show) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  };
  vis(s_map_info_lbl, s_map_show_coords);
  vis(s_map_zoom_lbl, s_map_show_tilexyz);
}

// ----- Route replay overlay -----
// "Replay" in a message's Info popup resolves that message's repeater hops to
// their advertised positions and animates them onto the map as a path, one node
// at a time. CHAIN mode (received floods) links consecutive hops sender->us;
// STAR mode (our own sent floods) links each repeater that echoed us back to
// "me". All overlay widgets live on the map pan layer and are freed by
// freeMapMarkers() at the start of the next render.
struct RouteNode { double lat, lon; bool has_pos; char tag[6]; char name[36]; char id[9]; };
static constexpr int k_route_max = 10;
static RouteNode*  s_route = (RouteNode*)allocateZeroed(sizeof(RouteNode) * k_route_max);   // PSRAM (0.7 KB off internal .bss)
static int         s_route_n      = 0;       // nodes captured
static int         s_route_reveal = 0;       // nodes shown so far (animation cursor)
static bool        s_route_active = false;   // overlay currently shown
static uint8_t     s_route_mode   = 0;       // 0 = chain, 1 = star
static lv_timer_t* s_route_timer  = nullptr;
static lv_obj_t*   s_route_objs[k_route_max * 3] = {nullptr};   // lines + dots + tags
static int         s_route_obj_n  = 0;
static lv_point_t  s_route_seg_pts[k_route_max][2];             // persistent (lv_line keeps the ptr)
static void drawRouteOverlay(lv_obj_t* parent, double cwx, double cwy);
// Route-replay HUD: top banner with the current hop's repeater name + identifier,
// plus a replay button. Lives on lv_layer_top while a route is shown on the map.
static lv_obj_t*   s_route_hud     = nullptr;
static lv_obj_t*   s_route_hud_lbl = nullptr;
static void routeReplayTick(lv_timer_t* t);
static void routeHudUpdate();
static void showRouteHud();
static void hideRouteHud();

// ---- Discover wardriving coverage overlay (drawn on the map pan layer) ----
// Plots the Discover app's logged coverage samples (s_disc_track[], populated in host.discoverWardriveTick)
// as signal-coloured dots — a personal RF-coverage map: a 0-hop hit means "reachable from that point",
// so green = strong contact, red = weak. Object pool freed alongside the other map markers.
static lv_obj_t* s_disc_map_objs[160] = {nullptr};
static int       s_disc_map_obj_n = 0;
static void discoverFreeMapObjs() {
  for (int i = 0; i < s_disc_map_obj_n; ++i)
    if (s_disc_map_objs[i]) { lv_obj_del(s_disc_map_objs[i]); s_disc_map_objs[i] = nullptr; }
  s_disc_map_obj_n = 0;
}
static void discoverDrawCoverage(lv_obj_t* parent, double cwx, double cwy) {
  const int cap = (int)(sizeof(s_disc_map_objs) / sizeof(s_disc_map_objs[0]));
  const int total = host.coverageCount() < 160 ? host.coverageCount() : 160;
  for (int k = 0; k < total && s_disc_map_obj_n < cap; ++k) {
    const CoveragePoint& p = host.coveragePoint(k);
    double mwx, mwy;
    latLonToWorldPx((double)p.lat_e6 / 1e6, (double)p.lon_e6 / 1e6, s_map_zoom, &mwx, &mwy);
    const int sx = (int)(mwx - cwx + k_map_canvas_w / 2);
    const int sy = (int)(mwy - cwy + k_map_canvas_h / 2);
    if (sx < -4 || sx >= k_map_canvas_w + 4 || sy < -4 || sy >= k_map_canvas_h + 4) continue;
    int r = p.best_rssi; if (r > -50) r = -50; if (r < -110) r = -110;   // clamp to the colour range
    int g = (r + 110) * 255 / 60;                                        // 0 (weak) .. 255 (strong)
    uint32_t col = ((uint32_t)(255 - g) << 16) | ((uint32_t)g << 8);     // red -> green
    lv_obj_t* d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, 8, 8);
    lv_obj_set_pos(d, sx - 4, sy - 4);
    lv_obj_set_style_bg_color(d, lv_color_hex(col), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(d, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_color(d, lv_color_hex(0x101010), LV_PART_MAIN);
    lv_obj_set_style_border_width(d, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(d, 4, LV_PART_MAIN);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);
    s_disc_map_objs[s_disc_map_obj_n++] = d;
  }
}
// Discover page's "Show on map": centre the map on our current position + open the Map tab, where
// the coverage overlay draws. Keeps the centre (s_map_view_inited) so onMapTabActivated won't snap back.
void discoverJumpToMapHere() {
  UITask* task = host.task();
  if (task && task->getGpsFix()) {
    s_map_center_lat = task->getNodeLat();
    s_map_center_lon = task->getNodeLon();
    s_map_view_inited = true;
  }
  host.closeDiscoverPage();
  host.goToTab(host.tabIndex);
}

static void freeMapMarkers() {
  for (auto& m : s_map_markers) {
    if (m.obj) { lv_obj_del(m.obj); m.obj = nullptr; }
    m.mesh_idx = -2;   // "slot empty" sentinel
  }
  for (auto& ln : s_map_link_objs) {
    if (ln) { lv_obj_del(ln); ln = nullptr; }
  }
  for (int i = 0; i < s_route_obj_n; ++i) {
    if (s_route_objs[i]) { lv_obj_del(s_route_objs[i]); s_route_objs[i] = nullptr; }
  }
  s_route_obj_n = 0;
  discoverFreeMapObjs();
}

static void openMarkerPopupForContact(int mesh_idx);
static void openMapPicker(const int* idxs, int n);
// (onMapMarkerClickedCb removed — marker taps are now dispatched centrally
// from the canvas's RELEASED handler in mapCanvasEventCb. See the marker
// scan in the tap branch below.)

// Plot self + contacts onto the canvas at their lat/lon. Called every time
// tiles are re-rendered (pan, zoom, recenter, tab open) so markers stay
// pinned to the right pixel for the current center.
void renderMapMarkers() {
  if (s_map_render_pending) return; // rebuilt with the final tile viewport
  freeMapMarkers();
  if (!s_map_canvas) return;
  if (s_map_center_lat == 0.0 && s_map_center_lon == 0.0) return;
  // Markers share the pan layer with tiles so live-pan slides everything
  // together with a single set_pos.
  lv_obj_t* parent = s_map_pan_layer ? s_map_pan_layer : s_map_canvas;

  double cwx, cwy;
  latLonToWorldPx(s_map_center_lat, s_map_center_lon, s_map_zoom, &cwx, &cwy);

  // Self screen position (computed even if off-canvas, so link lines from an
  // off-screen self still point the right way; clipped to the canvas by LVGL).
  bool self_has = false;
  int  self_sx = 0, self_sy = 0;
  if (host.task()) {
    const double self_lat = host.task()->getNodeLat();
    const double self_lon = host.task()->getNodeLon();
    if (self_lat != 0.0 || self_lon != 0.0) {
      double swx, swy;
      latLonToWorldPx(self_lat, self_lon, s_map_zoom, &swx, &swy);
      self_sx = (int)(swx - cwx + k_map_canvas_w / 2);
      self_sy = (int)(swy - cwy + k_map_canvas_h / 2);
      self_has = true;
    }
  }

  // ---- Dotted links from self to each on-screen contact (drawn first so the
  //      markers sit on top). Toggled by the on-map links button.
  if (s_map_show_links && s_map_show_contacts && self_has) {   // links only make sense when contacts are shown
    auto clampc = [](int v) -> lv_coord_t {
      if (v < -2000) v = -2000;
      if (v >  2000) v =  2000;
      return (lv_coord_t)v;
    };
    int link_n = 0;
    for (uint32_t i = 0; i < host.contactCount() && link_n < k_map_links_max; ++i) {
      ContactInfo c;
      if (!host.contactAt(i, c)) continue;
      if (c.gps_lat == 0 && c.gps_lon == 0) continue;
      if (s_map_direct_only && c.out_path_len != 0) continue;
      double mwx, mwy;
      latLonToWorldPx((double)c.gps_lat / 1.0e6, (double)c.gps_lon / 1.0e6,
                      s_map_zoom, &mwx, &mwy);
      const int sx = (int)(mwx - cwx + k_map_canvas_w / 2);
      const int sy = (int)(mwy - cwy + k_map_canvas_h / 2);
      if (sx < -8 || sx >= k_map_canvas_w + 8 || sy < -8 || sy >= k_map_canvas_h + 8) continue;
      s_map_link_pts[link_n][0].x = clampc(self_sx);
      s_map_link_pts[link_n][0].y = clampc(self_sy);
      s_map_link_pts[link_n][1].x = clampc(sx);
      s_map_link_pts[link_n][1].y = clampc(sy);
      lv_obj_t* ln = lv_line_create(parent);
      lv_line_set_points(ln, s_map_link_pts[link_n], 2);
      // Solid (not dashed): LVGL 8.3's SW renderer doesn't reliably dash
      // diagonal lines — a thin, semi-transparent line reads as a "link".
      lv_obj_set_style_line_width(ln, 2, LV_PART_MAIN);
      lv_obj_set_style_line_color(ln, lv_color_hex(0x6FA8DA), LV_PART_MAIN);
      lv_obj_set_style_line_opa(ln, LV_OPA_70, LV_PART_MAIN);
      lv_obj_set_style_line_rounded(ln, true, LV_PART_MAIN);
      lv_obj_clear_flag(ln, LV_OBJ_FLAG_CLICKABLE);
      s_map_link_objs[link_n] = ln;
      ++link_n;
    }
  }

  // ---- Self marker — crosshair (on top of the links).
  if (self_has &&
      self_sx >= -10 && self_sx < k_map_canvas_w + 10 &&
      self_sy >= -10 && self_sy < k_map_canvas_h + 10) {
    MapMarker& m = s_map_markers[0];
    m.mesh_idx = -1;
    m.obj = lv_label_create(parent);
    lv_label_set_text(m.obj, LV_SYMBOL_GPS);
    lv_obj_set_style_text_font(m.obj, &font16(), LV_PART_MAIN);
    lv_obj_set_style_text_color(m.obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    // A bare white glyph disappears on light tiles (#366). The contact markers
    // below already carry a dark border for exactly this reason; the self
    // marker never got one. Same treatment: a dark chip behind the glyph, so it
    // reads on any basemap without needing a colour that means something else.
    lv_obj_set_style_bg_color(m.obj, lv_color_hex(0x101418), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(m.obj, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(m.obj, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(m.obj, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(m.obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(m.obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(m.obj, LV_OPA_60, LV_PART_MAIN);
    // The glyph's optical center isn't at its bbox center — fudge the
    // align slightly so the crosshair lines up with the tile pixel. The 3 px
    // of chip (2 pad + 1 border) shifts it again, so take that off too.
    lv_obj_set_pos(m.obj, self_sx - 8 - 3, self_sy - 11 - 3);
  }

  // ---- Contact markers — colored circles sized for taps (14×14 with a
  //      2-px black border so they read on any tile background).
  int slot = 1;   // slot 0 reserved for self
  s_map_markers_wanted = 0;
  // Contact markers — skipped entirely when "Contacts on map" is off (self stays).
  const int marker_cap = mapMarkerCap();
  for (uint32_t i = 0; s_map_show_contacts && i < host.contactCount(); ++i) {
    ContactInfo c;
    if (!host.contactAt(i, c)) continue;
    if (c.gps_lat == 0 && c.gps_lon == 0) continue;
    if (s_map_direct_only && c.out_path_len != 0) continue;

    const double lat = (double)c.gps_lat / 1.0e6;
    const double lon = (double)c.gps_lon / 1.0e6;
    double mwx, mwy;
    latLonToWorldPx(lat, lon, s_map_zoom, &mwx, &mwy);
    const int sx = (int)(mwx - cwx + k_map_canvas_w / 2);
    const int sy = (int)(mwy - cwy + k_map_canvas_h / 2);
    // Cull off-screen markers — they'd just allocate widgets we never see.
    if (sx < -8 || sx >= k_map_canvas_w + 8 ||
        sy < -8 || sy >= k_map_canvas_h + 8) continue;

    // Past the cull this contact is positioned AND on screen, so it wanted a dot.
    // Count it either way — the status label reports the shortfall rather than
    // letting the map and the number disagree in silence.
    s_map_markers_wanted++;
    if (slot >= marker_cap + 1) continue;   // +1: slot 0 is self

    // Color by contact type so a glance at the map maps to the chip
    // colors on the Contacts tab. Defaults to chat-peer orange.
    uint32_t color = 0xFF6F4D;
    if (c.type == ADV_TYPE_REPEATER) color = 0x4DA8FF;
    else if (c.type == ADV_TYPE_ROOM) color = 0xC9A24A;

    MapMarker& m = s_map_markers[slot];
    m.mesh_idx = (int)i;
    m.obj = lv_obj_create(parent);
    lv_obj_remove_style_all(m.obj);
    lv_obj_set_size(m.obj, 14, 14);
    lv_obj_set_pos(m.obj, sx - 7, sy - 7);
    lv_obj_set_style_bg_color(m.obj, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(m.obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(m.obj, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(m.obj, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(m.obj, 7, LV_PART_MAIN);
    // Markers are NOT clickable themselves — taps go to the canvas, whose
    // RELEASED handler scans all markers within ~16 px of the tap point.
    // This makes overlapping markers selectable (disambiguation sheet) and
    // also lets panning that starts on a marker fall through to the canvas
    // pan handler instead of being swallowed by the marker.
    lv_obj_clear_flag(m.obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(m.obj, LV_OBJ_FLAG_SCROLLABLE);
    ++slot;
  }
  s_map_markers_drawn = slot - 1;   // slot 0 is self

  // Discover wardriving coverage dots (my logged signal samples), under the route overlay.
  if (host.coverageCount() > 0) discoverDrawCoverage(parent, cwx, cwy);

  // Route-replay overlay — drawn last so the path + nodes sit above tiles,
  // link lines and contact markers.
  if (s_route_active) drawRouteOverlay(parent, cwx, cwy);
}

// ===== Route replay overlay =================================================
// Paint the revealed portion of s_route[] onto the pan layer: amber segments
// plus a numbered dot at each repeater. The "ME" node is left to the existing
// GPS crosshair. Objects are tracked in s_route_objs[] and freed by the next
// freeMapMarkers().
static void drawRouteOverlay(lv_obj_t* parent, double cwx, double cwy) {
  if (!s_route_active || s_route_n <= 0 || !parent) return;
  const int reveal = (s_route_reveal < s_route_n) ? s_route_reveal : s_route_n;

  int  nsx[k_route_max], nsy[k_route_max];
  bool nhas[k_route_max];
  for (int i = 0; i < s_route_n; ++i) {
    nhas[i] = s_route[i].has_pos;
    if (!nhas[i]) continue;
    double wx, wy;
    latLonToWorldPx(s_route[i].lat, s_route[i].lon, s_map_zoom, &wx, &wy);
    nsx[i] = (int)(wx - cwx + k_map_canvas_w / 2);
    nsy[i] = (int)(wy - cwy + k_map_canvas_h / 2);
  }
  auto clampc = [](int v) -> lv_coord_t {
    if (v < -3000) v = -3000;
    if (v >  3000) v =  3000;
    return (lv_coord_t)v;
  };
  auto track = [&](lv_obj_t* o) {
    if (o && s_route_obj_n < (int)(sizeof(s_route_objs) / sizeof(s_route_objs[0])))
      s_route_objs[s_route_obj_n++] = o;
  };

  // Segments. chain: prev->cur; star: hub(node 0)->cur.
  int seg = 0;
  for (int i = 1; i < reveal && seg < k_route_max; ++i) {
    const int a = (s_route_mode == 1) ? 0 : (i - 1);
    const int b = i;
    if (!nhas[a] || !nhas[b]) continue;
    s_route_seg_pts[seg][0].x = clampc(nsx[a]); s_route_seg_pts[seg][0].y = clampc(nsy[a]);
    s_route_seg_pts[seg][1].x = clampc(nsx[b]); s_route_seg_pts[seg][1].y = clampc(nsy[b]);
    lv_obj_t* ln = lv_line_create(parent);
    lv_line_set_points(ln, s_route_seg_pts[seg], 2);
    lv_obj_set_style_line_width(ln, 3, LV_PART_MAIN);
    lv_obj_set_style_line_color(ln, lv_color_hex(0xFFC233), LV_PART_MAIN);
    lv_obj_set_style_line_opa(ln, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(ln, true, LV_PART_MAIN);
    lv_obj_clear_flag(ln, LV_OBJ_FLAG_CLICKABLE);
    track(ln);
    ++seg;
  }

  // Numbered nodes (skip the "ME" hub — the GPS crosshair already marks self).
  for (int i = 0; i < reveal && i < s_route_n; ++i) {
    if (!nhas[i]) continue;
    if (s_route[i].tag[0] == 'M' && s_route[i].tag[1] == 'E') continue;
    if (nsx[i] < -24 || nsx[i] >= k_map_canvas_w + 24 ||
        nsy[i] < -24 || nsy[i] >= k_map_canvas_h + 24) continue;
    const bool latest = (i == reveal - 1);
    const int  sz = latest ? 18 : 14;
    lv_obj_t* d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, sz, sz);
    lv_obj_set_pos(d, nsx[i] - sz / 2, nsy[i] - sz / 2);
    lv_obj_set_style_bg_color(d, lv_color_hex(latest ? 0xFFE08A : 0xFFC233), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(d, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(d, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(d, sz / 2, LV_PART_MAIN);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    track(d);
    lv_obj_t* tl = lv_label_create(parent);
    lv_label_set_text(tl, s_route[i].tag);
    lv_obj_set_style_text_font(tl, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(tl, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_pos(tl, nsx[i] - 5, nsy[i] - 8);
    track(tl);
  }
}

// Center + zoom the map so every positioned route node fits the canvas — on a wide
// OVERVIEW so you can see where the hops start + the whole path. Fits the hops into
// ~55% of the canvas (lots of surrounding context) and caps the zoom so even a tight
// cluster opens area-level, never fully zoomed in. You can pan/zoom freely after.
static void fitMapToRoute() {
  constexpr uint8_t k_route_overview_max = 12;   // never open the replay more zoomed-in than this
  double minlat = 90.0, maxlat = -90.0, minlon = 180.0, maxlon = -180.0;
  int n = 0;
  for (int i = 0; i < s_route_n; ++i) {
    if (!s_route[i].has_pos) continue;
    if (s_route[i].lat < minlat) minlat = s_route[i].lat;
    if (s_route[i].lat > maxlat) maxlat = s_route[i].lat;
    if (s_route[i].lon < minlon) minlon = s_route[i].lon;
    if (s_route[i].lon > maxlon) maxlon = s_route[i].lon;
    ++n;
  }
  if (n == 0) return;
  s_map_center_lat = (minlat + maxlat) / 2.0;
  s_map_center_lon = (minlon + maxlon) / 2.0;
  if (n == 1) { s_map_zoom = k_route_overview_max; return; }
  for (uint8_t z = k_map_zoom_max; ; --z) {
    double ax, ay, bx, by;
    latLonToWorldPx(maxlat, minlon, z, &ax, &ay);     // NW corner
    latLonToWorldPx(minlat, maxlon, z, &bx, &by);     // SE corner
    const double spanx = fabs(bx - ax), spany = fabs(by - ay);
    if ((spanx <= k_map_canvas_w * 0.55 && spany <= k_map_canvas_h * 0.55) ||
        z <= k_map_zoom_min) {
      s_map_zoom = z;
      break;
    }
  }
  if (s_map_zoom > k_route_overview_max) s_map_zoom = k_route_overview_max;   // keep a wide overview
}

// Reveal one more node each tick; stop (and keep the full path drawn) at the end.
static void routeReplayTick(lv_timer_t* t) {
  (void)t;
  if (!s_route_active) {
    if (s_route_timer) { lv_timer_del(s_route_timer); s_route_timer = nullptr; }
    return;
  }
  if (s_route_reveal < s_route_n) {
    ++s_route_reveal;
    if (host.getActiveTab() == host.tabIndex) renderMapMarkers();
    routeHudUpdate();   // banner shows the hop we just revealed
  }
  if (s_route_reveal >= s_route_n) {
    if (s_route_timer) { lv_timer_del(s_route_timer); s_route_timer = nullptr; }
  }
}

void clearRouteReplay() {
  if (s_route_timer) { lv_timer_del(s_route_timer); s_route_timer = nullptr; }
  const bool was = s_route_active;
  s_route_active = false;
  s_route_reveal = 0;
  s_route_n = 0;
  hideRouteHud();
  if (was && host.getActiveTab() == host.tabIndex) renderMapMarkers();   // drop the overlay
}

// Resolve a hop's hash to a repeater name (when known) + its hex identifier, for
// the on-screen replay banner. Read-only lookup -> safe mid-RX.
static void routeFillId(RouteNode& nd, const uint8_t* hash, uint8_t hsz) {
  nd.id[0] = '\0';
  for (uint8_t b = 0; b < hsz && b < 4; ++b) snprintf(nd.id + b * 2, 3, "%02X", hash[b]);
  char nm[36];
  if (host.hopName(hash, hsz, nm, sizeof nm)) {
    strncpy(nd.name, nm, sizeof(nd.name) - 1);
    nd.name[sizeof(nd.name) - 1] = '\0';
  } else {
    nd.name[0] = '\0';
  }
}

// Build s_route[] from a message's repeater hops. Returns how many nodes have a
// known position (the button is only offered when >= 2).
int buildRouteFromMessage(const ui::MessageTypes::UIMessage& m) {
  if (!s_route) return 0;
  s_route_n = 0;
  s_route_mode = 0;
  const double self_lat = host.task() ? host.task()->getNodeLat() : 0.0;
  const double self_lon = host.task() ? host.task()->getNodeLon() : 0.0;
  const bool self_has   = (self_lat != 0.0 || self_lon != 0.0);
  const bool has_rx     = (m.meta_flags & UITask::MSG_META_HAS_RX) != 0;
  const bool is_flood   = (m.meta_flags & UITask::MSG_META_IS_FLOOD) != 0;

  if (!m.outgoing && has_rx && is_flood && m.in_path_n > 0) {
    // Received flood: chain of repeaters (sender side -> us), ending at ME.
    s_route_mode = 0;
    const uint8_t hsz = (uint8_t)((m.path_len >> 6) + 1);
    const uint8_t cnt = (uint8_t)(m.path_len & 0x3F);
    int off = 0;
    for (uint8_t h = 0; h < cnt && off + (int)hsz <= m.in_path_n && s_route_n < k_route_max - 1;
         ++h, off += hsz) {
      double lat = 0, lon = 0;
      const bool hp = host.hopPosition(&m.in_path[off], hsz, &lat, &lon);
      s_route[s_route_n].has_pos = hp;
      s_route[s_route_n].lat = lat;
      s_route[s_route_n].lon = lon;
      snprintf(s_route[s_route_n].tag, sizeof(s_route[0].tag), "%u", (unsigned)(h + 1));
      routeFillId(s_route[s_route_n], &m.in_path[off], hsz);
      ++s_route_n;
    }
    if (self_has && s_route_n < k_route_max) {
      s_route[s_route_n].has_pos = true;
      s_route[s_route_n].lat = self_lat;
      s_route[s_route_n].lon = self_lon;
      strncpy(s_route[s_route_n].tag, "ME", sizeof(s_route[0].tag) - 1);
      s_route[s_route_n].tag[sizeof(s_route[0].tag) - 1] = '\0';
      strncpy(s_route[s_route_n].name, "You", sizeof(s_route[0].name) - 1);
      s_route[s_route_n].name[sizeof(s_route[0].name) - 1] = '\0';
      s_route[s_route_n].id[0] = '\0';
      ++s_route_n;
    }
  } else if (m.outgoing && m.sent_fp && self_has) {
    // Our own sent flood: star from ME to each repeater that echoed it back.
    s_route_mode = 1;
    s_route[s_route_n].has_pos = true;
    s_route[s_route_n].lat = self_lat;
    s_route[s_route_n].lon = self_lon;
    strncpy(s_route[s_route_n].tag, "ME", sizeof(s_route[0].tag) - 1);
    s_route[s_route_n].tag[sizeof(s_route[0].tag) - 1] = '\0';
    strncpy(s_route[s_route_n].name, "You", sizeof(s_route[0].name) - 1);
    s_route[s_route_n].name[sizeof(s_route[0].name) - 1] = '\0';
    s_route[s_route_n].id[0] = '\0';
    ++s_route_n;
    const uint8_t rhc = host.repeatCount(m.sent_fp);
    for (uint8_t r = 0; r < rhc && s_route_n < k_route_max; ++r) {
      uint8_t hh[4];
      const uint8_t hs = host.repeatHop(m.sent_fp, r, hh, sizeof(hh));
      if (hs == 0) continue;
      double lat = 0, lon = 0;
      const bool hp = host.hopPosition(hh, hs, &lat, &lon);
      s_route[s_route_n].has_pos = hp;
      s_route[s_route_n].lat = lat;
      s_route[s_route_n].lon = lon;
      snprintf(s_route[s_route_n].tag, sizeof(s_route[0].tag), "R%u", (unsigned)(r + 1));
      routeFillId(s_route[s_route_n], hh, hs);
      ++s_route_n;
    }
  }

  int plottable = 0;
  for (int i = 0; i < s_route_n; ++i) if (s_route[i].has_pos) ++plottable;
  return plottable;
}

// ---- Route-replay HUD: a top banner with the current hop's repeater name +
// identifier, and a replay button to re-run the reveal animation. ----
static void routeBannerText(int i, char* buf, size_t n) {
  if (!buf || !n) return;
  if (i < 0 || i >= s_route_n) { buf[0] = '\0'; return; }
  const RouteNode& nd = s_route[i];
  if (nd.name[0] && nd.id[0])  snprintf(buf, n, "%s  \xC2\xB7  %s", nd.name, nd.id);  // name · identifier
  else if (nd.name[0])         snprintf(buf, n, "%s", nd.name);                       // e.g. "You"
  else if (nd.id[0])           snprintf(buf, n, "Repeater %s", nd.id);                // unknown name -> hash only
  else                         snprintf(buf, n, "%s", nd.tag);
}

static void routeHudUpdate() {
  if (!s_route_hud_lbl || s_route_n <= 0) return;
  int idx = s_route_reveal - 1;                  // the hop most recently revealed
  if (idx < 0) idx = 0;
  if (idx >= s_route_n) idx = s_route_n - 1;
  char buf[60];
  routeBannerText(idx, buf, sizeof buf);
  lv_label_set_text(s_route_hud_lbl, buf);
}

static void hideRouteHud() {
  if (s_route_hud) { lv_obj_del(s_route_hud); s_route_hud = nullptr; s_route_hud_lbl = nullptr; }
}

// Replay button -> restart the reveal animation from the first hop.
static void routeReplayRestartCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || s_route_n < 1) return;
  s_route_active = true;
  s_route_reveal = 1;
  fitMapToRoute();
  if (host.getActiveTab() == host.tabIndex) { renderMapTiles(); renderMapMarkers(); }
  routeHudUpdate();
  if (s_route_timer) { lv_timer_del(s_route_timer); s_route_timer = nullptr; }
  if (s_route_reveal < s_route_n)
    s_route_timer = lv_timer_create(routeReplayTick, 650, nullptr);
}

static void showRouteHud() {
  hideRouteHud();
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  s_route_hud = lv_obj_create(lv_layer_top());
  watchRoot(&s_route_hud);
  lv_obj_remove_style_all(s_route_hud);
  // Keep the right-edge map button column (settings/zoom/recenter live at
  // x ~ sw-36) clear: anchor top-LEFT and stop the pill before that column,
  // so the replay button can't land under the settings gear.
  lv_obj_set_size(s_route_hud, sw - 48, 34);
  lv_obj_align(s_route_hud, LV_ALIGN_TOP_LEFT, 6, host.statusHeight() + 4);
  lv_obj_set_style_bg_color(s_route_hud, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_route_hud, LV_OPA_90, LV_PART_MAIN);
  lv_obj_set_style_radius(s_route_hud, 12, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_route_hud, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(s_route_hud, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_set_style_border_opa(s_route_hud, LV_OPA_50, LV_PART_MAIN);
  lv_obj_clear_flag(s_route_hud, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(s_route_hud, LV_OBJ_FLAG_CLICKABLE);   // let pans pass through to the map (the replay button stays clickable)

  s_route_hud_lbl = lv_label_create(s_route_hud);
  lv_label_set_long_mode(s_route_hud_lbl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_route_hud_lbl, sw - 48 - 12 - 42);   // leave room for the replay button
  lv_obj_align(s_route_hud_lbl, LV_ALIGN_LEFT_MID, 8, 0);
  lv_obj_set_style_text_color(s_route_hud_lbl, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_route_hud_lbl, &font14(), LV_PART_MAIN);
  lv_label_set_text(s_route_hud_lbl, "");

  lv_obj_t* rb = lv_btn_create(s_route_hud);
  lv_obj_set_size(rb, 34, 26);
  lv_obj_align(rb, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_set_style_radius(rb, 8, LV_PART_MAIN);
  lv_obj_set_style_bg_color(rb, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN);
  lv_obj_set_style_bg_color(rb, lv_color_hex(colors().COLOR_ACCENT_PRESS), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_add_event_cb(rb, routeReplayRestartCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* rl = lv_label_create(rb);
  useChainedFont(rl);
  lv_label_set_text(rl, LV_SYMBOL_REFRESH);
  lv_obj_set_style_text_color(rl, lv_color_hex(colors().COLOR_ON_ACCENT), LV_PART_MAIN);
  lv_obj_center(rl);
}

// Switch to the map and animate the route that buildRouteFromMessage() captured.
void startRouteReplay() {
  if (s_route_n < 2) return;
  s_route_active = true;        // set BEFORE host.goToTab so onMapTabActivated keeps our fit
  s_route_reveal = 1;          // reveal node 1 immediately; the timer adds the rest
  fitMapToRoute();
  host.goToTab(host.tabIndex);       // runs onMapTabActivated -> renders tiles + markers + overlay
  showRouteHud();              // top banner (current hop name + identifier) + replay button
  routeHudUpdate();
  if (s_route_timer) { lv_timer_del(s_route_timer); s_route_timer = nullptr; }
  if (s_route_reveal < s_route_n)
    s_route_timer = lv_timer_create(routeReplayTick, 650, nullptr);
}

// (The on-map links toggle moved into the Map options popup below; the dotted
// self->contact link lines are now controlled by the "Show link lines" switch.)

// ===== Map options popup (gear button, top-right of the map) =================
// Holds the per-map settings that used to be one-off overlay buttons: the
// self->contact link lines, a "reload tiles" repair action, and an info/credits
// sheet. Centralising them frees the map's right edge and gives room to grow.
static lv_obj_t* s_map_opts_root = nullptr;

void closeMapOptions() {
  if (s_map_opts_root) { host.popupClose(&s_map_opts_root); }
}
static void mapOptionsDismissCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  // Only dismiss on a tap of the dim backdrop itself, not a child (card) tap.
  if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;
  lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
  closeMapOptions();
}

// "Lines" switch inside the options popup.
static void mapOptLinesCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t* sw = lv_event_get_target(e);
  s_map_show_links = lv_obj_has_state(sw, LV_STATE_CHECKED);
#if defined(ESP32)
  touchPrefsSetMapShowLinks(s_map_show_links);   // persist across reboots (PR #61)
#endif
  renderMapMarkers();   // rebuild links to reflect the new state
}

// Topographic map style (OpenTopoMap) toggle — opt-in; default OFF (OpenStreetMap).
// Switches the tile source: topo tiles cache under /tiles/topo and fetch via the
// proxy's /opentopo route. Clears the (z,x,y) fetch-dedup ring — it's shared across
// styles — so the newly-selected style re-queues, then re-renders. host.applyMapChrome
// refreshes the on-map © attribution (OSM vs OpenTopoMap).
static void mapOptTopoCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  const bool topo = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  host.setStyle(topo ? 1 : 0);
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  touchPrefsSetMapStyle(host.style());
  host.resetFetchDedup();
#endif
  freeMapTiles();        // drop in-RAM decodes; next render reads the new style's cache dir
  renderMapTiles();      // loads /tiles/topo (cached) or queues a fetch via /opentopo
  renderMapMarkers();
  host.applyMapChrome(true);  // refresh the © attribution for the new style
}

// "Night mode" switch — invert tile colours at render time. Re-decodes the
// visible tiles so the change is immediate, and persists the choice.
static void mapOptNightCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  host.setNight(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
#if defined(ESP32)
  touchPrefsSetMapNight(host.night());
#endif
  freeMapTiles();        // drop cached decodes so they re-decode with the new mode
  renderMapTiles();
  renderMapMarkers();
  host.applyMapChrome(true);  // re-tint the status bar + tab bar for the new tile brightness
}

// Per-element map text/marker visibility toggles. Coords + tile line just hide
// their corner label; contacts re-renders the markers.
static void mapOptCoordsCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  s_map_show_coords = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
#if defined(ESP32)
  touchPrefsSetMapShowCoords(s_map_show_coords);
#endif
  applyMapTextVis();
}
static void mapOptTileXYZCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  s_map_show_tilexyz = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
#if defined(ESP32)
  touchPrefsSetMapShowTileXYZ(s_map_show_tilexyz);
#endif
  applyMapTextVis();
}
static void mapOptTileDebugCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  s_map_tile_debug = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
#if defined(ESP32)
  touchPrefsSetMapTileDebug(s_map_tile_debug);
#endif
  refreshMapInfoLabel();   // swap the zoom line to/from the diagnostic immediately
}
static void mapOptContactsCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  s_map_show_contacts = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
#if defined(ESP32)
  touchPrefsSetMapShowContacts(s_map_show_contacts);
#endif
  renderMapMarkers();   // add/remove the contact markers (+ their links)
}
static void mapOptDirectOnlyCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  s_map_direct_only = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  renderMapMarkers();
}

static void mapZoomControlsApply();   // fwd (defined near the map build) — reposition zoom controls
// Map zoom-control style toggle (map options popup): ON = +/- buttons, OFF = slider.
static void mapOptZoomButtonsCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  s_map_zoom_buttons = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
#if defined(ESP32)
  touchPrefsSetMapZoomButtons(s_map_zoom_buttons);
#endif
  mapZoomControlsApply();
}

#if CAP_MICROSD
// Map tile source toggle (in the map options popup): ON = tiles live on the microSD
// card — read the user's SD library AND cache Wi-Fi-fetched gaps there (#20), so the
// library grows and downloads survive; OFF = tile server + internal LittleFS cache.

#endif

// "Reload tiles" — delete the currently-visible tiles from the LittleFS cache
// and re-queue them for download, so a corrupted/partial tile in view can be
// repaired without wiping the whole pack. Bounded to the 9 on-screen tiles at
// the current zoom (NOT a bulk area download — stays OSM-policy-friendly).
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)

#endif

// Max contact dots drawn at once. Cycles All -> 200 -> 100 -> 50 -> 25 -> All, in
// place, so the card keeps its compact switch-row shape instead of growing a dropdown.
// "All" is the default and means every positioned contact in view, up to the firmware
// ceiling; the lower steps exist for a board that struggles with a crowded viewport,
// not because the map should be quietly limiting itself.
static void mapOptMarkerCapCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !host.task()) return;
#if defined(ESP32)
  static const uint16_t kSteps[] = { 0, 200, 100, 50, 25 };
  const uint16_t cur = touchPrefsGetMapMarkerCap();
  int idx = 0;
  for (int i = 0; i < (int)(sizeof kSteps / sizeof kSteps[0]); ++i)
    if (kSteps[i] == cur) { idx = i; break; }
  const uint16_t next = kSteps[(idx + 1) % (int)(sizeof kSteps / sizeof kSteps[0])];
  touchPrefsSetMapMarkerCap(next);
  // Relabel the button we were tapped on, so the card reflects it without a rebuild.
  lv_obj_t* b = lv_event_get_target(e);
  lv_obj_t* l = lv_obj_get_child(b, 0);
  char txt[48];
  if (next == 0) snprintf(txt, sizeof txt, LV_SYMBOL_GPS "  %s: %s", TR("Max dots"), TR("All"));
  else           snprintf(txt, sizeof txt, LV_SYMBOL_GPS "  %s: %u", TR("Max dots"), (unsigned)next);
  if (l) lv_label_set_text(l, txt);
  renderMapMarkers();   // redraw with the new limit
#endif
}
static void mapOptReloadCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeMapOptions();
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  host.mapReloadVisibleTiles();
#else
  if (host.task()) host.task()->showAlert(TR("Tile reload needs Wi-Fi build"), 1800);
#endif
}

// Map info / credits sheet — OSM attribution (policy requirement) plus a short
// explanation of how the tile fetching/caching works.
static void mapOptInfoCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeMapOptions();
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_map_opts_root = lv_obj_create(lv_layer_top());
  watchRoot(&s_map_opts_root);
  lv_obj_remove_style_all(s_map_opts_root);
  lv_obj_set_size(s_map_opts_root, sw, sh - host.statusHeight());
  lv_obj_set_pos(s_map_opts_root, 0, host.statusHeight());
  lv_obj_set_style_bg_color(s_map_opts_root, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_map_opts_root, LV_OPA_70, LV_PART_MAIN);
  lv_obj_clear_flag(s_map_opts_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_map_opts_root, mapOptionsDismissCb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* card = lv_obj_create(s_map_opts_root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, sw - 24, (sh - host.statusHeight()) - 24);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  styleSurface(card, colors().COLOR_PANEL, 8);
  lv_obj_set_style_border_color(card, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
  lv_obj_set_scroll_dir(card, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);
  addCloseXBadge(card, mapOptionsDismissCb);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, TR("About the map"));
  lv_obj_set_style_text_font(title, &font14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_pos(title, 0, 2);

  lv_obj_t* lbl = lv_label_create(card);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, sw - 24 - 20);
  lv_obj_set_pos(lbl, 0, 28);
  lv_obj_set_style_text_font(lbl, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(lbl, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  // Attribution header is style-dependent (legal requirement). OpenTopoMap's map
  // style is CC-BY-SA and must be credited; its underlying data is still OSM+SRTM.
  const char* attrib = (host.style() == 1)
    ? TR("Map style \xC2\xA9 OpenTopoMap (CC-BY-SA) \xE2\x80\x94 opentopomap.org\n"
         "Map data \xC2\xA9 OpenStreetMap contributors (ODbL) + SRTM.\n\n")
    : TR("Map data \xC2\xA9 OpenStreetMap contributors.\n"
         "openstreetmap.org/copyright\n"
         "Licensed under the Open Database License (ODbL).\n\n");
  // NOTE: the literal break after \x97 is REQUIRED — "\xC3\x97256" would parse the
  // trailing 256 as part of the hex escape (all hex digits) and emit one garbage byte.
  // Kept as ONE translatable block: it is continuous prose, and splitting it per
  // paragraph would hand translators fragments that only make sense together.
  const char* body = TR(
    "How tiles work:\n"
    "The map is built from 256\xC3\x97" "256 \"slippy\" tiles. Only the tiles for the "
    "area you're viewing are fetched \xE2\x80\x94 there is no bulk pre-download.\n\n"
    "Because this device can't do HTTPS (not enough heap after Wi-Fi starts) "
    "and decodes JPEG far more cheaply than PNG, tiles come from the wadamesh "
    "proxy: it fetches the upstream PNG over HTTPS with an identifying "
    "User-Agent, re-encodes it as JPEG, and caches it. Your device then caches "
    "each tile to its own flash, so a tile is only downloaded once.\n\n"
    "Use Options \xE2\x86\x92 Reload tiles to re-download the tiles currently in "
    "view if one looks corrupted.");
  // Sized for the longest translation, not for English: Hungarian runs ~1.5x and
  // the Cyrillic/Greek files are two bytes per letter in UTF-8.
  static const size_t CREDITS_SZ = 2048;
  static char* credits = (char*)allocateZeroed(CREDITS_SZ);   // lazy-PSRAM (nothing in .bss)
  if (!credits) return;                                // OOM only: skip the credits text
  snprintf(credits, CREDITS_SZ, "%s%s", attrib, body);
  lv_label_set_text(lbl, credits);
}

// Open the options popup: a compact bottom-anchored card with the Lines switch
// + Reload + Info rows.
static void openMapOptions() {
  closeMapOptions();
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_map_opts_root = lv_obj_create(lv_layer_top());
  watchRoot(&s_map_opts_root);
  lv_obj_remove_style_all(s_map_opts_root);
  lv_obj_set_size(s_map_opts_root, sw, sh - host.statusHeight());
  lv_obj_set_pos(s_map_opts_root, 0, host.statusHeight());
  lv_obj_set_style_bg_color(s_map_opts_root, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_map_opts_root, LV_OPA_50, LV_PART_MAIN);
  lv_obj_clear_flag(s_map_opts_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_map_opts_root, mapOptionsDismissCb, LV_EVENT_CLICKED, nullptr);

  const lv_coord_t cardw = sw - 24;
  lv_obj_t* card = lv_obj_create(s_map_opts_root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, cardw, LV_SIZE_CONTENT);                       // grow to the rows...
  lv_obj_set_style_max_height(card, sh - host.statusHeight() - 16, LV_PART_MAIN);  // ...scroll if past the screen
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 8);
  styleSurface(card, colors().COLOR_PANEL, 8);
  lv_obj_set_style_border_color(card, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 12, LV_PART_MAIN);
  lv_obj_set_scroll_dir(card, LV_DIR_VER);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, TR("Map options"));
  lv_obj_set_style_text_font(title, &font14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_pos(title, 0, 0);
  int y = 26;

#if CAP_MICROSD
  // Row: tile source — microSD (offline) vs the tile server. The important one,
  // so it sits at the very top.
  {
    lv_obj_t* tl = lv_label_create(card);
    lv_label_set_text(tl, TR("Tiles from SD card"));
    lv_obj_set_style_text_color(tl, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(tl, &font14(), LV_PART_MAIN);
    lv_obj_set_pos(tl, 2, y + 4);
    lv_obj_t* sw_sd = lv_switch_create(card);
    lv_obj_align(sw_sd, LV_ALIGN_TOP_RIGHT, 0, y);
    if (host.cache().sdTiles) lv_obj_add_state(sw_sd, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw_sd, mapStorageToggleCb, LV_EVENT_VALUE_CHANGED, nullptr);
    y += 40;
  }
#endif

  // Row: Topographic map (OpenTopoMap) — opt-in alternate tile style; default OSM.
  {
    lv_obj_t* tpl = lv_label_create(card);
    lv_label_set_text(tpl, TR("Topographic map"));
    lv_obj_set_style_text_color(tpl, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(tpl, &font14(), LV_PART_MAIN);
    lv_obj_set_pos(tpl, 2, y + 4);
    lv_obj_t* sw_topo = lv_switch_create(card);
    lv_obj_align(sw_topo, LV_ALIGN_TOP_RIGHT, 0, y);
    if (host.style() == 1) lv_obj_add_state(sw_topo, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw_topo, mapOptTopoCb, LV_EVENT_VALUE_CHANGED, nullptr);
    y += 40;
  }

  // Row: Night mode (invert tile colours) — a tile/display setting, kept up top.
  {
    lv_obj_t* nl = lv_label_create(card);
    lv_label_set_text(nl, TR("Night mode (invert)"));
    lv_obj_set_style_text_color(nl, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(nl, &font14(), LV_PART_MAIN);
    lv_obj_set_pos(nl, 2, y + 4);
    lv_obj_t* sw_night = lv_switch_create(card);
    lv_obj_align(sw_night, LV_ALIGN_TOP_RIGHT, 0, y);
    if (host.night()) lv_obj_add_state(sw_night, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw_night, mapOptNightCb, LV_EVENT_VALUE_CHANGED, nullptr);
    y += 40;
  }

  // Row: Zoom controls — slider (default, off) vs a +/- button pair (on, issue #26).
  {
    lv_obj_t* zl = lv_label_create(card);
    lv_label_set_text(zl, TR("Zoom: +/- buttons"));
    lv_obj_set_style_text_color(zl, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(zl, &font14(), LV_PART_MAIN);
    lv_obj_set_pos(zl, 2, y + 4);
    lv_obj_t* sw_zoom = lv_switch_create(card);
    lv_obj_align(sw_zoom, LV_ALIGN_TOP_RIGHT, 0, y);
    if (s_map_zoom_buttons) lv_obj_add_state(sw_zoom, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw_zoom, mapOptZoomButtonsCb, LV_EVENT_VALUE_CHANGED, nullptr);
    y += 40;
  }

  // ---- Section separator + label: the on-map visibility toggles below are a
  //      distinct, lower-stakes group from the tile settings above.
  {
    lv_obj_t* sep = lv_obj_create(card);
    lv_obj_remove_style_all(sep);
    lv_obj_set_size(sep, cardw - 24, 1);
    lv_obj_set_pos(sep, 0, y + 4);
    lv_obj_set_style_bg_color(sep, lv_color_hex(themeRole(0x303438, colors().COLOR_BORDER)), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t* sl = lv_label_create(card);
    lv_label_set_text(sl, TR("Show on map"));
    lv_obj_set_style_text_color(sl, lightSurfaceTextColor(0x8A929B), LV_PART_MAIN);
    lv_obj_set_style_text_font(sl, &font12(), LV_PART_MAIN);
    lv_obj_set_pos(sl, 2, y + 10);
    y += 34;
  }

  // Rows: per-element on-map visibility (link lines / coords / tile z-x-y / contacts).
  {
    struct { const char* label; bool state; lv_event_cb_t cb; } rows[] = {
      { "Show link lines",     s_map_show_links,    mapOptLinesCb      },
      { "Show coordinates",    s_map_show_coords,   mapOptCoordsCb     },
      { "Show tile z/x/y",     s_map_show_tilexyz,  mapOptTileXYZCb    },
      { "Show contacts",       s_map_show_contacts, mapOptContactsCb   },
      { "Direct (0-hop) only", s_map_direct_only,   mapOptDirectOnlyCb },
      { "Tile debug overlay",  s_map_tile_debug,    mapOptTileDebugCb  },
    };
    for (auto& r : rows) {
      lv_obj_t* l = lv_label_create(card);
      lv_label_set_text(l, TR(r.label));
      lv_obj_set_style_text_color(l, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
      lv_obj_set_style_text_font(l, &font14(), LV_PART_MAIN);
      lv_obj_set_pos(l, 2, y + 4);
      lv_obj_t* sw = lv_switch_create(card);
      lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, y);
      if (r.state) lv_obj_add_state(sw, LV_STATE_CHECKED);
      lv_obj_add_event_cb(sw, r.cb, LV_EVENT_VALUE_CHANGED, nullptr);
      y += 40;
    }
  }

  auto mk_row_btn = [&](const char* txt, lv_event_cb_t cb) {
    lv_obj_t* b = lv_btn_create(card);
    lv_obj_set_size(b, cardw - 24, 38);
    lv_obj_set_pos(b, 0, y);
    styleButton(b);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, TR(txt));
    lv_obj_set_style_text_font(l, &font14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
    y += 40;
  };
  {
    char capbuf[48];
    const uint16_t cap_now =
#if defined(ESP32)
        touchPrefsGetMapMarkerCap();
#else
        0;
#endif
    if (cap_now == 0) snprintf(capbuf, sizeof capbuf, LV_SYMBOL_GPS "  %s: %s", TR("Max dots"), TR("All"));
    else              snprintf(capbuf, sizeof capbuf, LV_SYMBOL_GPS "  %s: %u", TR("Max dots"), (unsigned)cap_now);
    // mk_row_btn TR()s its argument; this string is already assembled + translated.
    lv_obj_t* b = lv_btn_create(card);
    lv_obj_set_size(b, cardw - 24, 38);
    lv_obj_set_pos(b, 0, y);
    styleButton(b);
    lv_obj_add_event_cb(b, mapOptMarkerCapCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, capbuf);
    lv_obj_set_style_text_font(l, &font14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
    y += 40;
  }
  mk_row_btn(LV_SYMBOL_REFRESH "  Reload tiles in view", mapOptReloadCb);
  mk_row_btn(LV_SYMBOL_EYE_OPEN "  About / credits",     mapOptInfoCb);

  // Close X (top-right of the card). Added last so move_foreground() keeps it
  // above the title/switch/rows and reliably tappable. Same dismiss path as the
  // backdrop tap. The 32×32 hit area only grazes the link-lines switch (y=30)
  // by ~2px, which is imperceptible.
  // This card scrolls (many rows), and the close badge floats fixed at the
  // top-right. Give it an opaque chip + a faint ring so toggles scrolling under
  // it are cleanly hidden instead of bleeding through the bare glyph.
  lv_obj_t* xb = addCloseXBadge(card, mapOptionsDismissCb);
  lv_obj_set_style_bg_color(xb, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(xb, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(xb, lv_color_hex(themeRole(0x303438, colors().COLOR_BORDER)), LV_PART_MAIN);
  lv_obj_set_style_border_width(xb, 1, LV_PART_MAIN);
}

static void mapOpenOptionsCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  openMapOptions();
}

// ----- Marker tap popup -----
//
// Reuses host.openContactActionSheet (the same sheet you get tapping a row in
// the Contacts tab) so the user gets the full action set — send DM,
// telemetry, range test, etc. — without us duplicating UI.
static void openMarkerPopupForContact(int mesh_idx) {
  if (mesh_idx < 0) {
    // Self marker — show a small toast with our coords. Cheaper than a
    // full popup; the user already has their own profile screen.
    if (host.task()) {
      char buf[40];
      snprintf(buf, sizeof(buf), "Self  %.4f, %.4f",
               host.task()->getNodeLat(), host.task()->getNodeLon());
      host.task()->showAlert(buf, 1500);
    }
    return;
  }
  ContactInfo c;
  if (!host.contactAt((uint32_t)mesh_idx, c)) return;
  const bool is_repeater = (c.type == ADV_TYPE_REPEATER);
  host.openContactActionSheet((uint32_t)mesh_idx, is_repeater, c.name, /*from_map=*/true);
}

// ----- Overlapping-markers picker -----
//
// When 2+ markers fall within the tap-forgiveness radius, the canvas
// dispatcher calls this with the list of hits. We show a vertical list of
// contact rows; tapping one closes the picker and routes through the
// normal openMarkerPopupForContact path. Self (mesh_idx == -1) is shown
// as "(you)" so it's pickable even when other markers sit on top of it.
static lv_obj_t* s_map_picker_root = nullptr;

void closeMapPicker() {
  if (s_map_picker_root) {
    host.popupClose(&s_map_picker_root);
  }
}
static void mapPickerBackdropCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_indev_t* a = lv_indev_get_act();
  if (a) lv_indev_wait_release(a);
  closeMapPicker();
}
static void mapPickerRowCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const int idx = (int)(intptr_t)lv_event_get_user_data(e);
  lv_indev_t* a = lv_indev_get_act();
  if (a) lv_indev_wait_release(a);
  closeMapPicker();
  openMarkerPopupForContact(idx);
}

static void openMapPicker(const int* idxs, int n) {
  closeMapPicker();
  if (n <= 0) return;
  if (n > 6) n = 6;   // cap card height; in practice rare to overlap > a few

  lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_map_picker_root = lv_obj_create(lv_layer_top());
  watchRoot(&s_map_picker_root);
  lv_obj_remove_style_all(s_map_picker_root);
  lv_obj_set_size(s_map_picker_root, sw, sh - host.statusHeight());
  lv_obj_set_pos(s_map_picker_root, 0, host.statusHeight());
  lv_obj_set_style_bg_color(s_map_picker_root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_map_picker_root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_map_picker_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_map_picker_root, mapPickerBackdropCb, LV_EVENT_CLICKED, nullptr);

  // Bigger on the 800-px Tanmatsu panel; unchanged on the smaller boards (PSC no-op).
  const int card_w = PCW(220);
  const int btn_h  = PSC(34);
  const int gap    = PSC(4);
  const int hdr_h  = PSC(30);
  const int pad    = PSC(10);
  const int card_h = hdr_h + n * (btn_h + gap) + pad;

  lv_obj_t* card = lv_obj_create(s_map_picker_root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, pad, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  addCloseXBadge(card, mapPickerBackdropCb);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, TR("Nearby on map"));
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &font14(), LV_PART_MAIN);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, card_w - 2 * pad - 32);   // leave room for X
  lv_obj_set_pos(title, 0, 0);

  int y = hdr_h;
  for (int i = 0; i < n; ++i) {
    const int midx = idxs[i];
    char row_label[40];
    if (midx < 0) {
      snprintf(row_label, sizeof(row_label), LV_SYMBOL_GPS "  (you)");
    } else {
      ContactInfo c;
      if (!host.contactAt((uint32_t)midx, c)) continue;
      const char* icon = (c.type == ADV_TYPE_REPEATER) ? LV_SYMBOL_CHARGE :
                         (c.type == ADV_TYPE_ROOM)     ? LV_SYMBOL_LOOP   :
                                                          LV_SYMBOL_ENVELOPE;
      char nm[24];
      host.copyUtf8ReplacingMissingGlyphs(&font14(), nm, sizeof(nm), c.name);
      snprintf(row_label, sizeof(row_label), "%s  %s",
               icon, nm[0] ? nm : "(unnamed)");
    }
    lv_obj_t* b = lv_btn_create(card);
    lv_obj_set_size(b, card_w - 2 * pad, btn_h);
    lv_obj_set_pos(b, 0, y);
    styleButton(b);
    lv_obj_add_event_cb(b, mapPickerRowCb, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>((intptr_t)midx));
    lv_obj_t* lbl = lv_label_create(b);
    lv_label_set_text(lbl, row_label);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, card_w - 2 * pad - 16);
    lv_obj_set_style_text_font(lbl, &font14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
    y += btn_h + gap;
  }
}

// ===== "Show contact on map" — list every contact that has GPS coords, tap one
// to recenter the map on it. =================================================
static lv_obj_t* s_map_contacts_root = nullptr;
static lv_obj_t* s_map_contacts_list = nullptr;   // the scrollable rows container (rebuilt on sort)
// Sort order for the "contacts on map" list. Persists across opens this session.
enum MapContactsSort : uint8_t { MC_SORT_NAME = 0, MC_SORT_DIST, MC_SORT_HEARD, MC_SORT_COUNT };
static uint8_t s_map_contacts_sort = MC_SORT_DIST;   // distance is the most useful default
static const char* mapContactsSortName(uint8_t s) {
  switch (s) { case MC_SORT_NAME: return "Name"; case MC_SORT_DIST: return "Distance";
               case MC_SORT_HEARD: return "Heard"; default: return "?"; }
}

void closeMapContacts() {
  if (s_map_contacts_root) { host.popupClose(&s_map_contacts_root); }
  s_map_contacts_list = nullptr;
}
static void mapContactsBackdropCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;   // backdrop only
  lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
  closeMapContacts();
}
// Tap a row → recenter the map on that contact + zoom in a touch + close.
static void mapContactsRowCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const int midx = (int)(intptr_t)lv_event_get_user_data(e);
  lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
  closeMapContacts();
  ContactInfo c;
  if (!host.contactAt((uint32_t)midx, c)) return;
  if (c.gps_lat == 0 && c.gps_lon == 0) return;
  s_map_center_lat = (double)c.gps_lat / 1.0e6;
  s_map_center_lon = (double)c.gps_lon / 1.0e6;
  // Zoom in a step (capped) so "show on map" frames the contact closely.
  if (s_map_zoom < k_map_zoom_max) s_map_zoom = (uint8_t)(s_map_zoom + 1);
  renderMapTiles();
  renderMapMarkers();
  refreshMapInfoLabel();
  if (host.task()) {
    char nm[24];
    host.copyUtf8ReplacingMissingGlyphs(&font14(), nm, sizeof(nm), c.name);
    char msg[40];
    snprintf(msg, sizeof(msg), TR("Centered on %s"), nm[0] ? nm : "contact");
    host.task()->showAlert(msg, 1200);
  }
}

// "Show on map" from the contact action sheet: centre the map on the contact's
// last-known GPS and switch to the Map tab. The caller resolves the contact by
// its immutable public key after the sheet has closed.
void showContactOnMap(const ContactInfo &c) {
  if (c.gps_lat == 0 && c.gps_lon == 0) {
    if (host.task()) host.task()->showAlert(TR("No location for this contact"), 1500);
    return;
  }
  s_map_center_lat = (double)c.gps_lat / 1.0e6;
  s_map_center_lon = (double)c.gps_lon / 1.0e6;
  s_map_view_inited = true;   // keep this centre — don't recentre on self on open
  if (s_map_zoom < k_map_zoom_max) s_map_zoom = (uint8_t)(s_map_zoom + 1);
  host.goToTab(host.tabIndex);      // onMapTabActivated renders tiles + markers + overlay
}

// One GPS-bearing contact, with the derived sort keys precomputed.
struct MapContactEntry {
  int      midx;
  char     name[24];
  uint8_t  type;
  int32_t  lat_e6, lon_e6;
  double   dist_km;       // -1 if self has no fix
  uint32_t age_secs;      // 0 = unknown
};

// (Re)build the scrollable row list from the current sort. Separated from the
// popup shell so the Sort button can re-list without rebuilding the card.
static void mapContactsFillList() {
  if (!s_map_contacts_list) return;
  lv_indev_reset(nullptr, nullptr);   // #27: abort any scroll-throw before freeing these rows (UAF on telemetry/position rebuild mid-flick)
  lv_obj_clean(s_map_contacts_list);

  // Collect GPS-bearing contacts + their distance/age keys.
  static MapContactEntry* ents = (MapContactEntry*)allocateZeroed(sizeof(MapContactEntry) * 64);
  int n = 0;
  const double self_lat = host.task() ? host.task()->getNodeLat() : 0.0;
  const double self_lon = host.task() ? host.task()->getNodeLon() : 0.0;
  uint32_t now_secs = 0;
  now_secs = host.currentTime();
  const uint32_t total = host.contactCount();
  for (uint32_t i = 0; i < total && n < 64; ++i) {
    ContactInfo c;
    if (!host.contactAt(i, c)) continue;
    if (c.gps_lat == 0 && c.gps_lon == 0) continue;
    MapContactEntry& e = ents[n];
    e.midx = (int)i;
    e.type = c.type;
    e.lat_e6 = c.gps_lat; e.lon_e6 = c.gps_lon;
    host.copyUtf8ReplacingMissingGlyphs(&font14(), e.name, sizeof(e.name), c.name);
    e.dist_km = (self_lat == 0.0 && self_lon == 0.0) ? -1.0
                : host.contactDistanceKm(self_lat, self_lon,
                                    (double)c.gps_lat / 1.0e6, (double)c.gps_lon / 1.0e6);
    e.age_secs = (now_secs > c.last_advert_timestamp && c.last_advert_timestamp != 0)
                 ? (now_secs - c.last_advert_timestamp) : 0;
    ++n;
  }

  // Sort by the chosen key. (Stable enough for this small list.)
  qsort(ents, n, sizeof(MapContactEntry), [](const void* a, const void* b) -> int {
    const MapContactEntry* ea = static_cast<const MapContactEntry*>(a);
    const MapContactEntry* eb = static_cast<const MapContactEntry*>(b);
    switch (s_map_contacts_sort) {
      case MC_SORT_DIST: {
        // Unknown distance (-1) sorts last.
        const double da = ea->dist_km < 0 ? 1e12 : ea->dist_km;
        const double db = eb->dist_km < 0 ? 1e12 : eb->dist_km;
        if (da < db) return -1; if (da > db) return 1; break;
      }
      case MC_SORT_HEARD: {
        // Most recently heard first; unknown (0) last.
        const uint32_t aa = ea->age_secs ? ea->age_secs : 0xFFFFFFFFu;
        const uint32_t ab = eb->age_secs ? eb->age_secs : 0xFFFFFFFFu;
        if (aa < ab) return -1; if (aa > ab) return 1; break;
      }
      default: break;   // MC_SORT_NAME → fall through to name compare
    }
    return strcasecmp(ea->name, eb->name);
  });

  if (n == 0) {
    lv_obj_t* empty = lv_label_create(s_map_contacts_list);
    lv_label_set_text(empty, TR("No contacts have shared\na GPS location yet."));
    lv_obj_set_style_text_color(empty, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(empty, &font14(), LV_PART_MAIN);
    return;
  }

  // Row width from the display (the list's own width may not be resolved yet on
  // the first fill, right after creation).
  const lv_coord_t rw = (lv_disp_get_hor_res(nullptr) - 24) - 2 * 10 - 8;
  for (int i = 0; i < n; ++i) {
    const MapContactEntry& e = ents[i];
    const char* icon = (e.type == ADV_TYPE_REPEATER) ? LV_SYMBOL_CHARGE :
                       (e.type == ADV_TYPE_ROOM)     ? LV_SYMBOL_LOOP   : LV_SYMBOL_GPS;
    lv_obj_t* b = lv_btn_create(s_map_contacts_list);
    lv_obj_set_size(b, rw, 46);
    styleButton(b);
    lv_obj_set_style_bg_color(b, lv_color_hex(colors().COLOR_CONTROL), LV_PART_MAIN);
    lv_obj_set_style_pad_all(b, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(b, mapContactsRowCb, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>((intptr_t)e.midx));
    // Line 1 (left): icon + name. Line 1 (right): distance · age. Line 2: coords.
    char l1[40];
    snprintf(l1, sizeof(l1), "%s  %s", icon, e.name[0] ? e.name : "(unnamed)");
    lv_obj_t* nl = lv_label_create(b);
    lv_label_set_text(nl, l1);
    lv_label_set_long_mode(nl, LV_LABEL_LONG_DOT);
    // Constrain BOTH width and height to one line: LONG_DOT otherwise wraps to a
    // second line (dotting only the overflow), which a long name then pushed
    // down onto the coords row. A 1-line height forces single-line + ellipsis.
    lv_obj_set_width(nl, rw - 96);            // leave room for the dist/age badge on the right
    lv_obj_set_height(nl, lv_font_get_line_height(&font14()));
    lv_obj_set_style_text_font(nl, &font14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(nl, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_align(nl, LV_ALIGN_TOP_LEFT, 8, 5);

    char dist[16] = "";
    host.formatDistanceBadge(dist, sizeof(dist), self_lat, self_lon, e.lat_e6, e.lon_e6);
    char age[12]; host.formatAgeBadge(age, sizeof(age), e.age_secs);
    char meta[28];
    if (dist[0]) snprintf(meta, sizeof(meta), "%s \xC2\xB7 %s", dist, age);
    else         snprintf(meta, sizeof(meta), "%s", age);
    lv_obj_t* ml = lv_label_create(b);
    lv_label_set_text(ml, meta);
    lv_obj_set_style_text_font(ml, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(ml, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_align(ml, LV_ALIGN_TOP_RIGHT, -8, 6);

    char co[32];
    snprintf(co, sizeof(co), "%.5f, %.5f",
             (double)e.lat_e6 / 1.0e6, (double)e.lon_e6 / 1.0e6);
    lv_obj_t* cl = lv_label_create(b);
    lv_label_set_text(cl, co);
    lv_obj_set_style_text_font(cl, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(cl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_align(cl, LV_ALIGN_BOTTOM_LEFT, 8, -4);
  }
}

// Header Sort button label (shows the CURRENT key) — updated on cycle.
static lv_obj_t* s_map_contacts_sort_lbl = nullptr;
static void mapContactsSortCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  s_map_contacts_sort = (uint8_t)((s_map_contacts_sort + 1) % MC_SORT_COUNT);
  if (s_map_contacts_sort_lbl)
    lv_label_set_text_fmt(s_map_contacts_sort_lbl, LV_SYMBOL_SHUFFLE " %s",
                          mapContactsSortName(s_map_contacts_sort));
  mapContactsFillList();
}

static void openMapContactsList() {
  closeMapContacts();
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_map_contacts_root = lv_obj_create(lv_layer_top());
  watchRoot(&s_map_contacts_root);
  lv_obj_remove_style_all(s_map_contacts_root);
  lv_obj_set_size(s_map_contacts_root, sw, sh - host.statusHeight());
  lv_obj_set_pos(s_map_contacts_root, 0, host.statusHeight());
  lv_obj_set_style_bg_color(s_map_contacts_root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_map_contacts_root, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(s_map_contacts_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_map_contacts_root, mapContactsBackdropCb, LV_EVENT_CLICKED, nullptr);

  const int pad = 10;
  const lv_coord_t card_w = sw - 24;
  const lv_coord_t card_h = sh - host.statusHeight() - 20;
  lv_obj_t* card = lv_obj_create(s_map_contacts_root);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  styleSurface(card, colors().COLOR_PANEL, 8);
  lv_obj_set_style_border_color(card, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, pad, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  addCloseXBadge(card, mapContactsBackdropCb);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, TR(LV_SYMBOL_GPS "  On map"));
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &font14(), LV_PART_MAIN);
  lv_obj_set_pos(title, 0, 2);

  // Sort button (header, right of the title; left of the close X).
  lv_obj_t* sort_b = lv_btn_create(card);
  lv_obj_set_size(sort_b, 108, 26);
  lv_obj_align(sort_b, LV_ALIGN_TOP_RIGHT, -28, 0);
  styleButton(sort_b);
  lv_obj_set_style_bg_color(sort_b, lv_color_hex(colors().COLOR_CONTROL), LV_PART_MAIN);
  lv_obj_add_event_cb(sort_b, mapContactsSortCb, LV_EVENT_CLICKED, nullptr);
  s_map_contacts_sort_lbl = lv_label_create(sort_b);
  lv_label_set_text_fmt(s_map_contacts_sort_lbl, LV_SYMBOL_SHUFFLE " %s",
                        mapContactsSortName(s_map_contacts_sort));
  lv_obj_set_style_text_font(s_map_contacts_sort_lbl, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(s_map_contacts_sort_lbl, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_center(s_map_contacts_sort_lbl);

  // Scrollable list (filled + re-filled by mapContactsFillList).
  s_map_contacts_list = lv_obj_create(card);
  lv_obj_remove_style_all(s_map_contacts_list);
  lv_obj_set_size(s_map_contacts_list, card_w - 2 * pad, card_h - 2 * pad - 34);
  lv_obj_set_pos(s_map_contacts_list, 0, 34);
  lv_obj_set_flex_flow(s_map_contacts_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_map_contacts_list, 6, LV_PART_MAIN);
  lv_obj_set_scroll_dir(s_map_contacts_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_map_contacts_list, LV_SCROLLBAR_MODE_AUTO);
  mapContactsFillList();
}

static void mapOpenContactsCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  openMapContactsList();
}

// ----- Pan -----
//
// Touch-and-drag on the canvas with LIVE preview. PRESSING fires every
// 15 ms (LV_INDEV_DEF_READ_PERIOD); we just translate every existing tile
// and marker by the incremental touch delta. lv_obj_set_pos is cheap
// (style change + invalidation, no decode), so the map slides under the
// finger in real time.
//
// On RELEASED we compute the total delta, convert to lat/lon, and call
// renderMapTiles — which now reuses tile slots whose (z,x,y) is still
// wanted, so only newly-visible tiles get loaded from SPIFFS.
//
// Movement under 6 px on release is treated as a tap; we re-snap any
// pixels of jitter back to the proper grid.
static bool       s_map_panning           = false;
static lv_point_t s_map_pan_anchor        = {0, 0};
static lv_point_t s_map_pan_last          = {0, 0};
static double     s_map_pan_start_lat     = 0.0;
static double     s_map_pan_start_lon     = 0.0;

// Translate the pan layer by (dx, dy). All tiles + markers ride on the
// layer, so this is a SINGLE set_pos regardless of how many children sit
// on top. Compare to the previous per-child loop which made LVGL emit ~38
// dirty rects per PRESSING tick and merge them into a whole-canvas redraw.
void shiftMapChildren(int dx, int dy) {
  if (!s_map_pan_layer || (dx == 0 && dy == 0)) return;
  lv_obj_set_pos(s_map_pan_layer,
                 lv_obj_get_x(s_map_pan_layer) + dx,
                 lv_obj_get_y(s_map_pan_layer) + dy);
}

static void mapCanvasEventCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  const lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_PRESSED  && code != LV_EVENT_PRESSING &&
      code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) return;
  lv_indev_t* indev = lv_indev_get_act();
  if (!indev) return;
  lv_point_t p;
  lv_indev_get_point(indev, &p);

  if (code == LV_EVENT_PRESSED) {
    s_map_pan_anchor    = p;
    s_map_pan_last      = p;
    s_map_pan_start_lat = s_map_center_lat;
    s_map_pan_start_lon = s_map_center_lon;
    s_map_panning       = true;
    return;
  }
  if (!s_map_panning) return;

  if (code == LV_EVENT_PRESSING) {
    // Incremental delta since the last PRESSING tick — slide children.
    const int idx = p.x - s_map_pan_last.x;
    const int idy = p.y - s_map_pan_last.y;
    if (idx == 0 && idy == 0) return;
    shiftMapChildren(idx, idy);
    s_map_pan_last = p;
    return;
  }
  // RELEASED or PRESS_LOST — finalize.
  s_map_panning = false;
  const int tdx = p.x - s_map_pan_anchor.x;
  const int tdy = p.y - s_map_pan_anchor.y;
  // 6-px deadzone: treat as a tap. Snap children back so any sub-pixel
  // jitter we introduced during PRESSING goes away.
  if (tdx > -6 && tdx < 6 && tdy > -6 && tdy < 6) {
    if (tdx != 0 || tdy != 0) shiftMapChildren(-tdx, -tdy);
    // Centralized marker hit-test: find every marker whose center is
    // within 16 px of the tap. One hit → open that contact. Multiple
    // hits (overlapping markers) → open a picker so the user can choose.
    // 16 px is a finger-forgiveness radius, larger than the 14-px marker
    // diameter so an off-center tap still scores.
    // The picker shows at most six contacts. Do not reserve 1 KiB on every
    // drag callback's stack for markers that the picker would discard.
    int hits[6];
    int n_hits = 0;
    const int R2 = 16 * 16;
    for (auto& m : s_map_markers) {
      if (!m.obj) continue;
      lv_area_t a;
      lv_obj_get_coords(m.obj, &a);
      const int mx = (a.x1 + a.x2) / 2;
      const int my = (a.y1 + a.y2) / 2;
      const int ddx = mx - p.x;
      const int ddy = my - p.y;
      if (ddx * ddx + ddy * ddy <= R2) {
        if (n_hits < 6) hits[n_hits++] = m.mesh_idx;
      }
    }
    if (n_hits == 1) {
      openMarkerPopupForContact(hits[0]);
    } else if (n_hits > 1) {
      openMapPicker(hits, n_hits);
    }
    return;
  }
  double start_wx, start_wy;
  latLonToWorldPx(s_map_pan_start_lat, s_map_pan_start_lon, s_map_zoom,
                  &start_wx, &start_wy);
  double new_lat, new_lon;
  // Finger right = content shifts right under finger = center shifts LEFT,
  // so we subtract the touch delta from the start world-px center.
  worldPxToLatLon(start_wx - tdx, start_wy - tdy, s_map_zoom, &new_lat, &new_lon);
  s_map_center_lat = new_lat;
  s_map_center_lon = new_lon;
  // Slot reuse means existing tiles get repositioned (not re-decoded);
  // only the newly-visible 1-3 tiles trigger a SPIFFS read.
  renderMapTiles();
  renderMapMarkers();
  refreshMapInfoLabel();
}

#if defined(HAS_TANMATSU) || defined(TLORA_PAGER) || defined(HAS_THINKNODE_M9)
// Keyboard pan (Ctrl+Arrow on Tanmatsu / WAXD on the pager / the M9's Map-key
// pan mode, all on the Map tab). Mirrors the drag-release math in
// mapCanvasEventCb: synthesize a pixel delta of ~1/4 the visible span in the
// arrow direction, convert it through the same world-px ↔ lat/lon helpers (so
// the lon step automatically scales with the current zoom's degrees-per-pixel)
// and re-render. dir: 0=up(north, +lat) 1=down(south,−lat) 2=left(west,−lon)
// 3=right(east,+lon).
void mapNudge(int dir) {
  if (!s_map_canvas) return;
  // No center yet (no GPS / location) → nothing to pan around.
  if (s_map_center_lat == 0.0 && s_map_center_lon == 0.0) return;
  double cwx, cwy;
  latLonToWorldPx(s_map_center_lat, s_map_center_lon, s_map_zoom, &cwx, &cwy);
  const double step_x = k_map_canvas_w / 4.0;   // quarter of the visible span
  const double step_y = k_map_canvas_h / 4.0;
  switch (dir) {
    case 0: cwy -= step_y; break;   // up    = north = smaller world_y
    case 1: cwy += step_y; break;   // down  = south
    case 2: cwx -= step_x; break;   // left  = west  = smaller world_x
    case 3: cwx += step_x; break;   // right = east
    default: return;
  }
  worldPxToLatLon(cwx, cwy, s_map_zoom, &s_map_center_lat, &s_map_center_lon);
  // Match the touch-drag: it does NOT clear s_map_follow. NB auto-follow
  // recenters on the CENTER-vs-fix delta — the pan itself trips it, no GPS
  // movement needed — so mapAutoFollowTick pauses while the M9's pan mode is
  // active (s_m9_map_pan); on touch boards a drag away simply snaps back on
  // the next 250 ms tick while follow is on, which is that button's contract.
  renderMapTiles();
  renderMapMarkers();
  refreshMapInfoLabel();
}
#endif  // HAS_TANMATSU || TLORA_PAGER || HAS_THINKNODE_M9 (mapNudge)

// ----- Zoom + recenter -----
//
// Zoom PROBES whether the requested level is usable at the current center
// before committing: the center tile must already be cached (offline .jpg
// pack OR Wi-Fi-fetched .png), or Wi-Fi must be up so renderMapTiles can
// download it on the fly. With "zoom packs" we only cache the min + max levels
// per location, so the buttons JUMP to the nearest usable level (skipping the
// uncached levels in between) rather than dead-ending — e.g. one zoom-out from
// z14 lands on the cached z12 overview instead of refusing at the empty z13.
#if defined(ESP32)
static bool mapZoomReachable(uint8_t z) {
  if (!host.mapTileSourceReady()) return true;   // can't probe — don't block the user
  double wx, wy;
  latLonToWorldPx(s_map_center_lat, s_map_center_lon, z, &wx, &wy);
  const long tx = (long)floor(wx / 256.0);
  const long ty = (long)floor(wy / 256.0);
  if (host.tileExistsAt(z, tx, ty)) return true;   // SD /maps/osm pack OR LittleFS /tiles
#if defined(MULTI_TRANSPORT_COMPANION)
  if (host.online()) return true;   // renderMapTiles will fetch it
#endif
  return false;
}
#endif
// One zoom step, shared by the +/- buttons and the keyboard-nav scroll keys.
// Skips levels the current tile source can't show (same walk the buttons did).
// In slider mode the zoom number lives above the slider, so it is on screen the
// whole time the slider is. Buttons mode hides the slider, and the readout was
// hidden with it -- which left the +/- pair with no indication of the level at
// all. Reported by a user who had switched to buttons and could no longer see
// the zoom anywhere. So in buttons mode the readout is flashed beside the
// buttons on each step instead.
static lv_timer_t* s_map_zoomval_hide = nullptr;
static void mapZoomValHideCb(lv_timer_t* t) {
  if (s_map_zoom_val && lv_obj_is_valid(s_map_zoom_val))
    lv_obj_add_flag(s_map_zoom_val, LV_OBJ_FLAG_HIDDEN);
  if (t) lv_timer_del(t);
  s_map_zoomval_hide = nullptr;
}
static void mapZoomValFlash() {
  if (!s_map_zoom_val || !lv_obj_is_valid(s_map_zoom_val)) return;
  lv_label_set_text_fmt(s_map_zoom_val, TR("zoom %d"), (int)s_map_zoom);
  // Anchored to the slider at build time, which is hidden here. Re-anchor to the
  // "+" button so it tracks the column: the buttons are placed with plain
  // set_pos and no status-bar offset, so a hand-computed y would sit low.
  if (s_map_btn_zoomin && lv_obj_is_valid(s_map_btn_zoomin))
    lv_obj_align_to(s_map_zoom_val, s_map_btn_zoomin, LV_ALIGN_OUT_LEFT_MID, -6, 0);
  else
    lv_obj_align(s_map_zoom_val, LV_ALIGN_TOP_RIGHT, -(32 + 10), 4 + 32);
  lv_obj_clear_flag(s_map_zoom_val, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_map_zoom_val);
  if (s_map_zoomval_hide) { lv_timer_del(s_map_zoomval_hide); s_map_zoomval_hide = nullptr; }
  s_map_zoomval_hide = lv_timer_create(mapZoomValHideCb, 1400, nullptr);
  if (s_map_zoomval_hide) lv_timer_set_repeat_count(s_map_zoomval_hide, 1);
}

static void mapZoomStep(bool zoom_in) {
  if (zoom_in  && s_map_zoom >= k_map_zoom_max) return;
  if (!zoom_in && s_map_zoom <= k_map_zoom_min) return;
#if defined(ESP32)
  uint8_t want = 0;
  if (zoom_in) {
    for (uint8_t z = s_map_zoom + 1; z <= k_map_zoom_max; ++z) {
      if (mapZoomReachable(z)) { want = z; break; }
    }
  } else {
    for (uint8_t z = s_map_zoom - 1; z >= k_map_zoom_min; --z) {
      if (mapZoomReachable(z)) { want = z; break; }
      if (z == k_map_zoom_min) break;   // guard: z is unsigned, don't wrap below min
    }
  }
  if (!want) {
    if (host.task()) host.task()->showAlert(zoom_in ? TR("Max zoom for this pack") : TR("Min zoom for this pack"), 1200);
    return;
  }
#else
  const uint8_t want = zoom_in ? (uint8_t)(s_map_zoom + 1) : (uint8_t)(s_map_zoom - 1);
#endif
  s_map_zoom = want;
#if defined(ESP32)
  touchPrefsSetMapZoom(s_map_zoom);   // persist like the slider does
#endif
  renderMapTiles();
  renderMapMarkers();
  refreshMapInfoLabel();
  if (s_map_zoom_buttons) mapZoomValFlash();   // slider mode already shows it permanently
}
static void mapZoomInCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  mapZoomStep(true);
}
static void mapZoomOutCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  mapZoomStep(false);
}
#if CAP_KEYPAD_NAV
// Keyboard-nav hook (called from host.navScrollFocused): with the Map tab front-most
// and nothing modal on top, the scroll keys step the zoom. Returns true when the
// key was consumed as a zoom.
bool navMapZoomIfActive(bool zoom_in) {
  if (host.getActiveTab() != host.tabIndex) return false;
  if (host.anyPopupOpen()) return false;   // a sheet/popup over the map keeps normal scrolling
  mapZoomStep(zoom_in);
  return true;
}
#endif
// Zoom slider overlay. The zoom button toggles it; dragging updates the live
// readout, and releasing applies + persists the chosen level.
static void mapZoomSliderCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  const int z = (int)lv_slider_get_value(lv_event_get_target(e));
  if (s_map_zoom_val) lv_label_set_text_fmt(s_map_zoom_val, TR("zoom %d"), z);   // live target level while dragging
}
static void mapZoomSliderReleaseCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_RELEASED) return;
  int z = (int)lv_slider_get_value(lv_event_get_target(e));
  if (z < (int)k_map_zoom_min) z = k_map_zoom_min;
  if (z > (int)k_map_zoom_max) z = k_map_zoom_max;
  if ((uint8_t)z == s_map_zoom) return;
  s_map_zoom = (uint8_t)z;
#if defined(ESP32)
  touchPrefsSetMapZoom(s_map_zoom);   // persist the user's choice across reboots
#endif
  renderMapTiles();
  renderMapMarkers();
  refreshMapInfoLabel();
}
static void mapZoomToggleCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (!s_map_zoom_slider) return;
  if (lv_obj_has_flag(s_map_zoom_slider, LV_OBJ_FLAG_HIDDEN)) {
    lv_slider_set_value(s_map_zoom_slider, s_map_zoom, LV_ANIM_OFF);
    lv_obj_clear_flag(s_map_zoom_slider, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_map_zoom_slider);
    if (s_map_zoom_val) {
      lv_label_set_text_fmt(s_map_zoom_val, TR("zoom %d"), (int)s_map_zoom);
      lv_obj_clear_flag(s_map_zoom_val, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(s_map_zoom_val);
    }
  } else {
    lv_obj_add_flag(s_map_zoom_slider, LV_OBJ_FLAG_HIDDEN);
    if (s_map_zoom_val) lv_obj_add_flag(s_map_zoom_val, LV_OBJ_FLAG_HIDDEN);
  }
}
// Position + show/hide the map zoom controls per s_map_zoom_buttons. The right-edge
// button column is gear(4), [zoom], recenter, contacts, follow. The zoom section is
// one slot (the "+/-" slider toggle) in slider mode or two (+ / -) in buttons mode,
// so the buttons below shift down accordingly.
static void mapZoomControlsApply() {
  const int X = k_map_canvas_w - 32 - 4;
  const int H = 32;
  auto show = [&](lv_obj_t* b, int yy) { if (b) { lv_obj_set_pos(b, X, yy); lv_obj_clear_flag(b, LV_OBJ_FLAG_HIDDEN); } };
  auto hide = [](lv_obj_t* b) { if (b) lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN); };
  int y = 4 + H;   // below the gear
  if (s_map_zoom_buttons) {
    show(s_map_btn_zoomin,  y); y += H;
    show(s_map_btn_zoomout, y); y += H;
    hide(s_map_btn_zoomtoggle);
    if (s_map_zoom_slider) lv_obj_add_flag(s_map_zoom_slider, LV_OBJ_FLAG_HIDDEN);   // no slider in buttons mode
    if (s_map_zoom_val)    lv_obj_add_flag(s_map_zoom_val, LV_OBJ_FLAG_HIDDEN);
    // Starts hidden; mapZoomStep flashes it on each +/- press.
    if (s_map_zoomval_hide) { lv_timer_del(s_map_zoomval_hide); s_map_zoomval_hide = nullptr; }
  } else {
    show(s_map_btn_zoomtoggle, y); y += H;
    hide(s_map_btn_zoomin);
    hide(s_map_btn_zoomout);
  }
  show(s_map_btn_recenter, y); y += H;
  show(s_map_btn_contacts, y); y += H;
  if (s_map_follow_btn) lv_obj_set_pos(s_map_follow_btn, X, y);
}
static void mapRecenterCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (!host.task()) return;
  s_map_center_lat = host.task()->getNodeLat();
  s_map_center_lon = host.task()->getNodeLon();
  renderMapTiles();
  renderMapMarkers();
  refreshMapInfoLabel();
}

// Auto-follow: while enabled, recenter the map on self whenever the GPS position
// moves meaningfully (or the view was panned away). Called from the map tick.
void mapAutoFollowTick() {
  if (!s_map_follow || !host.task()) return;
#if defined(HAS_M9_KEYBOARD)
  if (s_m9_map_pan) return;   // Map-key pan mode owns the center; follow resumes when pan exits
#endif
  const double lat = host.task()->getNodeLat();
  const double lon = host.task()->getNodeLon();
  if (lat == 0.0 && lon == 0.0) return;                 // no fix yet
  if (fabs(lat - s_map_center_lat) < 5e-5 &&
      fabs(lon - s_map_center_lon) < 5e-5) return;      // ~5 m: ignore GPS jitter
  s_map_center_lat = lat;
  s_map_center_lon = lon;
  renderMapTiles();
  renderMapMarkers();
  refreshMapInfoLabel();
}

// Auto-follow toggle button (bottom of the right-edge column). Highlights when on
// and recenters immediately so the map jumps to your position the moment you enable it.
static void mapFollowToggleCb(lv_event_t* e) {
  if (!acceptsEvent(e)) return;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  s_map_follow = !s_map_follow;
  if (s_map_follow_btn) {
    if (s_map_follow) lv_obj_add_state(s_map_follow_btn, LV_STATE_CHECKED);
    else              lv_obj_clear_state(s_map_follow_btn, LV_STATE_CHECKED);
  }
  if (host.task()) host.task()->showAlert(s_map_follow ? TR("Auto-refresh on") : TR("Auto-refresh off"), 900);
  if (s_map_follow && host.task()) {
    const double lat = host.task()->getNodeLat();
    const double lon = host.task()->getNodeLon();
    if (lat != 0.0 || lon != 0.0) {
      s_map_center_lat = lat;
      s_map_center_lon = lon;
      renderMapTiles();
      renderMapMarkers();
      refreshMapInfoLabel();
    }
  }
}

// Recenters on self GPS and rebuilds the tile grid. Called from host.tabChangedCb
// every time the user switches TO the Map tab.
void onMapTabActivated() {
#if defined(MULTI_TRANSPORT_COMPANION)
  // Fresh fetch slate each map open: forget the "recently queued" dedup ring so
  // tiles that failed or were dropped on a previous visit get another chance
  // (the fetch task still skips anything already on disk). Without this a level
  // that showed "downloading" once would never re-attempt on re-open.
  host.resetFetchDedup();
#endif
  // Nine tile reads + JPEG decodes block this callback for ~2.5 s on the first
  // open after boot (measured: [STALL] ui:lvgl 2597ms, M9_PORT.md). Show the
  // hint and flush a frame FIRST — ahead of the recenter/zoom-probe block
  // below, not just ahead of renderMapTiles. Sitting after that block, the hint
  // could not paint until the probe had already finished, so the whole scan
  // happened with the OUTGOING tab still frozen on the panel and the device
  // looking wedged. Nothing here depends on the centre or zoom.
  if (s_map_status_lbl) {
    lv_label_set_text(s_map_status_lbl, TR("Loading map\xe2\x80\xa6"));
    lv_obj_clear_flag(s_map_status_lbl, LV_OBJ_FLAG_HIDDEN);
  }
  lv_refr_now(NULL);   // drain one frame so the label reaches the panel before we block
  // The entire UI is now clocked at 160 MHz from UITask::begin, so there's
  // no per-tab boost dance here anymore. (Tried 240 MHz briefly — the
  // PSRAM bus tightens enough at that clock for the SJPG decoder to
  // emit occasional RGB565 noise. 160 MHz is the sweet spot.)
  // Recenter on self + auto-snap zoom only on the FIRST map open this session
  // (or until we actually have a location to center on). After that, the user's
  // pan + zoom are remembered across tab switches (issue #5) — the center/zoom
  // statics already survive leaving the tab; this was the only thing wiping
  // them. A route replay sets its own fitted view (s_route_active).
  const bool have_center = !(s_map_center_lat == 0.0 && s_map_center_lon == 0.0);
  if (!s_route_active && (!s_map_view_inited || !have_center)) {
    if (host.task()) {
      const double la = host.task()->getNodeLat();
      const double lo = host.task()->getNodeLon();
      if (!(la == 0.0 && lo == 0.0)) { s_map_center_lat = la; s_map_center_lon = lo; }
    }
    // One-time auto-snap to the highest zoom level the pack actually contains
    // around this center. After this, the user's zoom-in/out taps are honoured
    // verbatim — they can pop above the pack and see "no tile pack" (a clear
    // cue to tap − to go back).
    // A user-saved zoom (persisted across reboots) wins over the auto-snap.
    bool have_saved_zoom = false;
#if defined(ESP32)
    have_saved_zoom = (touchPrefsGetMapZoom() != 0);
#endif
    // Only probe when the answer can actually be USED. Two ways it could not:
    //   • a persisted zoom wins outright, so the result is discarded one line
    //     down — but the walk was paid anyway, on every boot's first map open;
    //   • a still-zero centre has no tile to find at ANY level, so the walk ran
    //     all 17 zoom levels against the card and returned 0. That case is not
    //     first-open-only: s_map_view_inited below only latches once a centre
    //     exists, so without a fix (or a Profile location) the guard above never
    //     closes and this repeated on EVERY map open, forever.
    // bestAvailableZoom has no other caller, so this is pure dead-work removal.
    const bool centred = !(s_map_center_lat == 0.0 && s_map_center_lon == 0.0);
    const uint8_t best = (have_saved_zoom || !centred)
                       ? 0 : bestAvailableZoom(s_map_center_lat, s_map_center_lon);
    if (best != 0 && !have_saved_zoom) s_map_zoom = best;
    if (centred) s_map_view_inited = true;
  }
  // (The "Loading map…" hint + frame flush moved to the top of this function —
  // it has to precede the zoom probe above, not just the tile reads below.)
  renderMapTiles();
  renderMapMarkers();
  refreshMapInfoLabel();
}

// Idle power-save indicator (iPhone Low-Power-Mode style, T-Deck and M9): instead of a separate moon
// glyph, the status-bar battery turns amber while idle power-save is enabled. s_batt_base holds the
// colour the theme/map chrome wants (off-white off-map, black/white over light tiles); applyBattColor
// overlays the amber when power-save is on, so the map-chrome setter and the per-tick refresh share
// one writer and never fight over the battery colour.


// Immersive map chrome: on the Map tab the status bar, bottom info strip and
// tab bar all go transparent so the (full-screen) map shows through behind them.
// Because OSM tiles are LIGHT, the status-bar text/icons are switched to BLACK
// for legibility, and reverted to the normal off-white when leaving the tab.


void makeMapTab(lv_obj_t* tab) {
  destroy();
  if (!tab) return;
#if defined(TLORA_PAGER)
  // Diagnostic/map chrome stays compact at every Pager UI-size preset so the
  // overlays do not hide the map itself.
  const lv_font_t* map_info_font = &lv_font_montserrat_14;
  const lv_font_t* map_control_font = &lv_font_montserrat_20;
#else
  const lv_font_t* map_info_font = &font12();
  const lv_font_t* map_control_font = &font16();
#endif
  lv_obj_set_scroll_dir(tab, LV_DIR_NONE);
  lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(tab, colors().COLOR_BG, 0);
  lv_obj_set_style_pad_all(tab, 0, LV_PART_MAIN);
  s_map_page = tab;
  watchRoot(&s_map_page);

  // Full-screen map. The TILES live on s_map_canvas — a full-screen surface
  // parented to the SCREEN ROOT and pushed to the background, so it sits BEHIND
  // the tabview (and thus paints behind the transparent status bar + tab bar on
  // the Map tab). Touch can't reach it there (the tabview's transparent map page
  // is on top), so panning is driven by a transparent touch-catcher in this
  // page (below). All tile/marker projection reads k_map_canvas_w/h → a
  // full-screen canvas projects to the full screen.
  k_map_canvas_w = lv_disp_get_hor_res(nullptr);
  k_map_canvas_h = lv_disp_get_ver_res(nullptr);
  mapComputeGridRadius();   // size the tile grid to cover this canvas edge-to-edge
  constexpr int kMapInfoH = 34;   // bottom info strip height (floats over the map)

  s_map_canvas = lv_obj_create(lv_scr_act());
  watchRoot(&s_map_canvas);
  lv_obj_remove_style_all(s_map_canvas);
  lv_obj_set_size(s_map_canvas, k_map_canvas_w, k_map_canvas_h);
  lv_obj_set_pos(s_map_canvas, 0, 0);
  lv_obj_set_style_bg_color(s_map_canvas, lv_color_hex(colors().COLOR_FIELD), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_map_canvas, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_map_canvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(s_map_canvas, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_move_background(s_map_canvas);              // behind the tabview
  lv_obj_add_flag(s_map_canvas, LV_OBJ_FLAG_HIDDEN); // shown only on the Map tab
  // Pan layer — a transparent container that holds tile widgets + marker
  // widgets. Sliding the pan layer's position during a finger drag moves
  // everything as a unit (one invalidation per frame instead of 19).
  s_map_pan_layer = lv_obj_create(s_map_canvas);
  lv_obj_remove_style_all(s_map_pan_layer);
  lv_obj_set_size(s_map_pan_layer, k_map_canvas_w, k_map_canvas_h);
  lv_obj_set_pos(s_map_pan_layer, 0, 0);
  lv_obj_set_style_bg_opa(s_map_pan_layer, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_clear_flag(s_map_pan_layer, LV_OBJ_FLAG_SCROLLABLE);
  // Touch events must still reach the canvas — make the pan layer's tap
  // events bubble (LV_OBJ_FLAG_EVENT_BUBBLE) but also let the canvas catch
  // its own PRESSED/PRESSING directly by NOT marking the layer clickable.
  lv_obj_clear_flag(s_map_pan_layer, LV_OBJ_FLAG_CLICKABLE);
  // Placeholder text until tiles render here.
  s_map_status_lbl = lv_label_create(s_map_canvas);
  lv_label_set_long_mode(s_map_status_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_map_status_lbl, k_map_canvas_w - 20);
  lv_obj_set_style_text_color(s_map_status_lbl, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_map_status_lbl, map_info_font, LV_PART_MAIN);
  lv_obj_set_style_text_align(s_map_status_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_text(s_map_status_lbl,
      TR("Map — no tile pack on SPIFFS yet.\n\n"
      "Upload a tile pack to /tiles/<z>/<x>/<y>.jpg "
      "with the host-side generator."));
  lv_obj_center(s_map_status_lbl);

  // Bottom corner read-outs — transparent black text over the map, tucked into
  // the bottom corners UNDER the tab-bar icons: coords bottom-LEFT, marker /
  // download count bottom-RIGHT. A subtle light halo keeps them legible over
  // dark map patches.
  auto style_corner = [&](lv_obj_t* l) {
    lv_obj_set_style_text_color(l, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, map_info_font, LV_PART_MAIN);
    lv_obj_set_style_bg_color(l, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(l, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(l, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(l, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(l, 3, LV_PART_MAIN);
  };
  s_map_info_lbl = lv_label_create(tab);   // coords (bottom-left)
  useChainedFont(s_map_info_lbl);
  style_corner(s_map_info_lbl);
  lv_label_set_text(s_map_info_lbl, "—");
  lv_obj_align(s_map_info_lbl, LV_ALIGN_BOTTOM_LEFT, 2, -2);
  s_map_count_lbl = lv_label_create(tab);  // marker / status count (bottom-right)
  useChainedFont(s_map_count_lbl);
  style_corner(s_map_count_lbl);
  lv_label_set_text(s_map_count_lbl, "");
  lv_obj_align(s_map_count_lbl, LV_ALIGN_BOTTOM_RIGHT, -2, -2);
  // Zoom + tile path at the current center — a second line just under the
  // "© OpenStreetMap" status-bar attribution (top-left).
  s_map_zoom_lbl = lv_label_create(tab);
  useChainedFont(s_map_zoom_lbl);
  style_corner(s_map_zoom_lbl);
  lv_label_set_text(s_map_zoom_lbl, "");
  lv_obj_align(s_map_zoom_lbl, LV_ALIGN_TOP_LEFT, 2, host.statusHeight() + 2);
  applyMapTextVis();   // honour the coords / tile-line visibility toggles
  (void)kMapInfoH;

  // Touch catcher: the tiles live on the background canvas (behind the tabview)
  // and can't receive touch, so a full-page transparent layer in THIS tab page
  // drives pan/drag. It's the FIRST child (created before the overlay buttons +
  // info text below) so those sit on top and still catch their own taps. The
  // catcher and the canvas are both anchored at screen (0,0), so the touch
  // coordinates mapCanvasEventCb reads line up with the tile projection.
  s_map_touch = lv_obj_create(tab);
  lv_obj_remove_style_all(s_map_touch);
  lv_obj_set_size(s_map_touch, lv_pct(100), lv_pct(100));
  lv_obj_set_pos(s_map_touch, 0, 0);
  lv_obj_set_style_bg_opa(s_map_touch, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_clear_flag(s_map_touch, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_map_touch, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s_map_touch, LV_OBJ_FLAG_USER_1);   // pan/drag catcher — never a keyboard-nav focus target
  lv_obj_add_event_cb(s_map_touch, mapCanvasEventCb, LV_EVENT_PRESSED,    nullptr);
  lv_obj_add_event_cb(s_map_touch, mapCanvasEventCb, LV_EVENT_PRESSING,   nullptr);
  lv_obj_add_event_cb(s_map_touch, mapCanvasEventCb, LV_EVENT_RELEASED,   nullptr);
  lv_obj_add_event_cb(s_map_touch, mapCanvasEventCb, LV_EVENT_PRESS_LOST, nullptr);
  lv_obj_move_to_index(s_map_touch, 0);   // keep it beneath the buttons/labels

  // Overlay controls — siblings of the canvas inside the tab, so they
  // float on top of the tile grid and don't get freed when tiles refresh.
  // Layout: right-edge column [zoom+ / zoom- / recenter], 28-px buttons
  // with 4-px gutters.
  auto make_overlay_btn = [&](const char* sym, int y, lv_event_cb_t cb) -> lv_obj_t* {
    lv_obj_t* b = lv_btn_create(tab);
    lv_obj_set_size(b, 32, 28);
    lv_obj_set_pos(b, k_map_canvas_w - 32 - 4, y);
    styleButton(b);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_PART_MAIN);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_set_style_text_color(l, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, map_control_font, LV_PART_MAIN);
    lv_obj_center(l);
    return b;
  };
  // Options gear sits at the TOP of the right-edge column; zoom/recenter below,
  // then a "contacts on map" picker (list of GPS-bearing contacts → recenter).
  make_overlay_btn(LV_SYMBOL_SETTINGS, 4,         mapOpenOptionsCb);   // gear (fixed top)
  // Zoom controls — the "+/-" slider toggle OR a +/- button pair, chosen by
  // s_map_zoom_buttons. All created here; mapZoomControlsApply() (below) positions
  // + shows the right set and shifts recenter/contacts/follow down accordingly.
  s_map_btn_zoomtoggle = make_overlay_btn("+/-", 4 + 32,    mapZoomToggleCb);   // toggles the zoom slider
  s_map_btn_zoomin     = make_overlay_btn("+",   4 + 32,    mapZoomInCb);
  s_map_btn_zoomout    = make_overlay_btn("-",   4 + 32*2,  mapZoomOutCb);
  s_map_btn_recenter   = make_overlay_btn(LV_SYMBOL_GPS,  4 + 32*2, mapRecenterCb);
  s_map_btn_contacts   = make_overlay_btn(LV_SYMBOL_LIST, 4 + 32*3, mapOpenContactsCb);
  // Auto-follow toggle: recenters on self whenever the GPS coords change. Lit
  // (accent) while active via the CHECKED state.
  s_map_follow_btn = make_overlay_btn(LV_SYMBOL_REFRESH, 4 + 32*4, mapFollowToggleCb);
  lv_obj_set_style_bg_color(s_map_follow_btn, lv_color_hex(colors().COLOR_ACCENT), LV_PART_MAIN | LV_STATE_CHECKED);
  lv_obj_set_style_bg_opa(s_map_follow_btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);
  if (s_map_follow) lv_obj_add_state(s_map_follow_btn, LV_STATE_CHECKED);

  // Zoom slider — a full-width overlay along the bottom of the map, hidden until
  // the zoom button is tapped. Reuses the control-centre brightness-slider look.
  s_map_zoom_slider = lv_slider_create(tab);
  lv_obj_set_size(s_map_zoom_slider, k_map_canvas_w - 24, 8);
  lv_obj_align(s_map_zoom_slider, LV_ALIGN_BOTTOM_MID, 0, -(host.tabHeight() + 8));
  lv_slider_set_range(s_map_zoom_slider, k_map_zoom_min, k_map_zoom_max);
  lv_slider_set_value(s_map_zoom_slider, s_map_zoom, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_map_zoom_slider, lv_color_hex(colors().COLOR_TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_map_zoom_slider, LV_OPA_80, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_map_zoom_slider, lv_color_hex(colors().COLOR_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s_map_zoom_slider, lv_color_hex(colors().COLOR_ACCENT), LV_PART_KNOB);
  lv_obj_set_style_pad_all(s_map_zoom_slider, 6, LV_PART_KNOB);
  lv_obj_add_event_cb(s_map_zoom_slider, mapZoomSliderCb,        LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(s_map_zoom_slider, mapZoomSliderReleaseCb, LV_EVENT_RELEASED,      nullptr);
  lv_obj_add_flag(s_map_zoom_slider, LV_OBJ_FLAG_HIDDEN);

  // Live zoom-level readout, centred above the whole slider; shown with it.
  s_map_zoom_val = lv_label_create(tab);
  lv_label_set_text(s_map_zoom_val, TR("zoom"));
  lv_obj_set_style_text_font(s_map_zoom_val, &font14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(s_map_zoom_val, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_map_zoom_val, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_map_zoom_val, LV_OPA_60, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(s_map_zoom_val, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(s_map_zoom_val, 2, LV_PART_MAIN);
  lv_obj_set_style_radius(s_map_zoom_val, 4, LV_PART_MAIN);
  lv_obj_align_to(s_map_zoom_val, s_map_zoom_slider, LV_ALIGN_OUT_TOP_MID, 0, -6);
  lv_obj_add_flag(s_map_zoom_val, LV_OBJ_FLAG_HIDDEN);

  mapZoomControlsApply();   // position + show/hide slider-toggle vs +/- buttons per the pref

  // (OSM attribution now lives in the status bar's left zone on the map tab —
  // see updateGlobalStatusBar.)
}

// Refresh the bottom info strip — called from the periodic refresh tick
// once the Map tab is active. Cheap (one snprintf + label set).
void refreshMapInfoLabel() {
  if (!s_map_info_lbl || !host.task()) return;
  const double lat = host.task()->getNodeLat();
  const double lon = host.task()->getNodeLon();
  // Count contacts with non-zero GPS — the actual map will plot these as
  // markers once tile rendering is in.
  int with_gps = 0;
  for (uint32_t i = 0; i < host.contactCount(); ++i) {
    ContactInfo c;
    if (!host.contactAt(i, c)) continue;
    if (c.gps_lat != 0 || c.gps_lon != 0) ++with_gps;
  }
  // Compact map-coverage hint. When the current view has gaps (tiles not
  // yet on disk), say what's happening instead of just the marker count:
  //   • tiles queued/downloading  -> "downloading N"
  //   • gaps but Wi-Fi is down     -> "Wi-Fi off"
  // Otherwise show the usual marker count.
  char tail[28];
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  const bool wifi_up = (host.online());
  if (host.tileFetchPendingLoad() > 0) {
    snprintf(tail, sizeof(tail), "\xe2\x86\x93 %u downloading",
             (unsigned)host.tileFetchPendingLoad());
  } else if (s_map_last_missing > 0 && !wifi_up) {
    snprintf(tail, sizeof(tail), "Wi-Fi off \xe2\x80\x94 gaps");
  } else if (s_map_markers_wanted > s_map_markers_drawn) {
    // More positioned contacts are on screen than we drew dots for. Say so: the old
    // label counted every positioned contact and the map quietly stopped at its cap,
    // so the two disagreed with no explanation anywhere.
    snprintf(tail, sizeof(tail), TR("%d of %d on map"), s_map_markers_drawn, with_gps);
  } else {
    snprintf(tail, sizeof(tail), TR("%d on map"), with_gps);
  }
#else
  if (s_map_markers_wanted > s_map_markers_drawn)
    snprintf(tail, sizeof(tail), TR("%d of %d on map"), s_map_markers_drawn, with_gps);
  else
    snprintf(tail, sizeof(tail), TR("%d on map"), with_gps);
#endif
  // Coords → bottom-left corner; count/status → bottom-right corner.
  char buf[40];
  if (lat == 0.0 && lon == 0.0) snprintf(buf, sizeof(buf), TR("GPS unset"));
  else                          snprintf(buf, sizeof(buf), "%.4f, %.4f", lat, lon);
  lv_label_set_text(s_map_info_lbl, buf);
  if (s_map_count_lbl) lv_label_set_text(s_map_count_lbl, tail);

  // Zoom + tile path (z / x / y) at the current map center — the "second line"
  // under the © OpenStreetMap attribution. Uses the map center (post-pan), not
  // the self GPS fix.
  if (s_map_zoom_lbl) {
    if (s_map_center_lat == 0.0 && s_map_center_lon == 0.0) {
      lv_label_set_text(s_map_zoom_lbl, "");
    } else {
      double cwx, cwy;
      latLonToWorldPx(s_map_center_lat, s_map_center_lon, s_map_zoom, &cwx, &cwy);
      const long tx = (long)floor(cwx / 256.0);
      const long ty = (long)floor(cwy / 256.0);
      char zbuf[120];
      bool force_show = false;
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
      if (s_map_tile_debug) {
        // Developer tile-pipeline diagnostic (Map options → "Tile debug overlay").
        // Serial is unreadable on the companion build, so this two-line overlay is
        // the only window into the tile pipeline: d = tiles placed/wanted this pass,
        // p = live 128 KB pool buffers, wr = last cache-write outcome (w=ok S=cache
        // full H=heap O=open Z=len e=http J=notjpeg P=short), h = last HTTP code,
        // ok/f = downloads ok/failed, ps = free PSRAM KB, blk = largest contiguous
        // block KB (a tile needs 128 KB contiguous; blk<128 while ps high == frag).
        const int psf = (int)(host.externalFree() / 1024);
        const int psb = (int)(host.externalLargest() / 1024);
        const int pbufs = mapTileLayer.buffers();
        snprintf(zbuf, sizeof zbuf, "z%u d%d/%d p%d wr%c h%d ok%u f%u\nps%dk blk%dk",
                 (unsigned)s_map_zoom, s_tile_dec_ok, s_tile_dec_want, pbufs,
                 (char)host.cache().write, (int)host.cache().http,
                 (unsigned)host.cache().ok, (unsigned)host.cache().failed,
                 psf, psb);
        force_show = true;   // diagnostic overrides the "Show tile z/x/y" pref
      } else
#endif
      {
        snprintf(zbuf, sizeof zbuf, "z%u  %u/%ld/%ld",
                 (unsigned)s_map_zoom, (unsigned)s_map_zoom, tx, ty);
      }
      lv_label_set_text(s_map_zoom_lbl, zbuf);
      // Visibility: the debug overlay forces it on; otherwise follow the
      // "Show tile z/x/y" pref (so turning debug off restores normal behaviour).
      if (force_show || s_map_show_tilexyz) lv_obj_clear_flag(s_map_zoom_lbl, LV_OBJ_FLAG_HIDDEN);
      else                                  lv_obj_add_flag(s_map_zoom_lbl, LV_OBJ_FLAG_HIDDEN);
    }
  }
}


void configure(Host value) { s_map_render_pending = false; host = value; mapTileLayer.configure(value.tiles); }
void processPendingRender() {
  if (!s_map_render_pending) return;
  s_map_render_pending = false;
  if (!s_map_canvas) return;
  renderMapTilesNow();
  refreshMapInfoLabel();
}
View view() { return {s_map_center_lat, s_map_center_lon, s_map_grid_rx, s_map_grid_ry}; }
uint8_t zoom() { return s_map_zoom.load(std::memory_order_relaxed); }
void setCenter(double lat, double lon) { s_map_center_lat = lat; s_map_center_lon = lon; }
int missingTiles() { return s_map_last_missing; }
int routeCount() { return s_route_n; }
bool panMode() { return s_m9_map_pan; }
void setPanMode(bool value) { s_m9_map_pan = value; }
bool optionsOpen() { return s_map_opts_root != nullptr; }
bool pickerOpen() { return s_map_picker_root != nullptr; }
bool contactsOpen() { return s_map_contacts_root != nullptr; }
bool hasTiles() { return s_map_has_pack; }
void showSurface(bool on) {
  if (!on) s_map_render_pending = false;
  if (s_map_canvas) {
    if (on) { lv_obj_clear_flag(s_map_canvas, LV_OBJ_FLAG_HIDDEN); lv_obj_move_background(s_map_canvas); }
    else lv_obj_add_flag(s_map_canvas, LV_OBJ_FLAG_HIDDEN);
  }
  if (s_map_page) lv_obj_set_style_bg_opa(s_map_page, on ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_MAIN);
}
void loadPreferences() {
#if defined(ESP32)
  s_map_show_links = touchPrefsGetMapShowLinks();
  const auto savedZoom = touchPrefsGetMapZoom();
  if (savedZoom >= k_map_zoom_min && savedZoom <= k_map_zoom_max) s_map_zoom = savedZoom;
  s_map_zoom_buttons = touchPrefsGetMapZoomButtons();
  s_map_show_coords = touchPrefsGetMapShowCoords();
  s_map_show_tilexyz = touchPrefsGetMapShowTileXYZ();
  s_map_show_contacts = touchPrefsGetMapShowContacts();
  s_map_tile_debug = touchPrefsGetMapTileDebug();
#endif
}

static bool acceptsEvent(lv_event_t* event) {
  auto* target = lv_event_get_target(event);
  for (auto* parent = target; parent; parent = lv_obj_get_parent(parent)) {
    if (parent == s_map_page || parent == s_map_canvas || parent == s_route_hud ||
        parent == s_map_opts_root || parent == s_map_picker_root || parent == s_map_contacts_root) return true;
  }
  return false;
}
static void rootDeleted(lv_event_t* event) {
  auto** root = static_cast<lv_obj_t**>(lv_event_get_user_data(event));
  if (*root != lv_event_get_target(event)) return; // deletion of an older generation
  *root = nullptr;
  if (root == &s_map_page) destroy();
  else if (root == &s_map_canvas) {
    s_map_render_pending = false;
    freeMapMarkers(); mapTileLayer.clear();
    s_map_pan_layer = s_map_status_lbl = nullptr;
    s_route_active = false;
    if (s_route_timer) { lv_timer_del(s_route_timer); s_route_timer = nullptr; }
    hideRouteHud();
  } else if (root == &s_route_hud) s_route_hud_lbl = nullptr;
  else if (root == &s_map_contacts_root) s_map_contacts_list = s_map_contacts_sort_lbl = nullptr;
}
static void watchRoot(lv_obj_t** root) {
  if (*root) lv_obj_add_event_cb(*root, rootDeleted, LV_EVENT_DELETE, root);
}
void destroy() {
  s_map_render_pending = false;
  s_route_active = false;
  clearRouteReplay();
  if (s_map_zoomval_hide) { lv_timer_del(s_map_zoomval_hide); s_map_zoomval_hide = nullptr; }
  closeMapOptions(); closeMapPicker(); closeMapContacts();
  freeMapMarkers(); mapTileLayer.clear();
  if (s_map_canvas) lv_obj_del(s_map_canvas);
  if (s_map_page) {
    lv_obj_remove_event_cb_with_user_data(s_map_page, rootDeleted, &s_map_page);
    lv_obj_clean(s_map_page);
    s_map_page = nullptr;
  }
  s_map_canvas = s_map_pan_layer = s_map_touch = nullptr;
  s_map_info_lbl = s_map_count_lbl = s_map_status_lbl = nullptr;
  s_map_zoom_lbl = s_map_zoom_slider = s_map_zoom_val = nullptr;
  s_map_follow_btn = s_map_btn_zoomtoggle = s_map_btn_zoomin = s_map_btn_zoomout = nullptr;
  s_map_btn_recenter = s_map_btn_contacts = nullptr;
  s_map_contacts_list = s_map_contacts_sort_lbl = nullptr;
  s_map_panning = s_map_view_inited = s_map_has_pack = s_m9_map_pan = false;
  s_map_last_missing = s_tile_dec_ok = s_tile_dec_want = 0;
}
} } }

// SPDX-License-Identifier: GPL-3.0-or-later
#include "SimPlatform.h"
#include "widgets/MapTileLayer.h"
#include "services/ImageCodec.h"
#include <stdexcept>
#include <cstring>
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
const uint8_t png[] = {137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82, 0, 0, 1, 0, 0, 0, 1, 0, 8, 2, 0, 0, 0, 211, 16, 63, 49, 0, 0, 2, 186, 73, 68, 65, 84, 120, 156, 237, 211, 49, 1, 0, 0, 8, 128, 48, 251, 151, 214, 24, 30, 108, 9, 120, 152, 133, 176, 249, 14, 128, 79, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 154, 1, 72, 51, 0, 105, 6, 32, 205, 0, 164, 25, 128, 52, 3, 144, 102, 0, 210, 12, 64, 218, 1, 220, 183, 44, 212, 37, 136, 211, 131, 0, 0, 0, 0, 73, 69, 78, 68, 174, 66, 96, 130};
int loads = 0, retries = 0, repairs = 0;
bool broken = false;
bool load(uint8_t, int32_t, int32_t, uint8_t** bytes, size_t* length, bool* repairable) {
  ++loads; *length = sizeof png; *repairable = true;
  *bytes = static_cast<uint8_t*>(lvglPsramAlloc(*length));
  if (!*bytes) return false;
  memcpy(*bytes, png, *length);
  if (broken) (*bytes)[0] = 0;
  return true;
}
}
void runMapRegression() {
  using Layer = ui::widgets::MapTileLayer;
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  auto* parent = lv_obj_create(lv_layer_top());
  Layer layer({load, [](uint8_t, int32_t, int32_t, bool corrupt) { ++retries; if (corrupt) ++repairs; }, nullptr, nullptr});
  Layer::View view{50, 14, 14, 240, 226, 4, 0, false, true};
  auto result = layer.render(parent, view);
  check(result.placed > 0 && result.placed == result.wanted && !result.missing, "Map failed to decode PNG tiles");
  check(layer.buffers() <= 4, "Map exceeded buffer budget");
  const int previousLoads = loads;
  layer.render(parent, view);
  check(loads == previousLoads, "Unchanged map reread its tiles");
  auto* descriptor = static_cast<const lv_img_dsc_t*>(lv_img_get_src(lv_obj_get_child(parent, 0)));
  const auto day = *reinterpret_cast<const uint16_t*>(descriptor->data);
  view.night = true;
  layer.render(parent, view);
  descriptor = static_cast<const lv_img_dsc_t*>(lv_img_get_src(lv_obj_get_child(parent, 0)));
  check(*reinterpret_cast<const uint16_t*>(descriptor->data) == static_cast<uint16_t>(~day), "Map reused day pixels after night-mode change");
  view.capacity = 1;
  result = layer.render(parent, view);
  check(result.placed == 1 && layer.buffers() == 1, "Map retained buffers over a reduced budget");
  lv_obj_del(lv_obj_get_child(parent, 0));
  result = layer.render(parent, view);
  check(result.placed == 1, "Map retained an externally deleted image");
  lv_obj_del(parent);
  layer.clear();
  check(layer.buffers() == 0, "Parent deletion leaked map buffers");
  parent = lv_obj_create(lv_layer_top());
  broken = true;
  result = layer.render(parent, view);
  check(!result.placed && result.missing == result.wanted && repairs > 0, "Corrupt cached tiles were not retried");
  broken = false;
  result = layer.render(parent, view);
  check(result.placed == 1, "Map did not recover after a corrupt tile");
  auto* replacement = lv_obj_create(lv_layer_top());
  layer.render(replacement, view);
  check(lv_obj_get_child_cnt(parent) == 0, "Map retained images on the old parent");
  lv_obj_del(parent); lv_obj_del(replacement);
  layer.clear();
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Map regression leaked UI roots");
  int width = 0, height = 0;
  uint8_t tooSmall[2]{};
  check(!ui::images::decodePngToRgb565(png, sizeof png, &width, &height, tooSmall, sizeof tooSmall), "PNG decoder exceeded destination capacity");
}

#include "screens/MapScreen.h"
namespace {
lv_obj_t* mapLabel(lv_obj_t* root, const char* text) {
  if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text)) return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto* found = mapLabel(lv_obj_get_child(root, i), text)) return found;
  return nullptr;
}
void mapClick(lv_obj_t* root, const char* text) {
  auto* found = mapLabel(root, text);
  check(found, "Map control missing");
  lv_event_send(lv_obj_get_parent(found), LV_EVENT_CLICKED, nullptr);
}
}
void runMapScreenRegression(void (*pump)(unsigned)) {
  namespace map = ui::screens::map;
  map::destroy();
  pump(30);
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  const auto surfaces = lv_obj_get_child_cnt(lv_scr_act());
  auto* page = lv_obj_create(lv_layer_top());
  lv_obj_set_size(page, 320, 240);
  map::makeMapTab(page);
  map::setCenter(50, 14);
  map::renderMapTiles(); map::renderMapMarkers();
  check(map::missingTiles() == 0, "Map loaded tiles before the LVGL callback stack unwound");
  pump(30); // exercises the actual UITask drain after lv_timer_handler
  check(map::missingTiles() > 0, "Deferred map render did not process missing tiles");
  // A burst of input keeps the newest viewport and waits for the UI loop.
  map::setCenter(0, 0);
  map::renderMapTiles();
  check(map::missingTiles() > 0, "Map synchronously rendered an intermediate viewport");
  map::setCenter(50, 14);
  for (int i = 0; i < 24; ++i) {
    map::setCenter(50, 14 + i * 0.01);
    map::renderMapTiles();
  }
  const auto finalView = map::view();
  map::processPendingRender();
  check(map::view().lat == finalView.lat && map::view().lon == finalView.lon,
        "Deferred map render restored a stale viewport");
  check(map::missingTiles() > 0, "Coalesced map request lost the final viewport");
  lv_indev_drv_t driver;
  lv_indev_drv_init(&driver);
  driver.type = LV_INDEV_TYPE_POINTER;
  lv_indev_data_t pointer{};
  driver.user_data = &pointer;
  driver.read_cb = [](lv_indev_drv_t* d, lv_indev_data_t* data) {
    *data = *static_cast<lv_indev_data_t*>(d->user_data);
  };
  auto* input = lv_indev_drv_register(&driver);
  check(input, "Map test could not create a pointer");
  lv_obj_update_layout(page);
  for (int i = 0; i < 24; ++i) {
    const auto before = map::view();
    pointer.point = {140, 110}; pointer.state = LV_INDEV_STATE_PRESSED;
    lv_indev_read_timer_cb(input->driver->read_timer);
    pointer.point = {static_cast<lv_coord_t>(i % 2 ? 190 : 90), 130};
    lv_indev_read_timer_cb(input->driver->read_timer);
    pointer.state = LV_INDEV_STATE_RELEASED;
    lv_indev_read_timer_cb(input->driver->read_timer);
    check(map::view().lon != before.lon, "Touch drag did not move the map");
    pump(25);
    const auto oldZoom = map::zoom();
    mapClick(page, i % 2 ? "-" : "+");
    check(map::zoom() != oldZoom, "Map zoom button did not change zoom");
    pump(25);
  }
  lv_indev_delete(input);
  mapClick(page, LV_SYMBOL_SETTINGS);
  check(map::optionsOpen(), "Map options did not open");
  auto* oldOptions = lv_obj_get_child(lv_layer_top(), -1);
  auto* oldInfo = mapLabel(oldOptions, TR(LV_SYMBOL_EYE_OPEN "  About / credits"));
  check(oldInfo, "Map credits button missing");
  map::closeMapOptions();
  mapClick(page, LV_SYMBOL_SETTINGS);
  auto* newOptions = lv_obj_get_child(lv_layer_top(), -1);
  lv_event_send(lv_obj_get_parent(oldInfo), LV_EVENT_CLICKED, nullptr);
  check(map::optionsOpen() && lv_obj_get_child(lv_layer_top(), -1) == newOptions,
        "Closed map popup changed its replacement");
  pump(30);
  check(map::optionsOpen(), "Old map popup deletion cleared the new popup");
  lv_obj_del(newOptions);
  check(!map::optionsOpen(), "Map retained an externally deleted popup");
  mapClick(page, LV_SYMBOL_LIST);
  check(map::contactsOpen(), "Map contacts did not open");
  map::makeMapTab(page); // rebuilding the same borrowed page cleans its old controls
  pump(30);
  check(!map::contactsOpen(), "Map rebuild retained contact popup");
  check(lv_obj_get_child_cnt(lv_scr_act()) == surfaces + 1, "Map rebuild leaked a canvas");
  map::renderMapTiles();
  lv_obj_del(page); // a queued render must not outlive its page/canvas
  map::processPendingRender();
  check(map::missingTiles() == 0, "Deleted map retained a queued redraw");
  pump(50);
  map::destroy();
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots && lv_obj_get_child_cnt(lv_scr_act()) == surfaces,
        "Map screen teardown leaked roots or canvas");
  puts("Map: deferred UITask render, coalesced viewports, 24 touch drags/zooms and pending teardown passed.");
}

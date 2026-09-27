// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
#include "../models/MessageTypes.h"
#include "../widgets/MapTileLayer.h"
class UITask;
struct ContactInfo;
namespace ui { namespace screens { namespace map {
struct CoveragePoint { int32_t lat_e6, lon_e6; int8_t best_rssi; };
struct CacheStatus {
  bool ready = false, sdBackend = false, sdTiles = false, cardPresent = false;
  uint16_t freeKb = 0xffff, ok = 0, failed = 0, openFailures = 0, shortWrites = 0;
  int16_t http = 0;
  char write = '-';
};
struct Host {
  widgets::MapTileLayer::Host tiles;
  uint32_t (*contactCount)();
  bool (*contactAt)(uint32_t, ContactInfo&);
  uint32_t (*currentTime)();
  bool (*hopName)(const uint8_t*, int, char*, size_t);
  bool (*hopPosition)(const uint8_t*, int, double*, double*);
  uint8_t (*repeatCount)(uint32_t);
  uint8_t (*repeatHop)(uint32_t, uint8_t, uint8_t*, uint8_t);
  bool (*online)();
  size_t (*externalTotal)();
  size_t (*externalFree)();
  size_t (*externalLargest)();
  UITask* (*task)();
  lv_coord_t (*statusHeight)();
  lv_coord_t (*tabHeight)();
  int tabIndex;
  void (*copyUtf8ReplacingMissingGlyphs)(const lv_font_t*, char*, size_t, const char*);
  void (*goToTab)(int);
  int (*getActiveTab)();
  bool (*anyPopupOpen)();
  void (*formatAgeBadge)(char*, size_t, uint32_t);
  void (*formatDistanceBadge)(char*, size_t, double, double, int32_t, int32_t);
  double (*contactDistanceKm)(double, double, double, double);
  void (*popupClose)(lv_obj_t**);
  void (*closeDiscoverPage)();
  void (*discoverWardriveTick)();
  void (*queueZoomPackForCenter)();
  bool (*mapTileSourceReady)();
  bool (*tileExistsAt)(uint8_t, long, long);
  int (*tileFetchPendingLoad)();
  const char* (*mapTileRoot)();
  void (*applyMapChrome)(bool);
  lv_event_cb_t mapOptTilesSdCb;
  void (*mapReloadVisibleTiles)();
  void (*openContactActionSheet)(uint32_t, bool, const char*, bool);
  int (*coverageCount)();
  CoveragePoint (*coveragePoint)(int);
  CacheStatus (*cache)();
  uint8_t (*style)();
  bool (*night)();
  void (*setStyle)(uint8_t);
  void (*setNight)(bool);
  void (*resetFetchDedup)();
};
// One screen per device. All entry points except zoom() belong to the UI
// thread. Mutable widget state stays private; callers receive value snapshots.
void configure(Host);
void loadPreferences();
struct View { double lat, lon; int radiusX, radiusY; };
View view();
uint8_t zoom();
void setCenter(double, double);
int missingTiles();
int routeCount();
bool panMode();
void setPanMode(bool);
bool optionsOpen();
bool pickerOpen();
bool contactsOpen();
bool hasTiles();
void showSurface(bool);
void destroy();
constexpr uint8_t k_map_zoom_min = 3, k_map_zoom_max = 19, k_map_zoom_default = 14;

void mapNudge(int dir);
bool navMapZoomIfActive(bool zoom_in);
void refreshMapInfoLabel();
void renderMapTiles();
void renderMapMarkers();
void freeMapTiles();
void onMapTabActivated();
void clearRouteReplay();
void showContactOnMap(const ContactInfo &contact);
void discoverJumpToMapHere();
int buildRouteFromMessage(const ui::MessageTypes::UIMessage& m);
void startRouteReplay();
void closeMapOptions();
void closeMapPicker();
void closeMapContacts();
void shiftMapChildren(int dx, int dy);
void mapAutoFollowTick();
void makeMapTab(lv_obj_t* tab);
} } }

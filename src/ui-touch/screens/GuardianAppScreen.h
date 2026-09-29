// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../widgets/ObjectRef.h"
#include "../services/GuardianDraft.h"
#include "../services/GuardianAppearance.h"
#include "../models/GuardianStatus.h"
#include <string>
#include <vector>
namespace ui { namespace screens {
class GuardianAppScreen {
public:
  void create(lv_obj_t* parent, void (*attach)(lv_obj_t*), void (*hideKeyboard)(), void (*home)() = nullptr, void (*chrome)(const char*) = nullptr);
  bool back();
  void detach();
  void refresh(uint32_t now);
  bool acceptsRefreshKey() const;
  void refreshKey(bool down, uint32_t now);
  void manualRefresh();
private:
  enum Page { Dashboard, Messages, Contacts, Text, Compose, Settings, Info };
  struct Row { uint32_t id = 0; std::string title, detail, call; bool dot = false, saved = false; };
  struct Binding { GuardianAppScreen* owner; int action; };
  struct CachedPage {
    std::string key, revision, signature, body, heading;
    std::vector<Row> rows;
    uint32_t next = 0, total = 0;
    bool hasNext = false;
    size_t bytes() const;
  };
  widgets::ObjectRef _root;
  widgets::ObjectRef _refreshModal;
  lv_obj_t *_notice = nullptr, *_fields[3] = {}, *_priority = nullptr;
  lv_obj_t *_activity = nullptr, *_percent = nullptr, *_bar = nullptr;
  lv_obj_t *_inbox = nullptr, *_unread = nullptr, *_outbox = nullptr, *_links[3] = {};
  lv_obj_t *_arrowRx = nullptr, *_arrowTx = nullptr;
  void (*_attach)(lv_obj_t*) = nullptr;
  void (*_hideKeyboard)() = nullptr;
  void (*_home)() = nullptr;
  void (*_chrome)(const char*) = nullptr;
  Page _page = Dashboard, _requestPage = Dashboard, _infoReturn = Dashboard;
  guardian::Appearance _appearance = guardian::Appearance::Blue;
  Binding _bindings[24]{};
  unsigned _bindingCount = 0, _folder = 0, _source = 2;
  uint32_t _last = 0, _serial = 0, _offset = 0, _next = 0, _message = 0, _total = 0;
  uint32_t _epoch = 0, _requestEpoch = 0, _lastPoll = 0;
  bool _hasNext = false, _loaded = false, _online = false, _awaiting = false;
  bool _confirmNew = false, _needPage = false, _background = false;
  bool _contactPicker = false;
  bool _pageLoaded = false, _cached = false, _refreshing = false, _refreshStatus = false, _refreshPage = false;
  bool _requestRefresh = false, _rDown = false, _rFired = false;
  uint32_t _rAt = 0, _refreshAt = 0;
  guardian::Session _queriedStatus;
  uint8_t _direction = 0;
  std::string _operation, _id, _revision, _body, _heading, _info, _signature;
  std::vector<Row> _rows;
  std::vector<uint32_t> _previous;
  std::vector<CachedPage> _cache;
  guardian::Draft _draft;
  void render();
  void updateDashboard(uint32_t now);
  void captureDraft();
  void go(Page page);
  void requestPage(bool next = false, bool background = false, bool keepOffset = false);
  void send();
  bool request(const char* operation, const std::string& json);
  void response(const std::string& raw, const std::string& error);
  void action(int action);
  std::string cacheKey() const;
  bool restoreCache();
  void saveCache();
  void invalidateCache();
  void showRefresh();
  void finishRefresh();
  const char* noticeText() const;
  uint32_t accent() const;
  uint32_t surface() const;
  void style(lv_obj_t* object, bool active = false);
  lv_obj_t* button(const char* title, int x, int y, int w, int action, int h = 30, bool left = false);
  lv_obj_t* label(const char* value, int x, int y, int w, bool small = false, lv_obj_t* parent = nullptr);
  static bool belongs(lv_obj_t* object, lv_obj_t* root);
  static void clicked(lv_event_t* event);
  static void selection(lv_event_t* event);
};
} }

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../widgets/ObjectRef.h"
#include "../services/GuardianDraft.h"
#include <string>
#include <vector>
namespace ui { namespace screens {
class GuardianAppScreen {
public:
  void create(lv_obj_t* parent, void (*attach)(lv_obj_t*), void (*hideKeyboard)());
  void detach();
  void refresh(uint32_t now);
private:
  enum Page { Dashboard, Messages, Contacts, Text, Compose };
  struct Row { uint32_t id = 0; std::string title, detail, call; };
  struct Binding { GuardianAppScreen* owner; int action; };
  widgets::ObjectRef _root;
  lv_obj_t *_status = nullptr, *_notice = nullptr, *_fields[3] = {}, *_priority = nullptr;
  void (*_attach)(lv_obj_t*) = nullptr;
  void (*_hideKeyboard)() = nullptr;
  Page _page = Dashboard;
  Binding _bindings[20]{};
  unsigned _bindingCount = 0, _folder = 0, _source = 0;
  uint32_t _last = 0, _serial = 0, _offset = 0, _next = 0, _message = 0;
  bool _hasNext = false, _loaded = false, _online = false, _awaiting = false;
  bool _confirmNew = false;
  std::string _operation, _id, _revision, _body, _heading, _info, _transfers;
  std::vector<Row> _rows;
  guardian::Draft _draft;
  void render();
  void captureDraft();
  void requestPage(bool next = false);
  void send();
  bool request(const char* operation, const std::string& json);
  void response(const std::string& raw, const std::string& error);
  void action(int action);
  void button(const char* title, int x, int y, int w, int action, int h = 30);
  lv_obj_t* label(const char* value, int x, int y, int w, bool small = false);
  static void clicked(lv_event_t* event);
  static void selection(lv_event_t* event);
};
} }

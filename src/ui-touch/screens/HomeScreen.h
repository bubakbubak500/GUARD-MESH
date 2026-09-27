// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../widgets/ObjectRef.h"
#include <stddef.h>
#include <stdint.h>

namespace ui {
namespace screens {

// Owns only its body tree. The parent supplies the global bar and navigation.
class HomeScreen {
public:
  enum class Action { Inbox, Advert, Terminal, Discover, Apps, Control };
  struct Preview {
    int index = -1;
    bool channel = false;
    uint16_t unread = 0;
    int messageSlot = -1;
    uint32_t messageSequence = 0;
    char name[33] = {};
    char text[96] = {};
  };
  struct Host {
    void (*action)(Action);
    bool (*current)(const Preview &);
    void (*select)(int, bool);
    void (*sanitize)(const lv_font_t *, char *, size_t, const char *);
  };

  explicit HomeScreen(Host host) : _host(host) {}
  ~HomeScreen();
  HomeScreen(const HomeScreen &) = delete;
  HomeScreen &operator=(const HomeScreen &) = delete;

  bool create(lv_obj_t *parent, int width, int height);
  void reset() { detach(); }
  void refresh(int totalUnread, const Preview *rows, int count);
  lv_obj_t *actionTarget(Action action) const;
  bool active() const { return _root.get() != nullptr; }

private:
  struct ActionBinding { HomeScreen *owner = nullptr; Action action = Action::Inbox; };
  struct RowBinding { HomeScreen *owner = nullptr; int slot = -1; };
  struct Row {
    lv_obj_t *root = nullptr;
    lv_obj_t *name = nullptr;
    lv_obj_t *text = nullptr;
    Preview preview{};
    bool visible = false;
  };

  Host _host;
  widgets::ObjectRef _root;
  lv_obj_t *_messageCard = nullptr;
  lv_obj_t *_empty = nullptr;
  lv_obj_t *_unread = nullptr;
  lv_obj_t *_actions[5] = {};
  Row _rows[3];
  ActionBinding _actionBindings[6];
  RowBinding _rowBindings[3];
  int _width = 0, _height = 0;

  void detach();
  static void actionEvent(lv_event_t *event);
  static void rowEvent(lv_event_t *event);
};

} // namespace screens
} // namespace ui

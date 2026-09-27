// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../widgets/ObjectRef.h"
#include <stdint.h>
namespace ui {
namespace screens {
// One owner per conversation panel. UI thread only. The owner and Host context
// must outlive synchronous callbacks; callbacks may delete/rebuild its LVGL tree.
class ChatComposer {
public:
  enum class Picker { QuickReply, Emoji, Symbols };
  struct Layout {
    lv_coord_t width, viewportHeight, headerHeight, keyboardHeight;
  };
  struct Host {
    void *context;
    void (*input)(void *, lv_event_t *);
    void (*picker)(void *, Picker);
    bool (*send)(void *, const char *snapshot, bool dismissKeyboard);
    void (*closeRoot)(lv_obj_t **);
  };
  ChatComposer() = default;
  ~ChatComposer();
  ChatComposer(const ChatComposer &) = delete;
  ChatComposer &operator=(const ChatComposer &) = delete;
  void build(lv_obj_t *parent, lv_obj_t *messages, const Layout &, const Host &);
  void close();
  void relayout(const Layout &);
  // Call on conversation changes, even when reusing the same widgets/draft.
  void invalidate() { ++_generation; }
  uint32_t generation() const { return _generation; }
  bool send(bool dismissKeyboard = true);
  lv_obj_t *field() const { return _field.get(); }
  lv_obj_t *row() const { return _root.get(); }
  lv_obj_t *counter() const { return _counter.get(); }
  lv_obj_t *emojiButton() const { return _buttons[1].get(); }
  lv_obj_t *symbolButton() const { return _buttons[2].get(); }
  lv_obj_t *sendButton() const { return _buttons[3].get(); }
  lv_coord_t height() const { return _height ? _height : baseHeight(); }
  static lv_coord_t baseHeight();

private:
  static void fieldEvent(lv_event_t *);
  static void buttonEvent(lv_event_t *);
  static void rootDeleted(lv_event_t *);
  void detach();
  void grow();
  void count();
  void place();
  lv_coord_t fieldWidth() const;
  widgets::ObjectRef _root, _field, _counter, _messages, _buttons[4];
  Host _host{};
  Layout _layout{};
  lv_coord_t _height = 0;
  uint32_t _generation = 0, _pressGeneration[4]{};
  bool _pressed[4]{}, _sending = false;
};
} // namespace screens
} // namespace ui

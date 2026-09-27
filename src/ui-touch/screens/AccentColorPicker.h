// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../models/ColorChoice.h"
#include "../widgets/ObjectRef.h"
namespace ui {
namespace screens {
class AccentColorPicker {
public:
  struct Host {
    void *context;
    uint32_t (*read)(void *);
    bool (*save)(void *, uint32_t);
    void (*apply)(void *, uint32_t);
    void (*restart)(void *);
    void (*attachField)(void *, lv_obj_t *);
    void (*syncField)(void *);
    void (*alert)(void *, const char *, int);
    void (*closeRoot)(lv_obj_t **);
    lv_coord_t (*contentTop)();
  };
  explicit AccentColorPicker(Host host) : _host(host) {}
  ~AccentColorPicker() {
    _destroying = true;
    close();
  }
  AccentColorPicker(const AccentColorPicker &) = delete;
  AccentColorPicker &operator=(const AccentColorPicker &) = delete;
  void open();
  void close();
  bool isOpen() const { return _root.get(); }

private:
  static void deleted(lv_event_t *);
  static void event(lv_event_t *);
  void unbind(lv_obj_t *);
  void detach();
  void select(uint32_t, bool);
  void save();
  Host _host;
  widgets::ObjectRef _root, _field, _preview, _swatches[colorChoice::AccentCount], _save, _reset, _close;
  uint32_t _generation = 0, _value = colorChoice::DefaultAccent;
  bool _syncing = false, _destroying = false;
};
} // namespace screens
} // namespace ui

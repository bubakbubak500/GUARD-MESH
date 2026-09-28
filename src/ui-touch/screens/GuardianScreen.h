// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../widgets/ObjectRef.h"
#include "../services/GuardianLink.h"
namespace ui { namespace screens {
class GuardianScreen {
public:
  void create(lv_obj_t* parent, void (*command)(guardian::Command));
  void refresh(uint32_t now);
  void detach() { _root.set(nullptr); _command = nullptr; }
private:
  widgets::ObjectRef _root;
  lv_obj_t *_state = nullptr, *_counts = nullptr, *_radios = nullptr, *_name = nullptr;
  lv_obj_t *_enable = nullptr, *_pair = nullptr;
  void (*_command)(guardian::Command) = nullptr;
  uint32_t _last = 0;
  static void clicked(lv_event_t* event);
};
} }

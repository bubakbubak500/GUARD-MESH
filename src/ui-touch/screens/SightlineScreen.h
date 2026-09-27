// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../services/SightlineJob.h"
#include <lvgl.h>
namespace ui {
namespace screens {
namespace sightlineScreen {
struct Host {
  SightlineJob *job;
  bool (*ensureExecutor)();
  void (*closeRoot)(lv_obj_t **);
  lv_coord_t (*contentTop)();
  void (*diagnostic)(const char *);
};
struct Selection {
  sightline::Path path;
  char name[36]; // already normalized for the display font by the host
  char server[80];
  double frequency;
  bool miles, networkReady;
};
void configure(const Host &);
void open(const Selection &);
void close();
bool isOpen();
void poll(); // UI thread, including when closed: drains retired results
} // namespace sightlineScreen
} // namespace screens
} // namespace ui

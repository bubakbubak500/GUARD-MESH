// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"
class UITask;
namespace fs { class FS; }
namespace ui { namespace lua {
// Explicit bridges to UI-owned facilities. All callbacks run on the UI thread.
// Mesh protocol types and the legacy Lua C ABI stay inside the implementation.
struct Host {
  UITask* (*task)();
  fs::FS* (*filesystem)();
  void (*path)(char*, size_t, const char*);
  fs::FS* (*sdFilesystem)(bool*);
  bool (*sdReadFailed)();
  bool (*sdClearAttributes)(const char*);
  lv_coord_t (*barHeight)();
  void (*battery)(uint16_t*, int*, bool*);
  void (*confirm)(const char*, const char*, void (*)());
  void (*closePrompt)();
  void (*prompt)(const char*, const char*, void (*)(const char*));
  const char* (*installedId)(int);
};
void configure(Host host);
void writePermissions(const char* app_id, int mask);
int permissionIds(char (*ids)[24], int capacity);
} }

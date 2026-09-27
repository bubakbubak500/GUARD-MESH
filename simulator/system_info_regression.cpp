// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/SystemInfoScreen.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace {
using Screen = ui::screens::SystemInfoScreen;
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
struct Context {
  Screen *screen = nullptr;
  lv_obj_t *body = nullptr, *replacement = nullptr;
  unsigned calls = 0, version = 1;
  bool erase = false, replace = false, grow = false;
};
void read(void *value, bool, ui::diagnostics::Snapshot &snapshot) {
  auto &context = *static_cast<Context *>(value);
  ++context.calls;
  const auto version = context.version;
  if (context.erase) {
    context.erase = false;
    lv_obj_del(context.body);
    context.body = nullptr;
  }
  if (context.replace) {
    context.replace = false;
    ++context.version;
    context.screen->build(context.replacement, true);
  }
  snapshot.hardware.available = true;
  snapshot.hardware.dram.total = 100 * 1024;
  snapshot.hardware.dram.free = version * 1024;
  snprintf(snapshot.firmware, sizeof snapshot.firmware, "version %u", version);
  if (context.grow) {
    strcpy(snapshot.historySave, "FAIL\n  first line\n  second line\n  third line");
    snapshot.stallCount = 6;
    for (auto &stall : snapshot.stalls)
      strcpy(stall.tag, "loop");
  }
}
lv_obj_t *body() {
  auto *object = lv_obj_create(lv_layer_top());
  lv_obj_set_size(object, 240, 240);
  return object;
}
} // namespace
void runSystemInfoRegression() {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  Context context;
  Screen screen({&context, read});
  context.screen = &screen;
  auto *first = context.body = body();
  screen.build(first);
  check(context.calls == 1 && lv_obj_get_child_cnt(first) == 2, "System info initial snapshot missing");
  auto *live = lv_obj_get_child(first, 0), *rest = lv_obj_get_child(first, 1);
  check(strstr(lv_label_get_text(rest), "version 1"), "System info build text missing");
  const char *sameLive = lv_label_get_text(live), *sameRest = lv_label_get_text(rest);
  screen.refresh(UINT32_MAX - 500);
  check(context.calls == 2 && sameLive == lv_label_get_text(live) && sameRest == lv_label_get_text(rest),
        "Unchanged diagnostic text was reallocated");
  screen.refresh(498);
  check(context.calls == 2, "System info timer failed across wrap");
  context.grow = true;
  screen.refresh(499);
  check(context.calls == 3, "System info did not refresh after wrap");
  lv_obj_update_layout(first);
  check(lv_obj_get_y(rest) >= lv_obj_get_y(live) + lv_obj_get_height(live),
        "Growing diagnostic text overlapped the slow tier");
  auto *second = context.body = body();
  ++context.version;
  screen.build(second);
  lv_obj_del(first);
  screen.refresh(2000);
  check(strstr(lv_label_get_text(lv_obj_get_child(second, 1)), "version 2"),
        "Deleting old diagnostics detached the current screen");
  context.erase = true;
  screen.refresh(3000);
  const auto afterDelete = context.calls;
  screen.refresh(4000);
  check(context.calls == afterDelete, "Deleted diagnostics still read hardware");
  context.body = body();
  context.replacement = body();
  context.replace = true;
  screen.build(context.body);
  check(lv_obj_get_child_cnt(context.replacement) == 1 &&
            strstr(lv_label_get_text(lv_obj_get_child(context.replacement, 0)), "free  3 KB"),
        "A retired snapshot overwrote or starved the replacement memory page");
  const auto memoryCalls = context.calls;
  screen.refresh(10000);
  check(context.calls == memoryCalls, "Memory detail changed from one-shot to live");
  lv_obj_del(context.body);
  lv_obj_del(context.replacement);
  screen.detach();
  auto *remaining = body();
  {
    Screen temporary({&context, read});
    temporary.build(remaining);
  }
  lv_obj_del(remaining);
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "System info left a page behind");
  std::puts("System info: refresh/wrap, unchanged text, flex growth, DELETE, replacement and owner teardown "
            "passed.");
}

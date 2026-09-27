// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.h"
#include "screens/BatteryHistoryScreen.h"
#include "services/BatteryHistory.h"
#include <FS.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
namespace {
using History = ui::BatteryHistory;
using Screen = ui::screens::BatteryHistoryScreen;
using Result = History::Result;
void check(bool ok, const char *text) {
  if (!ok)
    throw std::runtime_error(text);
}
void put(fs::FS &disk, const char *path, const std::string &text) {
  auto file = disk.open(path, FILE_WRITE);
  check(file && file.write(reinterpret_cast<const uint8_t *>(text.data()), text.size()) == text.size(),
        "Battery test seed failed");
}
std::string get(fs::FS &disk, const char *path) {
  auto file = disk.open(path, FILE_READ);
  if (!file)
    return {};
  std::string text(file.size(), '\0');
  check(file.read(reinterpret_cast<uint8_t *>(&text[0]), text.size()) == text.size(),
        "Battery test read failed");
  return text;
}
struct Context {
  fs::FS disk, other;
  bool useOther = false;
  unsigned resolves = 0, failures = 0, samples = 0, alerts = 0;
  Screen *screen = nullptr;
  bool eraseOnSnapshot = false, replaceOnSnapshot = false, replaceOnClose = false;
};
Context *closingContext = nullptr;
History::Host historyHost(Context &context) {
  return {&context,
          [](void *p) {
            auto &c = *static_cast<Context *>(p);
            ++c.resolves;
            History::Backend backend;
            backend.filesystem = c.useOther ? &c.other : &c.disk;
            return backend;
          },
          [](void *p, const History::Backend &) { ++static_cast<Context *>(p)->failures; },
          [](void *p) {
            ++static_cast<Context *>(p)->samples;
            return ui::battery::Sample{1700002700, 3900, 80, 70};
          }};
}
lv_obj_t *label(lv_obj_t *root, const char *text) {
  if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = label(lv_obj_get_child(root, i), text))
      return found;
  return nullptr;
}
lv_obj_t *findChart(lv_obj_t *root) {
  if (lv_obj_check_type(root, &lv_chart_class))
    return root;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i)
    if (auto *found = findChart(lv_obj_get_child(root, i)))
      return found;
  return nullptr;
}
lv_obj_t *button(lv_obj_t *root, const char *text) {
  auto *caption = label(root, text);
  check(caption, "Battery button missing");
  return lv_obj_get_parent(caption);
}
void click(lv_obj_t *object) { lv_event_send(object, LV_EVENT_CLICKED, nullptr); }
lv_obj_t *root() {
  auto *title = label(lv_layer_top(), TR("Battery \xe2\x80\x94 last 24h"));
  check(title, "Battery history root missing");
  return lv_obj_get_parent(lv_obj_get_parent(title));
}
unsigned allocations = 0, releases = 0;
bool outOfMemory = false;
const char *original = "1700000000\t2023-11-14 22:13\t4200\t100\t80\n1700000900\t2023-11-14 "
                       "22:28\t4100\t90\n1700001800\t2023-11-14 22:43\t4000\t80\t160\n";
} // namespace
void runBatteryHistoryRegression(void (*pump)(unsigned)) {
  Context context;
  closingContext = &context;
  context.disk.enableMemory();
  context.other.enableMemory();
  History history(historyHost(context));
  ui::battery::Sample samples[ui::battery::HistoryCapacity];
  size_t count = 77;
  check(history.load(samples, 288, count) == Result::Ok && count == 0,
        "Missing battery history is not empty");
  put(context.disk, "/battery.log", original);
  check(history.load(samples, 288, count) == Result::Ok && count == 3 && samples[1].cpuMHz == 0,
        "Legacy battery columns not loaded");
  const ui::battery::Sample next{1700002700, 3900, 80, 70};
  auto resolves = context.resolves;
  check(history.append(next) == Result::Ok && context.resolves == resolves + 1,
        "Battery append changed backend mid-operation");
  check(!context.disk.exists("/battery.tmp") && !context.disk.exists("/battery.bak"),
        "Battery commit leaked staging files");
  for (size_t failure : {size_t(1), size_t(2)}) {
    put(context.disk, "/battery.log", original);
    context.disk.failRenameOnCall(failure);
    check(history.append(next) == Result::CommitFailed, "Battery rename failure hidden");
    check(get(context.disk, "/battery.log") == original, "Battery rename lost original");
    check(!context.disk.exists("/battery.tmp") && !context.disk.exists("/battery.bak"),
          "Battery rollback left ordinary debris");
    context.disk.failRenameOnCall(0);
  }
  for (size_t budget : {size_t(1), strlen(original) + 1, strlen(original) * 2 + 1}) {
    context.disk.limitReads(budget);
    check(history.append(next) != Result::Ok, "Battery short read accepted");
    context.disk.allowReads();
    check(get(context.disk, "/battery.log") == original, "Battery short read lost source");
    check(!context.disk.exists("/battery.tmp"), "Battery read failure leaked temp");
  }
  context.disk.limitWrites(5);
  check(history.append(next) == Result::WriteFailed, "Battery short write accepted");
  context.disk.allowWrites();
  check(get(context.disk, "/battery.log") == original, "Battery short write replaced source");
  context.disk.failRenameOnCall(2, true);
  check(history.append(next) == Result::RecoveryRequired, "Battery failed rollback not reported");
  context.disk.failRenameOnCall(0);
  check(get(context.disk, "/battery.bak") == original && context.disk.exists("/battery.tmp"),
        "Battery recovery data removed");
  History restarted(historyHost(context));
  check(restarted.append(next) == Result::RecoveryRequired && restarted.clear() == Result::RecoveryRequired,
        "Battery restart overwrote pending recovery");
  check(restarted.load(samples, 288, count) == Result::RecoveryRequired,
        "Missing primary hid recovery backup");
  context.disk.enableMemory();
  std::string many;
  for (unsigned i = 0; i < 300; ++i) {
    char line[80];
    snprintf(line, sizeof line, "%u\t----------------\t4000\t80\t%u\n", i, i);
    many += line;
  }
  put(context.disk, "/battery.log", many);
  check(history.load(samples, 4, count) == Result::Ok && count == 4 && samples[0].cpuMHz == 296 &&
            samples[3].cpuMHz == 299,
        "Battery loader kept oldest rather than newest tail");
  check(history.append({300, 3900, 300, 70}) == Result::Ok, "Battery bounded append failed");
  check(history.load(samples, 288, count) == Result::Ok && count == 288 && samples[0].cpuMHz == 13 &&
            samples[287].cpuMHz == 300,
        "Battery log did not enforce bounded retention");
  put(context.disk, "/battery.log", "1\tdate\t4000\t50\n1700000000\tdate\t4100\t70\n");
  check(history.append(next) == Result::Ok && history.load(samples, 288, count) == Result::Ok && count == 2,
        "Battery 24-hour retention failed");
  put(context.disk, "/battery.log", "broken\n");
  check(history.append(next) == Result::InvalidData && get(context.disk, "/battery.log") == "broken\n",
        "Malformed log overwritten");
  check(history.clear() == Result::Ok, "Battery clear failed");
  const auto captured = history.resolve();
  put(context.disk, "/battery.log", original);
  put(context.other, "/battery.log", original);
  context.useOther = true;
  check(history.clear(captured) == Result::Ok && !context.disk.exists("/battery.log") &&
            context.other.exists("/battery.log"),
        "Captured battery clear followed a new backend");
  context.useOther = false;
  const auto beforeSamples = context.samples;
  history.tick(UINT32_MAX - 100);
  history.tick(99);
  check(context.samples == beforeSamples + 1 && !history.due(299898) && history.due(299899),
        "Battery interval wrap failed");
  history.tick(299899);
  check(context.samples == beforeSamples + 2, "Battery period did not sample again");

  // Actual shared LVGL screen and owned confirmation.
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  put(context.disk, "/battery.log", original);
  Screen::Host host{&context,
                    [](void *p, Screen::Snapshot &snapshot) {
                      auto &c = *static_cast<Context *>(p);
                      snapshot.cpuMHz = 160;
                      strcpy(snapshot.lastWake, "test");
                      strcpy(snapshot.blocker, "test blocker");
                      if (c.eraseOnSnapshot) {
                        c.eraseOnSnapshot = false;
                        c.screen->close();
                      }
                      if (c.replaceOnSnapshot) {
                        c.replaceOnSnapshot = false;
                        c.screen->close();
                        c.screen->open();
                      }
                    },
                    [](void *p, const char *, int) { ++static_cast<Context *>(p)->alerts; },
                    [] { return lv_coord_t(24); },
                    [](lv_obj_t **object) {
                      auto *old = *object;
                      *object = nullptr;
                      lv_obj_del_async(old);
                      if (closingContext && closingContext->replaceOnClose) {
                        closingContext->replaceOnClose = false;
                        closingContext->screen->close();
                        closingContext->screen->open();
                      }
                    },
                    nullptr,
                    [](size_t size) -> void * {
                      if (outOfMemory)
                        return nullptr;
                      ++allocations;
                      return std::malloc(size);
                    },
                    [](void *pointer) {
                      ++releases;
                      std::free(pointer);
                    }};
  Screen screen(history, host);
  context.screen = &screen;
  screen.open();
  auto *first = root();
  auto *chart = findChart(first);
  check(chart && lv_chart_get_point_count(chart) == 3 && allocations == releases,
        "Battery chart did not copy owned sample data");
  auto *oldCpu = button(first, "CPU");
  auto *oldClear = button(first, LV_SYMBOL_TRASH);
  click(oldCpu);
  const auto opened = allocations;
  click(oldCpu);
  click(oldClear);
  check(allocations == opened && !label(lv_layer_top(), TR("Clear battery history?")),
        "Old battery tree controls replacement");
  pump(40);
  auto *second = root();
  click(button(second, LV_SYMBOL_TRASH));
  auto *oldConfirm = button(lv_layer_top(), TR("Clear"));
  screen.close();
  screen.open();
  click(oldConfirm);
  check(context.disk.exists("/battery.log"), "Stale battery confirmation erased history");
  pump(40);
  click(button(root(), LV_SYMBOL_TRASH));
  context.replaceOnClose = true;
  click(button(lv_layer_top(), TR("Clear")));
  check(context.disk.exists("/battery.log"), "Reentrant confirmation dismissal erased replacement history");
  pump(40);
  put(context.disk, "/battery.bak", "recovery");
  click(button(root(), LV_SYMBOL_TRASH));
  const auto alerts = context.alerts;
  click(button(lv_layer_top(), TR("Clear")));
  check(context.alerts == alerts + 1 && context.disk.exists("/battery.log") && screen.isOpen(),
        "Failed battery clear hid the error or removed the log");
  context.disk.remove("/battery.bak");
  pump(40);
  click(button(root(), LV_SYMBOL_TRASH));
  context.useOther = true;
  click(button(lv_layer_top(), TR("Clear")));
  check(!context.disk.exists("/battery.log") && context.other.exists("/battery.log"),
        "Battery screen clear lost captured backend");
  pump(40);
  click(button(root(), LV_SYMBOL_TRASH));
  auto *deletedConfirm = button(lv_layer_top(), TR("Clear"));
  int deletes = 0;
  for (int i = 0; i < 3; ++i)
    lv_obj_add_event_cb(
        root(), [](lv_event_t *e) { ++*static_cast<int *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE,
        &deletes);
  lv_obj_del(root());
  check(deletes == 3, "Battery teardown skipped later DELETE observers");
  click(deletedConfirm);
  check(!screen.isOpen(), "Deleting battery root left live owner state");
  check(context.other.exists("/battery.log"), "Deleted battery page confirmation erased history");
  pump(40);
  context.useOther = false;
  outOfMemory = true;
  screen.open();
  check(label(root(), TR("Out of memory")), "Battery OOM hidden");
  screen.close();
  pump(40);
  outOfMemory = false;
  context.eraseOnSnapshot = true;
  screen.open();
  check(!screen.isOpen() && allocations == releases, "Battery close during snapshot leaked data");
  pump(40);
  context.replaceOnSnapshot = true;
  screen.open();
  check(screen.isOpen() && allocations == releases, "Battery reentrant open lost replacement");
  pump(40);
  screen.close();
  pump(40);
  put(context.disk, "/battery.log", original);
  lv_obj_t *retired, *retiredConfirm;
  {
    Screen temporary(history, host);
    temporary.open();
    retired = button(root(), "CPU");
    click(button(root(), LV_SYMBOL_TRASH));
    retiredConfirm = button(lv_layer_top(), TR("Clear"));
  }
  click(retired);
  click(retiredConfirm);
  check(context.disk.exists("/battery.log"), "Destroyed battery owner left an active confirmation");
  pump(40);
  check(allocations == releases && lv_obj_get_child_cnt(lv_layer_top()) == roots,
        "Battery owner teardown leaked callbacks or roots");
  closingContext = nullptr;
  puts("Battery history: bounded tail, legacy rows, short I/O, commit/rollback/restart, wrap, owned buffers "
       "and stale confirmations passed.");
}

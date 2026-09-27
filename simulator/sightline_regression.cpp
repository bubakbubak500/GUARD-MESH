// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.h"
#include "screens/SightlineScreen.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace {
namespace screen = ui::screens::sightlineScreen;
std::vector<lv_obj_t *> retired;
void check(bool good, const char *message) {
  if (!good)
    throw std::runtime_error(message);
}
lv_obj_t *root() { return lv_obj_get_child(lv_layer_top(), -1); }
lv_obj_t *label(lv_obj_t *object, const char *text) {
  if (lv_obj_check_type(object, &lv_label_class) && !strcmp(lv_label_get_text(object), text))
    return object;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
    if (auto *found = label(lv_obj_get_child(object, i), text))
      return found;
  return nullptr;
}
lv_obj_t *line(lv_obj_t *object) {
  if (lv_obj_check_type(object, &lv_line_class))
    return object;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
    if (auto *found = line(lv_obj_get_child(object, i)))
      return found;
  return nullptr;
}
void click(lv_obj_t *object) {
  check(object, "Sightline missing control");
  lv_event_send(object, LV_EVENT_CLICKED, nullptr);
}
int fetch(void *, const ui::SightlineJob::Request &, float *out, int &code) {
  for (int i = 0; i < ui::sightline::Samples; ++i)
    out[i] = 100;
  code = 200;
  return ui::sightline::Samples;
}
} // namespace
void runSightlineRegression() {
  const auto roots = lv_obj_get_child_cnt(lv_layer_top());
  ui::SightlineJob job;
  screen::configure({&job, [] { return true; },
                     [](lv_obj_t **object) {
                       retired.push_back(*object);
                       lv_obj_add_flag(*object, LV_OBJ_FLAG_HIDDEN);
                       *object = nullptr;
                     },
                     []() -> lv_coord_t { return 24; }, nullptr});
  screen::Selection selection{};
  selection.path = {50, 14, 50, 14.01};
  selection.frequency = 868;
  selection.networkReady = true;
  strcpy(selection.name, "First");
  strcpy(selection.server, "http://tiles");
  screen::open(selection);
  auto *first = root();
  check(job.active() && !line(first), "Sightline did not queue terrain request");
  screen::close();
  strcpy(selection.name, "Second");
  selection.path.peerLon = 14.02;
  screen::open(selection);
  auto *second = root();
  check(label(second, TR("Still analyzing the previous\npath\xe2\x80\xa6 try again in a moment.")),
        "Sightline replaced an unfinished request");
  check(job.run({nullptr, fetch, nullptr}), "Sightline queued worker missing");
  screen::poll();
  check(!line(second) && !job.active(), "Old sightline result rendered into another contact");
  click(first);
  check(screen::isOpen(), "Retired sightline backdrop closed replacement");
  screen::open(selection);
  check(job.run({nullptr, fetch, nullptr}), "Second sightline request missing");
  screen::poll();
  auto *ready = root();
  check(line(ready) && label(ready, "2m"), "Sightline result did not render");
  auto *plus = lv_obj_get_parent(label(ready, "+"));
  click(plus);
  check(label(ready, "3m"), "Sightline height change missing");
  auto *oldLine = line(ready);
  auto *oldPoints = reinterpret_cast<lv_line_t *>(oldLine)->point_array;
  const auto saved = oldPoints[12];
  screen::open(selection);
  check(job.run({nullptr, fetch, nullptr}), "Replacement sightline request missing");
  screen::poll();
  auto *replacement = root();
  click(plus);
  check(label(replacement, "3m") && !label(replacement, "4m"), "Retired height button mutated new screen");
  check(oldPoints != reinterpret_cast<lv_line_t *>(line(replacement))->point_array &&
            oldPoints[12].x == saved.x && oldPoints[12].y == saved.y,
        "Retired graph shared line-point storage with replacement");
  for (auto *object : retired)
    lv_obj_del(object);
  retired.clear();
  check(screen::isOpen(), "Deleting retired sightline cleared live root");
  lv_obj_del(replacement);
  check(!screen::isOpen(), "Sightline retained deleted root");
  screen::poll();
  screen::close();
  selection.path.selfLat = selection.path.selfLon = 0;
  screen::open(selection);
  check(!job.active() && !line(root()), "Unknown location queued a request");
  screen::close();
  for (auto *object : retired)
    lv_obj_del(object);
  retired.clear();
  screen::configure({nullptr, nullptr, nullptr, nullptr, nullptr});
  check(lv_obj_get_child_cnt(lv_layer_top()) == roots, "Sightline leaked a modal");
  std::puts("Sightline screen: stale results/buttons/backdrops, per-tree graph storage, height edits and "
            "deletion passed.");
}

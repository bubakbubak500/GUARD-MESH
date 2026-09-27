// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReleasePicker.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/Styles.h"
#include "ConfirmDialog.h"
#include <cstdio>
namespace ui {
namespace screens {
namespace releasePicker {
using namespace theme;
using namespace widgets;
static Host host{};
static lv_obj_t *root = nullptr;
static uint32_t generation = 0;
struct Request {
  int version;
  bool beta;
};
static bool channel = false;
static void closeRoot(lv_obj_t **object) {
  if (!object || !*object)
    return;
  if (host.closeRoot)
    host.closeRoot(object);
  else {
    lv_obj_del(*object);
    *object = nullptr;
  }
}
static ConfirmDialog confirmation({[]() -> lv_coord_t { return host.contentTop ? host.contentTop() : 0; },
                                   closeRoot,
                                   [](lv_obj_t *object) {
                                     if (host.focus)
                                       host.focus(object);
                                   }});
static void closePicker() {
  auto *old = root;
  root = nullptr;
  if (old)
    closeRoot(&old);
}
void close() {
  ++generation;
  auto *old = root;
  root = nullptr;
  confirmation.dismiss();
  if (old)
    closeRoot(&old);
}
void configure(const Host &value) {
  close();
  host = value;
}
bool isOpen() { return root || confirmation.isOpen(); }
static bool owns(lv_event_t *event) {
  for (auto *object = lv_event_get_target(event); root && object; object = lv_obj_get_parent(object))
    if (object == root)
      return true;
  return false;
}
static void deleted(lv_event_t *event) {
  if (lv_event_get_target(event) == root) {
    root = nullptr;
    ++generation;
  }
}
static void cancel(lv_event_t *event) {
  if (owns(event))
    close();
}
static void pick(lv_event_t *event) {
  if (!owns(event))
    return;
  const int version =
      static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_target(event))));
  if (version < 1)
    return;
  const Request request{version, channel};
  const auto current = generation;
  closePicker();
  if (current != generation || isOpen())
    return;
  char message[96];
  snprintf(message, sizeof message, TR("Install beta_%d?\nThis downgrades the firmware and reboots."),
           version);
  const auto install = host.install;
  confirmation.showCaptured(
      message, TR("Install"),
      [request, install] {
        if (install)
          install(request.version, request.beta);
      },
      false);
}
void open(int latest, bool beta) {
  close();
  if (isOpen())
    return;
  if (latest < 2) {
    if (host.notify)
      host.notify(TR("No earlier versions yet"));
    return;
  }
  channel = beta;
  root = lv_obj_create(lv_layer_top());
  lv_obj_add_event_cb(root, deleted, LV_EVENT_DELETE, nullptr);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
  lv_obj_set_pos(root, 0, 0);
  lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_50, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(root, cancel, LV_EVENT_CLICKED, nullptr);
  lv_obj_move_foreground(root);
  auto *card = lv_obj_create(root);
  lv_obj_remove_style_all(card);
  lv_obj_set_width(card, lv_disp_get_hor_res(nullptr) - SC(40));
  lv_obj_set_height(card, LV_SIZE_CONTENT);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  styleSurface(card, colors().COLOR_PANEL, 10);
  lv_obj_set_style_pad_all(card, 12, LV_PART_MAIN);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
  addCloseXBadge(card, cancel);
  auto *title = lv_label_create(card);
  lv_label_set_text(title, TR("Install an earlier version"));
  lv_obj_set_style_text_font(title, &font14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  for (int k = 1; k <= 3 && latest - k > 0; ++k) {
    auto *button = lv_btn_create(card);
    lv_obj_set_size(button, lv_pct(90), 38);
    styleButton(button);
    lv_obj_set_user_data(button, reinterpret_cast<void *>(static_cast<intptr_t>(latest - k)));
    lv_obj_add_event_cb(button, pick, LV_EVENT_CLICKED, nullptr);
    auto *label = lv_label_create(button);
    lv_label_set_text_fmt(label, "beta_%d", latest - k);
    lv_obj_set_style_text_font(label, &font14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_center(label);
  }
}
} // namespace releasePicker
} // namespace screens
} // namespace ui

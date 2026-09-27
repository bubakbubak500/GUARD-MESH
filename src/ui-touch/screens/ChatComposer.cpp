// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChatComposer.h"
#include "../device_caps.h"
#include "../emoji_data.h"
#include "../i18n.h"
#include "../models/MessageTypes.h"
#include "../platform/UiPlatform.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/FkeyShape.h"
#include "../widgets/Styles.h"
#include <cstring>
#include <memory>
namespace ui {
namespace screens {
using namespace theme;
using namespace widgets;
lv_coord_t ChatComposer::baseHeight() {
#if CAP_LARGE_SCREEN
  return 64;
#elif defined(TLORA_PAGER)
  const lv_coord_t need = lv_font_get_line_height(&font14()) + 18;
  return need > 34 ? need : 34;
#else
  return 34;
#endif
}
static lv_coord_t chipSize() {
#if CAP_LARGE_SCREEN
  return 56;
#elif defined(TLORA_PAGER)
  return ChatComposer::baseHeight() - 4;
#else
  return 30;
#endif
}
static lv_coord_t sendSize() {
#if defined(TLORA_PAGER)
  return chipSize();
#else
  return 34;
#endif
}
static bool hasSymbols() {
#if defined(HAS_M9_KEYBOARD)
  return true;
#else
  return false;
#endif
}
lv_coord_t ChatComposer::fieldWidth() const {
  const lv_coord_t width =
      _layout.width - (2 * chipSize() + 12) - sendSize() - 14 - (hasSymbols() ? chipSize() + 6 : 0);
  return width > 16 ? width : 16;
}
ChatComposer::~ChatComposer() { close(); }
void ChatComposer::detach() {
  invalidate();
  if (row())
    lv_obj_remove_event_cb_with_user_data(row(), rootDeleted, this);
  if (field())
    lv_obj_remove_event_cb_with_user_data(field(), fieldEvent, this);
  for (auto &button : _buttons) {
    if (button.get())
      lv_obj_remove_event_cb_with_user_data(button.get(), buttonEvent, this);
    button.set(nullptr);
  }
  _root.set(nullptr);
  _field.set(nullptr);
  _counter.set(nullptr);
  _messages.set(nullptr);
  _height = 0;
  // Keep _sending set during a synchronous send callback that rebuilds us.
}
void ChatComposer::close() {
  auto *old = row();
  auto closer = _host.closeRoot;
  detach();
  if (old) {
    if (closer)
      closer(&old);
    else
      lv_obj_del(old);
  }
}
void ChatComposer::rootDeleted(lv_event_t *event) {
  static_cast<ChatComposer *>(lv_event_get_user_data(event))->detach();
}
void ChatComposer::place() {
  if (!row() || !field())
    return;
  lv_obj_set_size(row(), _layout.width, height());
  lv_obj_set_pos(row(), 0, _layout.viewportHeight - _layout.keyboardHeight - height());
  lv_obj_set_size(field(), fieldWidth(), height() - 4);
  if (hasSymbols()) {
    const lv_coord_t x = chipSize() + 6 + fieldWidth() + 6;
    if (symbolButton())
      lv_obj_align(symbolButton(), LV_ALIGN_BOTTOM_LEFT, x, 0);
    if (emojiButton())
      lv_obj_align(emojiButton(), LV_ALIGN_BOTTOM_LEFT, x + chipSize() + 6, 0);
  }
  if (_messages.get()) {
    lv_obj_set_size(_messages.get(), _layout.width,
                    _layout.viewportHeight - _layout.headerHeight - _layout.keyboardHeight);
    lv_obj_set_style_pad_bottom(_messages.get(), height() + 6, LV_PART_MAIN);
  }
}
void ChatComposer::relayout(const Layout &layout) {
  _layout = layout;
  place();
  if (field())
    lv_obj_update_layout(field());
  grow();
}
void ChatComposer::grow() {
  if (!field() || !row())
    return;
  const lv_coord_t lineHeight = lv_font_get_line_height(&font14());
  if (lineHeight <= 0)
    return;
  lv_coord_t width = lv_obj_get_content_width(field()) - 4;
  if (width < 16)
    width = 16;
  const char *text = lv_textarea_get_text(field());
  lv_point_t size;
  lv_txt_get_size(&size, text && *text ? text : " ", &font14(), 0, 0, width, LV_TEXT_FLAG_NONE);
  int lines = (size.y + lineHeight - 1) / lineHeight;
  if (lines < 1)
    lines = 1;
  if (lines > 4)
    lines = 4;
  _height = baseHeight() + (lines - 1) * lineHeight;
  place();
}
void ChatComposer::count() {
  if (!field() || !counter())
    return;
  const uint32_t used = _lv_txt_get_encoded_length(lv_textarea_get_text(field()));
  const uint32_t cap = MessageTypes::MAX_MSG_TEXT;
  if (used * 4 < cap * 3) {
    lv_obj_add_flag(counter(), LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_label_set_text_fmt(counter(), "%u/%u", (unsigned)used, (unsigned)cap);
  lv_obj_set_style_text_color(
      counter(), lv_color_hex(used >= cap ? colors().COLOR_STATUS_WARN : colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_clear_flag(counter(), LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(counter(), LV_ALIGN_TOP_RIGHT, -SC(4), -SC(2));
  lv_obj_move_foreground(counter());
}
void ChatComposer::fieldEvent(lv_event_t *event) {
  auto &self = *static_cast<ChatComposer *>(lv_event_get_user_data(event));
  if (!self.field() || lv_event_get_target(event) != self.field())
    return;
  const auto code = lv_event_get_code(event);
  if (code == LV_EVENT_VALUE_CHANGED) {
    self.count();
    self.grow();
  }
  if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED || code == LV_EVENT_PRESSED ||
      code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_LONG_PRESSED) {
    if (self._host.input)
      self._host.input(self._host.context, event);
  }
}
void ChatComposer::buttonEvent(lv_event_t *event) {
  auto &self = *static_cast<ChatComposer *>(lv_event_get_user_data(event));
  if (!self.field())
    return;
  for (int i = 0; i < 4; ++i) {
    if (lv_event_get_target(event) != self._buttons[i].get())
      continue;
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
      self._pressGeneration[i] = self._generation;
      self._pressed[i] = true;
    } else if (lv_event_get_code(event) == LV_EVENT_PRESS_LOST) {
      self._pressed[i] = false;
    } else if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
      const bool stale = self._pressed[i] && self._pressGeneration[i] != self._generation;
      self._pressed[i] = false;
      if (stale)
        return;
      if (i == 3)
        self.send();
      else if (self._host.picker)
        self._host.picker(self._host.context, static_cast<Picker>(i));
    }
    return;
  }
}
bool ChatComposer::send(bool dismissKeyboard) {
  if (_sending || !field() || !_host.send)
    return false;
  const char *text = lv_textarea_get_text(field());
  if (!text || !*text)
    return false;
  const size_t bytes = strlen(text) + 1;
  std::unique_ptr<char, void (*)(void *)> snapshot(static_cast<char *>(platform::allocate(bytes, true)),
                                                   platform::release);
  if (!snapshot)
    return false;
  memcpy(snapshot.get(), text, bytes);
  ObjectRef destination;
  if (!destination.set(field()))
    return false;
  const uint32_t generation = _generation;
  _sending = true;
  const bool sent = _host.send(_host.context, snapshot.get(), dismissKeyboard);
  // A callback may switch conversations, replace the draft, or delete/rebuild the tree.
  if (sent && generation == _generation && destination.get() && destination.get() == field() &&
      strcmp(lv_textarea_get_text(field()), snapshot.get()) == 0)
    lv_textarea_set_text(field(), "");
  _sending = false;
  return sent;
}
void ChatComposer::build(lv_obj_t *parent, lv_obj_t *messages, const Layout &layout, const Host &host) {
  close();
  _host = host;
  _layout = layout;
  _height = baseHeight();
  for (auto &pressed : _pressed)
    pressed = false;
  if (!parent)
    return;
  _messages.set(messages);

  const lv_coord_t composer_h = baseHeight();
  lv_obj_t *root = lv_obj_create(parent);
  _root.set(root);

  lv_obj_set_size(root, _layout.width, composer_h);
  lv_obj_set_pos(root, 0, _layout.viewportHeight - _height);

  styleSurface(root, colors().COLOR_PANEL, 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(root, 2, LV_PART_MAIN);

  lv_obj_set_style_pad_hor(root, 4, LV_PART_MAIN);

  const lv_coord_t chip_sz = chipSize();
  const lv_coord_t chip_gap = 6;
  const bool has_symbol_chip = hasSymbols();
  const lv_coord_t send_sz = sendSize();
  const lv_coord_t comp_ta_w = fieldWidth();
  const lv_coord_t comp_ta_x = has_symbol_chip ? chip_sz + chip_gap : 2 * chip_sz + 2 * chip_gap;
  const lv_coord_t symbol_x = has_symbol_chip ? comp_ta_x + comp_ta_w + chip_gap : 0;
  const lv_coord_t emoji_x = has_symbol_chip ? symbol_x + chip_sz + chip_gap : chip_sz + chip_gap;

  lv_obj_t *qr_btn = lv_btn_create(root);
  _buttons[0].set(qr_btn);
  lv_obj_set_size(qr_btn, chip_sz, chip_sz);
  lv_obj_align(qr_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  styleButton(qr_btn);
  lv_obj_set_style_radius(qr_btn, chip_sz / 2, LV_PART_MAIN);

  lv_obj_set_style_bg_color(qr_btn, lv_color_hex(themeRole(0x000000, colors().COLOR_PANEL)), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(qr_btn, LV_OPA_50, LV_PART_MAIN);

  lv_obj_t *ql = lv_label_create(qr_btn);
  lv_label_set_text(ql, LV_SYMBOL_LIST);
  lv_obj_set_style_text_font(ql, &font14(), LV_PART_MAIN);
  lv_obj_center(ql);
#if defined(HAS_TANMATSU)
  styleChipAsFkey(qr_btn, ql, 0, 0xF5A623, chip_sz, true); // orange △ — quick replies (F2)

  lv_obj_set_style_bg_color(qr_btn, lv_color_hex(themeRole(0x000000, colors().COLOR_PANEL)), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(qr_btn, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_radius(qr_btn, chip_sz / 2, LV_PART_MAIN);

  lv_obj_set_style_text_font(ql, &lv_font_montserrat_14, LV_PART_MAIN);
#endif

  lv_obj_t *emoji = lv_btn_create(root);
  _buttons[1].set(emoji);
  lv_obj_set_size(emoji, chip_sz, chip_sz);
  lv_obj_align(emoji, LV_ALIGN_BOTTOM_LEFT, emoji_x, 0);
  styleButton(emoji);
  lv_obj_set_style_radius(emoji, chip_sz / 2, LV_PART_MAIN);

  lv_obj_set_style_bg_color(emoji, lv_color_hex(themeRole(0x000000, colors().COLOR_PANEL)), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(emoji, LV_OPA_50, LV_PART_MAIN);

  lv_obj_t *el = lv_label_create(emoji);
  lv_label_set_text(el, TR("\xF0\x9F\x98\x8A")); // 😊
  lv_obj_set_style_text_font(el, &font16(), LV_PART_MAIN);
  lv_obj_center(el);
#if defined(HAS_TANMATSU)
  styleChipAsFkey(emoji, el, 1, 0xFFD400, chip_sz,
                  false); // yellow □ — emoji picker (F3); keep the colour glyph

  lv_obj_set_style_bg_color(emoji, lv_color_hex(themeRole(0x000000, colors().COLOR_PANEL)), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(emoji, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_radius(emoji, chip_sz / 2, LV_PART_MAIN);
#if LV_USE_IMGFONT

  if (const lv_img_dsc_t *sm = emojiGlyphLookup(0x1F60A)) { // 😊
    lv_obj_add_flag(el, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *eimg = lv_img_create(emoji);
    lv_img_set_src(eimg, sm);
    lv_img_set_antialias(eimg, true);
    lv_img_set_zoom(eimg, 512); // 256 = 1x -> 512 = 2x
    lv_obj_center(eimg);
    lv_obj_move_foreground(eimg);
  }
#endif
#endif

#if defined(HAS_M9_KEYBOARD)
  if (has_symbol_chip) {
    lv_obj_t *symbol = lv_btn_create(root);
    _buttons[2].set(symbol);
    lv_obj_set_size(symbol, chip_sz, chip_sz);
    lv_obj_align(symbol, LV_ALIGN_BOTTOM_LEFT, symbol_x, 0);
    styleButton(symbol);
    lv_obj_set_style_radius(symbol, chip_sz / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(symbol, lv_color_hex(themeRole(0x000000, colors().COLOR_PANEL)), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(symbol, LV_OPA_50, LV_PART_MAIN);

    lv_obj_t *symbol_label = lv_label_create(symbol);
    lv_label_set_text(symbol_label, "#");
    lv_obj_set_style_text_font(symbol_label, &font14(), LV_PART_MAIN);
    lv_obj_center(symbol_label);
  }
#endif

  lv_obj_t *field = lv_textarea_create(root);
  _field.set(field);

  lv_obj_set_size(field, comp_ta_w, composer_h - 4);

  lv_obj_align(field, LV_ALIGN_BOTTOM_LEFT, comp_ta_x, 0);
  styleCard(field);
  lv_obj_set_style_radius(field, 15, LV_PART_MAIN); // pill shape

  lv_obj_set_style_pad_ver(field, 3, LV_PART_MAIN);

  lv_textarea_set_one_line(field, false);

  lv_textarea_set_max_length(field, MessageTypes::MAX_MSG_TEXT);
  lv_obj_set_scrollbar_mode(field, LV_SCROLLBAR_MODE_OFF);
  taSetPlaceholder(field, TR("Type a message..."));
  lv_obj_set_style_text_color(field, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(field, &font14(), LV_PART_MAIN);

  if (lv_obj_t *comp_lbl = lv_textarea_get_label(field)) {
    lv_obj_set_style_bg_color(comp_lbl, lv_color_hex(colors().COLOR_TEXT), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(comp_lbl, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_color(comp_lbl, lv_color_hex(colors().COLOR_PANEL), LV_PART_SELECTED);

    lv_obj_set_style_pad_top(comp_lbl, 4, LV_PART_MAIN);
  }

  lv_obj_t *counter = lv_label_create(root);
  _counter.set(counter);
  lv_label_set_text(counter, "");
  lv_obj_set_style_text_font(counter, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(counter, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_add_flag(counter, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_clear_flag(counter, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(counter, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(counter, LV_ALIGN_TOP_RIGHT, -SC(4), -SC(2));
  lv_obj_move_foreground(counter);

  lv_obj_t *send = lv_btn_create(root);
  _buttons[3].set(send);
  lv_obj_set_size(send, send_sz,
#if defined(TLORA_PAGER)
                  send_sz
#else
                  30
#endif
  );
  lv_obj_align(send, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  styleButton(send);
  lv_obj_set_style_radius(send, 15, LV_PART_MAIN);

  lv_obj_t *sl = lv_label_create(send);
  lv_label_set_text(sl, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_font(sl, &font16(), LV_PART_MAIN);
  lv_obj_center(sl);

  lv_obj_add_event_cb(root, rootDeleted, LV_EVENT_DELETE, this);
  lv_obj_add_event_cb(field, fieldEvent, LV_EVENT_ALL, this);
  for (auto &button : _buttons)
    if (button.get())
      lv_obj_add_event_cb(button.get(), buttonEvent, LV_EVENT_ALL, this);
  relayout(layout);
}
} // namespace screens
} // namespace ui

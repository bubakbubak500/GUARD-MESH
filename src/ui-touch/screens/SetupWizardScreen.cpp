// SPDX-License-Identifier: GPL-3.0-or-later
#include "SetupWizardScreen.h"

#include "i18n.h"
#include "theme/Fonts.h"
#include "theme/Theme.h"
#include "widgets/Styles.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace ui { namespace screens {
using namespace theme;
using namespace widgets;

namespace {
constexpr lv_coord_t ButtonHeight = 40;
void copyUtf8(char *out, size_t capacity, const char *text) {
  if (!capacity) return;
  out[0] = 0;
  if (!text) return;
  size_t input = 0, output = 0;
  while (text[input]) {
    const unsigned char lead = static_cast<unsigned char>(text[input]);
    const size_t width = lead >= 0xF0 && lead <= 0xF4 ? 4 :
                         lead >= 0xE0 && lead <= 0xEF ? 3 :
                         lead >= 0xC2 && lead <= 0xDF ? 2 : 1;
    if (output + width >= capacity) break;
    bool complete = true;
    for (size_t i = 1; i < width; ++i) {
      const unsigned char next = static_cast<unsigned char>(text[input + i]);
      if (!next || (next & 0xC0) != 0x80) { complete = false; break; }
    }
    if (!complete) break;
    std::memcpy(out + output, text + input, width);
    input += width; output += width;
  }
  out[output] = 0;
}
} // namespace

SetupWizardScreen::SetupWizardScreen(Host host) : _host(host) {}
SetupWizardScreen::~SetupWizardScreen() {
  _destroying = true;
  hide();
}
bool SetupWizardScreen::still(uint32_t generation, lv_obj_t *root) const {
  return !_destroying && _generation == generation && _root.get() == root;
}
bool SetupWizardScreen::readCommitted(Committed &out) {
  out = Committed{};
  if (!_host.readCommitted || !_host.readCommitted(_host.context, out)) return false;
  out.name[CommittedNameCapacity - 1] = 0;
  return true;
}
void SetupWizardScreen::alert(const char *text, unsigned duration) {
  if (_host.showAlert) _host.showAlert(_host.context, text, duration);
}

void SetupWizardScreen::detachOwned(lv_obj_t *object, bool rootDeleting) {
  if (!object) return;
  const uint32_t count = lv_obj_get_child_cnt(object);
  for (uint32_t i = 0; i < count; ++i)
    detachOwned(lv_obj_get_child(object, i));
  if (rootDeleting) return; // Preserve the executing DELETE descriptor and later observers.
  for (auto callback : {rootDeleted, skipClicked, startedClicked, backClicked,
                        nameNextClicked, regionNextClicked, finishClicked, regionClicked})
    while (lv_obj_remove_event_cb_with_user_data(object, callback, this)) {}
}

void SetupWizardScreen::clear(lv_obj_t *retiring, bool externalDelete) {
  const uint32_t retiredGeneration = ++_generation;
  if (retiring) detachOwned(retiring, externalDelete);
  // A Host callback may delete the old tree, then open a new wizard. Track the
  // retired root independently of the owner's now-empty live reference.
  widgets::ObjectRef retired;
  const bool watching = !externalDelete && retiring && retired.set(retiring);
  _root.set(nullptr);
  _nameField.set(nullptr); _regionList.set(nullptr); _regionNext.set(nullptr);
  _step = Step::Welcome;
  _selectedRegion = -1;
  _draftName[0] = 0;
  // If LVGL cannot register the temporary watcher, consume the old root
  // before any Host callback can synchronously delete it.
  if (!externalDelete && retiring && !watching) {
    if (_host.closeRoot) _host.closeRoot(_host.context, &retiring);
    else lv_obj_del(retiring);
  }
  if (_generation != retiredGeneration) return;
  if (_host.resetNav) _host.resetNav(_host.context);
  if (_generation == retiredGeneration && _host.hideKeyboard)
    _host.hideKeyboard(_host.context);
  if (_generation == retiredGeneration && _host.navDetachBeforeTreeMutation)
    _host.navDetachBeforeTreeMutation(_host.context);
  if (!externalDelete && watching && retired.get()) {
    lv_obj_t *old = retired.get();
    if (_host.closeRoot) _host.closeRoot(_host.context, &old);
    else lv_obj_del(old);
  }
  if (_generation == retiredGeneration && _host.statusBarHidden)
    _host.statusBarHidden(_host.context, false);
}

void SetupWizardScreen::hide() {
  if (!_root.get()) return;
  clear(_root.get(), false);
}
void SetupWizardScreen::rootDeleted(lv_event_t *event) {
  auto *self = static_cast<SetupWizardScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying)
    self->clear(lv_event_get_current_target(event), true);
}

void SetupWizardScreen::show() {
  if (_destroying) return;
  if (auto *existing = _root.get()) {
    lv_obj_move_foreground(existing);
    return;
  }
  const uint32_t generation = ++_generation;
  if (_host.resetNav) _host.resetNav(_host.context);
  if (_generation != generation) return;
  lv_obj_t *root = lv_obj_create(lv_layer_top());
  if (!root) return;
  if (!_root.set(root)) { lv_obj_del(root); return; }
  lv_obj_add_event_cb(root, rootDeleted, LV_EVENT_DELETE, this);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
  lv_obj_set_pos(root, 0, 0);
  styleSurface(root, colors().COLOR_BG, 0);
  lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  if (_host.statusBarHidden) _host.statusBarHidden(_host.context, true);
  if (!still(generation, root)) return;
  Committed saved;
  if (readCommitted(saved) && still(generation, root))
    copyUtf8(_draftName, sizeof _draftName, saved.name);
  if (!still(generation, root)) return;
  _selectedRegion = -1;
  showStep(Step::Welcome);
  if (_root.get() == root) lv_obj_move_foreground(root);
}

lv_obj_t *SetupWizardScreen::button(const char *caption, lv_event_cb_t callback,
                                     bool primary, lv_coord_t x, lv_coord_t y, lv_coord_t width) {
  auto *root = _root.get();
  if (!root) return nullptr;
  auto *control = lv_btn_create(root);
  lv_obj_set_size(control, width, ButtonHeight);
  lv_obj_set_pos(control, x, y);
  styleButton(control);
  if (primary) {
    lv_obj_set_style_bg_color(control, lv_color_hex(colors().COLOR_STATUS_OK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(control, lv_color_hex(colors().COLOR_STATUS_OK_PRESSED),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(control, lv_color_hex(colors().COLOR_ON_STATUS_OK), LV_PART_MAIN);
  }
  lv_obj_add_event_cb(control, callback, LV_EVENT_CLICKED, this);
  auto *label = lv_label_create(control);
  lv_label_set_text(label, TR(caption));
  lv_obj_set_style_text_font(label, &font14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(label,
      lv_color_hex(primary ? colors().COLOR_ON_STATUS_OK : colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_center(label);
  return control;
}

int SetupWizardScreen::header(const char *title, const char *blurb, const char *stepTag) {
  auto *root = _root.get();
  const lv_coord_t width = lv_disp_get_hor_res(nullptr);
  auto *heading = lv_label_create(root);
  lv_label_set_text(heading, TR(title));
  lv_label_set_long_mode(heading, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(heading, width - 84);
  lv_obj_set_style_text_font(heading, &font16(), LV_PART_MAIN);
  lv_obj_set_style_text_color(heading, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_pos(heading, 12, 12);
  lv_obj_update_layout(heading);
  int y = 12 + LV_MAX(24, lv_obj_get_height(heading) + 2);
  if (stepTag && stepTag[0]) {
    auto *tag = lv_label_create(root);
    lv_label_set_text(tag, stepTag);
    lv_obj_set_style_text_font(tag, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(tag, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_align(tag, LV_ALIGN_TOP_RIGHT, -12, 16);
  }
  if (blurb && blurb[0]) {
    auto *body = lv_label_create(root);
    lv_label_set_text(body, TR(blurb));
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, width - 24);
    lv_obj_set_style_text_font(body, &font14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(body, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_pos(body, 12, y + 2);
    lv_obj_update_layout(body);
    y += 2 + lv_obj_get_height(body) + 4;
  }
  return y;
}

void SetupWizardScreen::fillRegions() {
  auto *list = _regionList.get();
  if (!list) return;
  const unsigned count = _host.presetCount ? _host.presetCount(_host.context) : 0;
  const unsigned bounded = count < PresetCapacity ? count : PresetCapacity;
  const lv_coord_t rowWidth = lv_disp_get_hor_res(nullptr) - 24 - 12;
  for (unsigned i = 0; i < bounded; ++i) {
    auto *row = lv_btn_create(list);
    lv_obj_set_size(row, rowWidth, 34);
    styleButton(row);
    lv_obj_add_event_cb(row, regionClicked, LV_EVENT_CLICKED, this);
    auto *label = lv_label_create(row);
    const char *name = _host.presetLabel ? _host.presetLabel(_host.context, i) : "";
    lv_label_set_text(label, name ? name : "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, rowWidth - 16);
    lv_obj_set_style_text_font(label, &font14(), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, 0);
  }
  recolorRegions();
}
void SetupWizardScreen::recolorRegions() {
  auto *list = _regionList.get();
  if (!list) return;
  const uint32_t count = lv_obj_get_child_cnt(list);
  for (uint32_t i = 0; i < count; ++i) {
    auto *row = lv_obj_get_child(list, i);
    const bool selected = static_cast<int>(i) == _selectedRegion;
    lv_obj_set_style_bg_color(row,
        lv_color_hex(selected ? colors().COLOR_STATUS_OK : colors().COLOR_CONTROL), LV_PART_MAIN);
    lv_obj_set_style_text_color(row,
        lv_color_hex(selected ? colors().COLOR_ON_STATUS_OK : colors().COLOR_TEXT), LV_PART_MAIN);
  }
}

void SetupWizardScreen::showStep(Step step) {
  auto *root = _root.get();
  if (!root) return;
  // A Host call can navigate to another step without replacing the root.
  // Every step transition therefore retires callbacks from the prior step.
  const uint32_t generation = ++_generation;
  if (_host.navDirty) _host.navDirty(_host.context);
  if (!still(generation, root)) return;
  if (_host.hideKeyboard) _host.hideKeyboard(_host.context);
  if (!still(generation, root)) return;
  if (_host.navDetachBeforeTreeMutation)
    _host.navDetachBeforeTreeMutation(_host.context);
  if (!still(generation, root)) return;
  lv_obj_clean(root);
  if (!still(generation, root)) return;
  _nameField.set(nullptr); _regionList.set(nullptr); _regionNext.set(nullptr);
  _step = step;
  const lv_coord_t width = lv_disp_get_hor_res(nullptr);
  const lv_coord_t height = lv_disp_get_ver_res(nullptr);
  const lv_coord_t buttonY = height - ButtonHeight - 10;

  switch (step) {
    case Step::Welcome: {
      header(TR("Welcome to WADAMESH"), nullptr, nullptr);
      auto *message = lv_label_create(root);
      lv_label_set_text(message, TR("Let's set up your device.\n\n"
                                 "You'll pick a name and choose your LoRa region. Takes about a minute."));
      lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(message, width - 24);
      lv_obj_set_style_text_font(message, &font14(), LV_PART_MAIN);
      lv_obj_set_style_text_color(message, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
      lv_obj_set_pos(message, 12, 46);
      button(TR("Skip"), skipClicked, false, 12, buttonY, 96);
      button(TR("Get Started"), startedClicked, true, 116, buttonY, width - 128);
      break;
    }
    case Step::Name: {
      const int y = header(TR("Choose your name"),
          TR("How you'll appear to other nodes. You can change this later in Settings."),
          TR("Step 1 of 3"));
      Committed saved;
      const bool haveSaved = readCommitted(saved);
      if (!still(generation, root)) return;
      if (haveSaved) copyUtf8(_draftName, sizeof _draftName, saved.name);
      auto *field = lv_textarea_create(root);
      _nameField.set(field);
      lv_obj_set_size(field, width - 24, 36);
      lv_obj_set_pos(field, 12, y + 6);
      lv_textarea_set_one_line(field, true);
      taSetPlaceholder(field, TR("Your name"));
      lv_textarea_set_max_length(field, 30);
      if (_draftName[0]) lv_textarea_set_text(field, _draftName);
      if (_host.attachTextArea) _host.attachTextArea(_host.context, field);
      if (!still(generation, root) || _nameField.get() != field) return;
      button(TR("Back"), backClicked, false, 12, buttonY, 72);
      button(TR("Next"), nameNextClicked, true, width - 132, buttonY, 120);
      break;
    }
    case Step::Region: {
      const int y = header(TR("Choose your region"),
          TR("Every node you talk to must match. You can change this later in Settings."),
          TR("Step 2 of 3"));
      auto *list = lv_obj_create(root);
      _regionList.set(list);
      lv_obj_remove_style_all(list);
      lv_obj_set_size(list, width - 24, buttonY - (y + 6) - 8);
      lv_obj_set_pos(list, 12, y + 6);
      lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_style_pad_row(list, 4, LV_PART_MAIN);
      lv_obj_set_scroll_dir(list, LV_DIR_VER);
      lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
      if (_selectedRegion < 0) {
        Committed saved;
        const bool haveSaved = readCommitted(saved);
        if (!still(generation, root)) return;
        if (haveSaved) _selectedRegion = saved.regionIndex;
      }
      if (!still(generation, root) || _regionList.get() != list) return;
      fillRegions();
      button(TR("Back"), backClicked, false, 12, buttonY, 72);
      _regionNext.set(button(TR("Next"), regionNextClicked, true,
                             width - 132, buttonY, 120));
      break;
    }
    case Step::Connectivity: {
      const int y = header(TR("Wi-Fi & Bluetooth"), nullptr, TR("Step 3 of 3"));
      auto *message = lv_label_create(root);
      lv_label_set_text(message,
          TR("This device can run Wi-Fi and Bluetooth at once, as a standalone radio and a "
             "phone companion (MeshCore app) together.\n\n"
             "Running both at the same time uses more RAM, so turn on only what you need.\n\n"
             "Set them up anytime in Settings."));
      lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(message, width - 24);
      lv_obj_set_style_text_font(message, &font12(), LV_PART_MAIN);
      lv_obj_set_style_text_color(message, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
      lv_obj_set_pos(message, 12, y + 6);
      button(TR("Back"), backClicked, false, 12, buttonY, 72);
      button(TR("Finish"), finishClicked, true, width - 132, buttonY, 120);
      break;
    }
  }
}

void SetupWizardScreen::back() {
  if (!_root.get() || _step == Step::Welcome) return;
  showStep(static_cast<Step>(static_cast<unsigned>(_step) - 1));
}
void SetupWizardScreen::nameNext() {
  auto *root = _root.get();
  auto *field = _nameField.get();
  if (!root || !field || _step != Step::Name) return;
  const uint32_t generation = _generation;
  if (_host.syncKeyboard) _host.syncKeyboard(_host.context);
  if (!still(generation, root) || _nameField.get() != field) return;
  copyUtf8(_draftName, sizeof _draftName, lv_textarea_get_text(field));
  if (!_draftName[0]) { alert(TR("Enter a name"), 1200); return; }
  if (!_host.applyName || !_host.applyName(_host.context, _draftName)) {
    if (still(generation, root)) alert(TR("Save failed"), 1400);
    return;
  }
  if (still(generation, root)) showStep(Step::Region);
}
void SetupWizardScreen::regionNext() {
  auto *root = _root.get();
  if (!root || _step != Step::Region) return;
  const uint32_t generation = _generation;
  const unsigned count = _host.presetCount ? _host.presetCount(_host.context) : 0;
  const unsigned bounded = count < PresetCapacity ? count : PresetCapacity;
  if (_selectedRegion >= 0 && static_cast<unsigned>(_selectedRegion) < bounded) {
    if (!_host.applyRegion || !_host.applyRegion(_host.context,
                                                  static_cast<unsigned>(_selectedRegion))) {
      if (still(generation, root)) alert(TR("Save failed"), 1400);
      return;
    }
  }
  if (still(generation, root)) showStep(Step::Connectivity);
}
void SetupWizardScreen::complete(bool skipped) {
  auto *root = _root.get();
  if (!root) return;
  const uint32_t generation = _generation;
  if (_host.syncKeyboard) _host.syncKeyboard(_host.context);
  if (!still(generation, root)) return;
  if (!_host.markDone || !_host.markDone(_host.context, skipped)) {
    if (still(generation, root)) alert(TR("Save failed"), 1400);
    return;
  }
  if (!still(generation, root)) return;
  hide();
  if (visible()) return; // a Host callback opened a replacement wizard
  alert(skipped ? TR("You can run setup later in Settings \xE2\x86\x92 Device")
                : TR("Setup complete"), skipped ? 2200 : 1400);
}

void SetupWizardScreen::skipClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *self = static_cast<SetupWizardScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying && self->_step == Step::Welcome) self->complete(true);
}
void SetupWizardScreen::startedClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *self = static_cast<SetupWizardScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying && self->_step == Step::Welcome)
    self->showStep(Step::Name);
}
void SetupWizardScreen::backClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *self = static_cast<SetupWizardScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying) self->back();
}
void SetupWizardScreen::nameNextClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *self = static_cast<SetupWizardScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying) self->nameNext();
}
void SetupWizardScreen::regionNextClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *self = static_cast<SetupWizardScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying) self->regionNext();
}
void SetupWizardScreen::finishClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *self = static_cast<SetupWizardScreen *>(lv_event_get_user_data(event));
  if (self && !self->_destroying && self->_step == Step::Connectivity)
    self->complete(false);
}
void SetupWizardScreen::regionClicked(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *self = static_cast<SetupWizardScreen *>(lv_event_get_user_data(event));
  if (!self || self->_destroying || self->_step != Step::Region) return;
  auto *list = self->_regionList.get();
  auto *row = lv_event_get_current_target(event);
  if (!list || lv_obj_get_parent(row) != list) return;
  const uint32_t count = lv_obj_get_child_cnt(list);
  for (uint32_t i = 0; i < count; ++i) {
    if (lv_obj_get_child(list, i) != row) continue;
    const bool confirmed = static_cast<int>(i) == self->_selectedRegion;
    self->_selectedRegion = static_cast<int>(i);
    self->recolorRegions();
    if (confirmed && self->_regionNext.get() && self->_host.focusNext)
      self->_host.focusNext(self->_host.context, self->_regionNext.get());
    return;
  }
}

} } // namespace ui::screens

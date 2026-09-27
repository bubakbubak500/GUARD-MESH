// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <lvgl.h>
namespace ui { namespace screens {
// Owns the contents of a borrowed body and its separate command popup.
// Close detaches callbacks before deleting contents; external DELETE invalidates
// the borrowed body and keyboard/sink before children are freed.
class TerminalScreen {
public:
  struct Host {
    bool (*isEink)();
    int (*statusHeight)();
    void (*closePopup)(lv_obj_t**);
    void (*bindKeyboard)(lv_obj_t*);
    void (*unbindKeyboard)();
    void (*syncKeyboard)();
    void (*attachField)(lv_obj_t*);
    void (*setSink)(bool);
    void (*execute)(const char*);
    void (*log)(uint32_t, const char*, const char*);
  };
  explicit TerminalScreen(Host host) : _host(host) {}
  ~TerminalScreen();
  TerminalScreen(const TerminalScreen&) = delete;
  TerminalScreen& operator=(const TerminalScreen&) = delete;
  void buildTerminal(lv_obj_t* body);
  void close();
  void openTermCmdPicker();
  void closeTermCmdPicker();
  void terminalSubmit();
  void append(uint32_t color, const char* prefix, const char* text);
  bool active() const { return _body != nullptr; }
  bool pickerOpen() const { return s_term_picker_root != nullptr; }
  lv_obj_t* input() const { return s_term_input_ta; }
private:
  Host _host;
  lv_obj_t* _body = nullptr;
  lv_obj_t* s_term_log_box = nullptr;
  lv_obj_t* s_term_input_ta = nullptr;
  lv_obj_t* s_term_picker_root = nullptr;
  static constexpr uint32_t TERM_MAX_LINES = 150;
  bool accepts(lv_event_t*) const;
  void detach(lv_obj_t*);
  static void deleted(lv_event_t*);
};
} }

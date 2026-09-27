// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/application/BackNavigation.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>
#include <vector>

using ui::back::BackNavigation;
using ui::back::Host;
using ui::back::Policy;
using ui::back::Result;

struct State {
  bool wizard = false, pan = false, map = true, panChangesPage = false;
  bool dropdown = false, power = false, control = false, confirm = false;
  bool page = false, popup = false, popupAbove = false, selection = false;
  bool chat = false, history = false, home = true, editing = false;
  bool settingsSheet = false, settingsFromControl = false, popupDisappears = false;
  bool popupBlocked = false, reenter = false;
  void *chatPointer = reinterpret_cast<void *>(0x1234);
  std::vector<std::string> actions;
  BackNavigation router;
  Result nested = Result::Root;
};
static State *pageOwner = nullptr;
static State &s(void *ctx) { return *static_cast<State *>(ctx); }
static void pageClose() { pageOwner->actions.push_back("page"); pageOwner->page = false; }
static Host host(State &state) {
  pageOwner = &state;
  Host h{};
  h.context = &state;
  h.wizardOpen = [](void *c) { return s(c).wizard; };
  h.wizardBack = [](void *c) {
    s(c).actions.push_back("wizard");
    if (s(c).reenter) s(c).nested = s(c).router.route(Policy::M9, host(s(c)));
  };
  h.panMode = [](void *c) { return s(c).pan; };
  h.mapActive = [](void *c) { return s(c).map; };
  h.clearPan = [](void *c, bool announce) {
    s(c).pan = false;
    s(c).actions.push_back(announce ? "pan-notify" : "pan-stale");
    if (s(c).panChangesPage) { s(c).page = true; return false; }
    return true;
  };
  h.dropdownOpen = [](void *c) { return s(c).dropdown; };
  h.dismissDropdown = [](void *c) { s(c).dropdown = false; s(c).actions.push_back("dropdown"); };
  h.powerMenuOpen = [](void *c) { return s(c).power; };
  h.closePowerMenu = [](void *c) { s(c).power = false; s(c).actions.push_back("power"); };
  h.controlCenterOpen = [](void *c) { return s(c).control; };
  h.closeControlCenter = [](void *c) { s(c).control = false; s(c).actions.push_back("control"); };
  h.confirmOpen = [](void *c) { return s(c).confirm; };
  h.pageCloser = [](void *c) -> ui::UiApplication::Close { return s(c).page ? pageClose : nullptr; };
  h.popupOpen = [](void *c) {
    if (s(c).popupDisappears) { s(c).popup = false; return true; }
    return s(c).popup;
  };
  h.popupAboveBase = [](void *c) { return s(c).popupAbove; };
  h.dismissPopup = [](void *c) {
    s(c).actions.push_back("popup");
    if (s(c).popupBlocked) return ui::UiApplication::Dismiss::Blocked;
    if (!s(c).popup && !s(c).selection && !s(c).popupAbove)
      return ui::UiApplication::Dismiss::None;
    s(c).popup = s(c).popupAbove = s(c).selection = false;
    s(c).settingsSheet = false;
    return ui::UiApplication::Dismiss::Closed;
  };
  h.selectionMode = [](void *c) { return s(c).selection; };
  h.chatOwner = [](void *c) -> void * { return s(c).chat ? s(c).chatPointer : nullptr; };
  h.closeChat = [](void *c, void *owner) {
    assert(owner == s(c).chatPointer);
    s(c).chat = false; s(c).actions.push_back("chat");
  };
  h.historyBack = [](void *c) {
    if (!s(c).history) return false;
    s(c).history = false; s(c).actions.push_back("history"); return true;
  };
  h.onHome = [](void *c) { return s(c).home; };
  h.goHome = [](void *c) { s(c).home = true; s(c).actions.push_back("home"); };
  h.editing = [](void *c) { return s(c).editing; };
  h.stopEditing = [](void *c) { s(c).editing = false; s(c).actions.push_back("stop-edit"); };
  h.escape = [](void *c, bool down) { s(c).actions.push_back(down ? "esc-down" : "esc-up"); };
  h.settingsFromControlCenter = [](void *c) { return s(c).settingsFromControl; };
  h.settingsSheetOpen = [](void *c) { return s(c).settingsSheet; };
  h.openControlCenter = [](void *c) { s(c).actions.push_back("reopen-control"); };
  return h;
}
static void expect(State &state, Result result, const char *actions, Policy policy,
                   bool pressed = true) {
  assert(state.router.route(policy, host(state), pressed) == result);
  std::string got;
  for (const auto &item : state.actions) {
    if (!got.empty()) got += ",";
    got += item;
  }
  assert(got == actions);
}

int main() {
  { State x; x.popup = x.page = x.chat = true; x.home = false;
    expect(x, Result::Handled, "popup", Policy::Pager);
    x.actions.clear(); expect(x, Result::Handled, "page", Policy::Pager);
    x.actions.clear(); expect(x, Result::Handled, "chat", Policy::Pager);
    x.actions.clear(); expect(x, Result::Handled, "home", Policy::Pager);
    x.actions.clear(); expect(x, Result::Escape, "esc-down", Policy::Pager); }
  { State x; x.popup = x.page = true; x.popupBlocked = true; x.home = false;
    expect(x, Result::Blocked, "popup", Policy::Pager); assert(x.page && !x.home); }
  { State x; x.page = true; x.popupDisappears = true;
    expect(x, Result::Handled, "popup", Policy::Pager); assert(x.page); }
  { State x; x.wizard = x.dropdown = x.power = x.control = x.page = x.popup = x.chat = x.history = true;
    x.home = false;
    expect(x, Result::Handled, "wizard", Policy::M9);
    x.wizard = false; x.actions.clear(); expect(x, Result::Handled, "dropdown", Policy::M9);
    x.actions.clear(); expect(x, Result::Handled, "power", Policy::M9);
    x.actions.clear(); expect(x, Result::Handled, "control", Policy::M9);
    x.confirm = true; x.actions.clear(); expect(x, Result::Handled, "popup", Policy::M9);
    x.confirm = false; x.actions.clear(); expect(x, Result::Handled, "page", Policy::M9);
    x.actions.clear(); expect(x, Result::Handled, "chat", Policy::M9);
    x.actions.clear(); expect(x, Result::Handled, "history", Policy::M9);
    x.actions.clear(); expect(x, Result::Handled, "home", Policy::M9);
    x.actions.clear(); expect(x, Result::Escape, "esc-down", Policy::M9); }
  { State x; x.pan = x.dropdown = true; expect(x, Result::Handled, "pan-notify", Policy::M9);
    assert(x.dropdown); }
  { State x; x.pan = x.dropdown = true; x.map = false;
    expect(x, Result::Handled, "pan-stale,dropdown", Policy::M9); }
  { State x; x.pan = x.dropdown = true; x.map = false; x.panChangesPage = true;
    expect(x, Result::Handled, "pan-stale", Policy::M9); assert(x.dropdown && x.page); }
  { State x; x.pan = x.dropdown = true; x.map = false;
    Host h = host(x); h.clearPan = nullptr;
    assert(x.router.route(Policy::M9, h) == Result::Handled);
    assert(x.pan && x.dropdown && x.actions.empty()); }
  { State x; x.selection = x.chat = true;
    expect(x, Result::Handled, "popup", Policy::M9); assert(x.chat); }
  { State x; x.editing = x.popup = x.chat = true;
    expect(x, Result::Handled, "stop-edit", Policy::TanmatsuRed);
    x.actions.clear(); expect(x, Result::Handled, "popup", Policy::TanmatsuRed);
    x.actions.clear(); expect(x, Result::Handled, "chat", Policy::TanmatsuRed);
    x.actions.clear(); expect(x, Result::Escape, "esc-down", Policy::TanmatsuRed); }
  { State x; x.popup = true;
    expect(x, Result::Handled, "popup", Policy::TanmatsuNav);
    x.actions.clear(); expect(x, Result::Escape, "esc-up", Policy::TanmatsuNav, false); }
  { State x; x.popup = true;
    expect(x, Result::Handled, "", Policy::TanmatsuNav, false); assert(x.popup); }
  { State x; x.chat = true;
    expect(x, Result::Escape, "esc-down", Policy::TanmatsuText); assert(x.chat); }
  { State x; x.dropdown = x.power = x.control = x.page = x.popupAbove = x.chat = true;
    x.settingsSheet = x.settingsFromControl = true; x.home = false;
    expect(x, Result::Handled, "dropdown", Policy::Ble);
    x.actions.clear(); expect(x, Result::Handled, "power", Policy::Ble);
    x.actions.clear(); expect(x, Result::Handled, "control", Policy::Ble);
    x.confirm = true; x.actions.clear(); expect(x, Result::Handled, "popup,reopen-control", Policy::Ble);
    x.confirm = false; x.actions.clear(); expect(x, Result::Handled, "page", Policy::Ble);
    x.actions.clear(); expect(x, Result::Handled, "chat", Policy::Ble);
    x.actions.clear(); expect(x, Result::Handled, "home", Policy::Ble);
    x.actions.clear(); expect(x, Result::Root, "", Policy::Ble); }
  { State x; x.popupAbove = x.settingsSheet = x.settingsFromControl = true; x.popupBlocked = true;
    expect(x, Result::Blocked, "popup", Policy::Ble); assert(x.settingsSheet); }
  { State x; x.wizard = x.reenter = true;
    expect(x, Result::Handled, "wizard", Policy::M9); assert(x.nested == Result::Reentered); }
}

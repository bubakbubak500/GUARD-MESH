// SPDX-License-Identifier: GPL-3.0-or-later
#include "BackNavigation.h"

namespace ui {
namespace back {
namespace {

bool check(bool (*fn)(void *), void *context) { return fn && fn(context); }

Result dismiss(const Host &host, bool reopenSettings) {
  const bool fromControlCenter = reopenSettings &&
      check(host.settingsFromControlCenter, host.context) &&
      check(host.settingsSheetOpen, host.context);
  const auto outcome = host.dismissPopup ? host.dismissPopup(host.context)
                                         : UiApplication::Dismiss::None;
  if (outcome == UiApplication::Dismiss::Blocked) return Result::Blocked;
  if (outcome == UiApplication::Dismiss::Closed && fromControlCenter &&
      !check(host.settingsSheetOpen, host.context) && host.openControlCenter)
    host.openControlCenter(host.context);
  // None after observing a popup means that it disappeared while routing.
  return Result::Handled;
}

Result escape(const Host &host, bool pressed) {
  if (host.escape) host.escape(host.context, pressed);
  return Result::Escape;
}

Result chat(const Host &host, void *owner) {
  if (host.closeChat) host.closeChat(host.context, owner);
  return Result::Handled;
}

} // namespace

Result BackNavigation::route(Policy policy, const Host &host, bool pressed) {
  if (_routing) return Result::Reentered;
  _routing = true;
  struct Reset { bool &flag; ~Reset() { flag = false; } } reset{_routing};
  void *const context = host.context;

  // Tanmatsu's navigation Esc has paired LVGL edges. Release never closes a
  // second layer; it only releases Esc if the current UI has no popup.
  if (policy == Policy::TanmatsuNav && !pressed)
    return check(host.popupOpen, context) ? Result::Handled : escape(host, false);

  const bool layered = policy == Policy::M9 || policy == Policy::Ble;
  if (layered && check(host.wizardOpen, context)) {
    if (host.wizardBack) host.wizardBack(context);
    return Result::Handled;
  }

  if (policy == Policy::M9 && check(host.panMode, context)) {
    const bool stale = !check(host.mapActive, context) || check(host.popupOpen, context);
    const bool safeToContinue = host.clearPan && host.clearPan(context, !stale);
    if (!stale) return Result::Handled;
    // A stale pan flag should not cost a Back press when clearing it is known
    // to be a plain flag assignment. Other callbacks may rebuild the UI.
    if (!safeToContinue) return Result::Handled;
  }

  if (layered && check(host.dropdownOpen, context)) {
    if (host.dismissDropdown) host.dismissDropdown(context);
    return Result::Handled;
  }
  if (layered && check(host.powerMenuOpen, context)) {
    if (host.closePowerMenu) host.closePowerMenu(context);
    return Result::Handled;
  }
  if (layered && check(host.controlCenterOpen, context)) {
    if (host.closeControlCenter) host.closeControlCenter(context);
    return Result::Handled;
  }

  if ((policy == Policy::TanmatsuRed || policy == Policy::TanmatsuNav) &&
      check(host.editing, context)) {
    if (host.stopEditing) host.stopEditing(context);
    return Result::Handled;
  }

  if (layered && !check(host.confirmOpen, context) && host.pageCloser) {
    const UiApplication::Close close = host.pageCloser(context);
    if (close) {
      close(); // capture the owner once; its callback may replace the current page
      return Result::Handled;
    }
  }

  if (policy == Policy::Ble) {
    if (check(host.popupAboveBase, context) || check(host.selectionMode, context))
      return dismiss(host, true);
    if (host.chatOwner) {
      void *const owner = host.chatOwner(context);
      if (owner) return chat(host, owner);
    }
    if (check(host.popupOpen, context)) return dismiss(host, false);
  } else {
    if (check(host.popupOpen, context) ||
        (policy == Policy::M9 && check(host.selectionMode, context)))
      return dismiss(host, false);
    // Pager closes an app page after popups. Other policies have no page rung.
    if (policy == Policy::Pager && host.pageCloser) {
      const UiApplication::Close close = host.pageCloser(context);
      if (close) { close(); return Result::Handled; }
    }
    if (policy == Policy::M9 || policy == Policy::Pager || policy == Policy::TanmatsuRed ||
        policy == Policy::TanmatsuNav) {
      if (host.chatOwner) {
        void *const owner = host.chatOwner(context);
        if (owner) return chat(host, owner);
      }
    }
  }

  if (policy == Policy::M9 && check(host.historyBack, context)) return Result::Handled;
  if (policy == Policy::M9 || policy == Policy::Pager || policy == Policy::Ble) {
    if (!check(host.onHome, context)) {
      if (host.goHome) host.goHome(context);
      return Result::Handled;
    }
    if (policy == Policy::Ble) return Result::Root;
  }
  return escape(host, pressed);
}

} // namespace back
} // namespace ui

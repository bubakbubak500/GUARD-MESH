// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "UiApplication.h"
#include <stdint.h>

namespace ui {
namespace back {

enum class Policy { Pager, M9, TanmatsuRed, TanmatsuNav, TanmatsuText, Ble };
enum class Result { Handled, Blocked, Escape, Root, Reentered };

// Queries must describe the current UI, not a snapshot from before another
// callback ran. All callbacks run on the UI thread. No widget is retained.
struct Host {
  void *context = nullptr;
  bool (*wizardOpen)(void *) = nullptr;
  void (*wizardBack)(void *) = nullptr;
  bool (*panMode)(void *) = nullptr;
  bool (*mapActive)(void *) = nullptr;
  // True only when cleanup cannot change the current navigation context.
  // A missing callback or false result consumes this Back press.
  bool (*clearPan)(void *, bool announce) = nullptr;
  bool (*dropdownOpen)(void *) = nullptr;
  void (*dismissDropdown)(void *) = nullptr;
  bool (*powerMenuOpen)(void *) = nullptr;
  void (*closePowerMenu)(void *) = nullptr;
  bool (*controlCenterOpen)(void *) = nullptr;
  void (*closeControlCenter)(void *) = nullptr;
  bool (*confirmOpen)(void *) = nullptr;
  UiApplication::Close (*pageCloser)(void *) = nullptr;
  bool (*popupOpen)(void *) = nullptr;
  bool (*popupAboveBase)(void *) = nullptr;
  UiApplication::Dismiss (*dismissPopup)(void *) = nullptr;
  bool (*selectionMode)(void *) = nullptr;
  void *(*chatOwner)(void *) = nullptr;
  void (*closeChat)(void *, void *) = nullptr;
  bool (*historyBack)(void *) = nullptr;
  bool (*onHome)(void *) = nullptr;
  void (*goHome)(void *) = nullptr;
  bool (*editing)(void *) = nullptr;
  void (*stopEditing)(void *) = nullptr;
  void (*escape)(void *, bool pressed) = nullptr;
  bool (*settingsFromControlCenter)(void *) = nullptr;
  bool (*settingsSheetOpen)(void *) = nullptr;
  void (*openControlCenter)(void *) = nullptr;
};

// One Back press peels at most one visible layer. A blocked popup consumes the
// press; a popup that disappears during a query also cannot expose its parent.
class BackNavigation {
public:
  Result route(Policy policy, const Host &host, bool pressed = true);
private:
  bool _routing = false;
};

} // namespace back
} // namespace ui

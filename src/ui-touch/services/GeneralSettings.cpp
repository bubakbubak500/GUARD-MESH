// SPDX-License-Identifier: GPL-3.0-or-later
#include "GeneralSettings.h"
#include "../../helpers/esp32/TouchPrefsStore.h"
namespace ui {
namespace {
const uint16_t history[] = {100, 250, 500, 1000, 2000, 0};
const uint8_t fallback[] = {0, 1, 2, 3, 5, 8};
template <typename T, unsigned N> unsigned indexOf(const T (&values)[N], T value, unsigned missing) {
  for (unsigned i = 0; i < N; ++i)
    if (values[i] == value)
      return i;
  return missing;
}
} // namespace
GeneralSettings::Storage GeneralSettings::storageStatus(bool want, const StorageSnapshot &state) {
  if (state.contactsOnSd && !state.cardPresent)
    return want ? Storage::MissingFullData : Storage::MissingCard;
  if (want && state.migrationBlocked)
    return Storage::MigrationBlocked;
  if (state.contactsOnSd)
    return Storage::Sd;
  return want ? Storage::MountFailed : Storage::Internal;
}
GeneralSettings::State GeneralSettings::read() const {
  State state;
  state.history = indexOf(history, touchPrefsGetHistPerChat(), 1);
  state.fallback = indexOf(fallback, touchPrefsGetHistSyncAfter(), 2);
  state.sd = touchPrefsGetUseSdStorage();
  state.console = touchPrefsGetConsoleMode();
  StorageSnapshot snapshot;
  if (_host.storage)
    _host.storage(_host.context, snapshot);
  state.storage = storageStatus(state.sd, snapshot);
  return state;
}
bool GeneralSettings::setHistory(unsigned index) {
  return index < sizeof history / sizeof history[0] && touchPrefsSetHistPerChat(history[index]);
}
bool GeneralSettings::setFallback(unsigned index) {
  return index < sizeof fallback / sizeof fallback[0] && touchPrefsSetHistSyncAfter(fallback[index]);
}
bool GeneralSettings::setSd(bool on) { return _host.capabilities.sd && touchPrefsSetUseSdStorage(on); }
bool GeneralSettings::setConsole(bool on) {
  if (!_host.capabilities.console || !_host.action || !touchPrefsSetConsoleMode(on))
    return false;
  _host.action(_host.context, on ? Action::ConsoleOn : Action::ConsoleOff);
  return true;
}
bool GeneralSettings::advert() { return _host.advert && _host.advert(_host.context); }
void GeneralSettings::heardNames(unsigned& count, unsigned& capacity) const {
  count = capacity = 0;
  if (_host.heardNames) _host.heardNames(_host.context, count, capacity);
}
bool GeneralSettings::clearHeardNames() { return _host.clearHeardNames && _host.clearHeardNames(_host.context); }
bool GeneralSettings::action(Action action) {
  if (!_host.action)
    return false;
  if (action == Action::Recover && !_host.capabilities.recovery)
    return false;
  if (action != Action::Recover && action != Action::Setup && action != Action::Reboot)
    return false;
  _host.action(_host.context, action);
  return true;
}
} // namespace ui

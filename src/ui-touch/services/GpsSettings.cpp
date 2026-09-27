// SPDX-License-Identifier: GPL-3.0-or-later
#include "GpsSettings.h"
#include "../../helpers/esp32/TouchPrefsStore.h"
#include "../i18n.h"
namespace ui {
GpsSettings::State GpsSettings::read() const {
  State state{};
  if (_host.snapshot)
    _host.snapshot(_host.context, state.position);
  state.baud = touchPrefsGetGpsBaud(_host.defaultBaud);
  state.privacy = touchPrefsGetGpsFuzzM();
  return state;
}
void GpsSettings::setEnabled(bool on) {
  if (_host.setEnabled)
    _host.setEnabled(_host.context, on);
}
uint32_t GpsSettings::baud(unsigned index) {
  static const uint32_t values[] = {9600, 19200, 38400, 57600, 115200};
  return index < BaudCount ? values[index] : 0;
}
uint16_t GpsSettings::privacy(unsigned index) {
  static const uint16_t values[] = {0, 100, 250, 1000};
  return index < PrivacyCount ? values[index] : 0;
}
bool GpsSettings::setBaud(unsigned index) {
  return index < BaudCount &&
         (touchPrefsGetGpsBaud(_host.defaultBaud) == baud(index) || touchPrefsSetGpsBaud(baud(index)));
}
bool GpsSettings::setPrivacy(unsigned index) {
  // Repeated taps must not rewrite the complete preferences blob.
  return index < PrivacyCount &&
         (touchPrefsGetGpsFuzzM() == privacy(index) || touchPrefsSetGpsFuzzM(privacy(index)));
}
void GpsSettings::format(const gps::Snapshot &position, uint32_t now, bool compact, char *text,
                         size_t capacity) {
  _status.format(
      position, now, compact,
      {TR("GPS: off"), TR("GPS: searching"), TR("GPS: fix"),
       TR("No position yet. A cold start needs a clear view of the sky and can take several minutes.")},
      text, capacity);
}
void GpsSettings::status(uint32_t now, bool compact, char *text, size_t capacity) {
  gps::Snapshot position;
  if (_host.snapshot)
    _host.snapshot(_host.context, position);
  format(position, now, compact, text, capacity);
}
} // namespace ui

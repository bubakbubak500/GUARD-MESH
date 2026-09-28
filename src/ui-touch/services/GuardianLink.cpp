// SPDX-License-Identifier: GPL-3.0-or-later
#include "GuardianLink.h"
#include <stdio.h>
#if defined(ESP32) && !defined(GUARD_SIMULATOR)
#include <freertos/FreeRTOS.h>
namespace { portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
struct Lock { Lock() { portENTER_CRITICAL(&mux); } ~Lock() { portEXIT_CRITICAL(&mux); } }; }
#else
#include <mutex>
namespace { std::mutex mux; using Lock = std::lock_guard<std::mutex>; }
#endif
namespace guardian {
namespace {
Snapshot state;
Command pending = Command::None;
uint32_t pairDeadline = 0;
struct Guard {
#if defined(ESP32) && !defined(GUARD_SIMULATOR)
  Lock lock;
#else
  Lock lock{mux};
#endif
};
}
Snapshot snapshot(uint32_t now) {
  Guard guard;
  Snapshot result = state;
  result.pairing = pairDeadline && int32_t(pairDeadline - now) > 0;
  result.pairingSeconds = result.pairing ? (pairDeadline - now + 999) / 1000 : 0;
  // Stale numbers are deliberately absent from the UI-facing snapshot.
  if (!result.session.fresh(now)) result.session.status = Status{};
  return result;
}
void request(Command command) { Guard guard; pending = command; }
Command takeCommand() { Guard guard; const auto cmd = pending; pending = Command::None; return cmd; }
void configure(bool enabled, bool radio, bool ready) {
  Guard guard; state.enabled = enabled; state.radio = radio; state.ready = ready;
  if (!enabled || !radio) { state.session.disconnect(); pairDeadline = 0; }
}
void pairingUntil(uint32_t deadline) { Guard guard; pairDeadline = deadline; }
void deviceName(const char* name) { Guard guard; snprintf(state.deviceName, sizeof state.deviceName, "%s", name ? name : ""); }
void connected() { Guard guard; state.session.connect(); }
void disconnected() { Guard guard; state.session.disconnect(); }
Result receive(const uint8_t* bytes, size_t size, uint32_t now) {
  Guard guard; return state.session.accept(bytes, size, now);
}
}

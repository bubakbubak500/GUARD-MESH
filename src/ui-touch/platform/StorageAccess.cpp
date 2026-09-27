// SPDX-License-Identifier: GPL-3.0-or-later
#include "StorageAccess.h"

#ifdef ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace ui { namespace platform {

StorageAccess::Reader::Reader(StorageAccess& access, uintptr_t context) : _access(access) {
  uint32_t state = _access._state.load(std::memory_order_acquire);
  for (;;) {
    if (state & kWriterRequested) {
      // A lifecycle owner can call filesystem helpers while its own exclusive
      // operation is active. It must keep this borrowed scope inside the
      // lifetime of the outer Transition.
      _acquired = context != 0 && (state & kWriterActive) != 0 &&
                  (state & kReaderMask) == 0 &&
                  _access._owner.load(std::memory_order_acquire) == context &&
                  _access._state.load(std::memory_order_acquire) == state;
      return;
    }
    if ((state & kReaderMask) == kReaderMask) return;
    if (_access._state.compare_exchange_weak(state, state + 1,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
      _acquired = _counted = true;
      return;
    }
  }
}

void StorageAccess::Reader::release() {
  if (!_acquired) return;
  if (_counted) _access._state.fetch_sub(1, std::memory_order_release);
  _acquired = _counted = false;
}

StorageAccess::Transition::Transition(StorageAccess& access, uintptr_t context)
    : Transition() {
  request(access, context);
}

bool StorageAccess::Transition::request(StorageAccess& access, uintptr_t context) {
  if (_requested) return _access == &access && _context == context;
  if (!context) return false;
  uint32_t state = access._state.load(std::memory_order_acquire);
  if (state & kWriterRequested) {
    // A nested transition is borrowed only inside the active lifecycle call.
    if ((state & kWriterActive) == 0 || (state & kReaderMask) != 0 ||
        access._owner.load(std::memory_order_acquire) != context ||
        access._state.load(std::memory_order_acquire) != state)
      return false;
    _access = &access;
    _context = context;
    _requested = true;
    return true;
  }
  do {
    if (state & kWriterRequested) return false;
  } while (!access._state.compare_exchange_weak(state, state | kWriterRequested,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire));
  access._owner.store(context, std::memory_order_release);
  _access = &access;
  _context = context;
  _requested = _ownsReservation = true;
  return true;
}

bool StorageAccess::Transition::ready() const {
  if (!_requested) return false;
  const uint32_t state = _access->_state.load(std::memory_order_acquire);
  return (state == kWriterRequested || state == (kWriterRequested | kWriterActive)) &&
         _access->_owner.load(std::memory_order_acquire) == _context;
}

bool StorageAccess::Transition::enter() {
  if (!ready()) return false;
  if (!_ownsReservation) return true; // borrowed from the active owner
  uint32_t expected = kWriterRequested;
  return _access->_state.compare_exchange_strong(expected,
                                                  kWriterRequested | kWriterActive,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire) ||
         expected == (kWriterRequested | kWriterActive);
}

void StorageAccess::Transition::release() {
  if (!_requested) return;
  if (_ownsReservation) {
    _access->_owner.store(0, std::memory_order_release);
    _access->_state.fetch_and(kReaderMask, std::memory_order_release);
  }
  _requested = _ownsReservation = false;
  _access = nullptr;
  _context = 0;
}

bool StorageAccess::admissionClosed() const {
  return (_state.load(std::memory_order_acquire) & kWriterRequested) != 0;
}

uint32_t StorageAccess::readerCount() const {
  return static_cast<uint32_t>(_state.load(std::memory_order_acquire) & kReaderMask);
}

StorageAccess& storageAccess() {
  static StorageAccess access;
  return access;
}

uintptr_t currentStorageContext() {
#ifdef ESP32
  return reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle());
#else
  static thread_local unsigned char token;
  return reinterpret_cast<uintptr_t>(&token);
#endif
}

} } // namespace ui::platform

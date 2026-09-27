// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>

namespace ui { namespace platform {

// Arbitrates the lifetime of an already mounted filesystem. Admission is
// nonblocking: callers keep queued work queued when a lease is unavailable.
// No gate operation performs I/O or waits for an existing reader.
class StorageAccess {
 public:
  StorageAccess() = default;
  StorageAccess(const StorageAccess&) = delete;
  StorageAccess& operator=(const StorageAccess&) = delete;

  class Reader {
   public:
    Reader(StorageAccess& access, uintptr_t context);
    ~Reader() { release(); }
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    bool acquired() const { return _acquired; }
    explicit operator bool() const { return acquired(); }
    void release();

   private:
    StorageAccess& _access;
    bool _acquired = false;
    bool _counted = false;
  };

  class Transition {
   public:
    Transition() = default;
    Transition(StorageAccess& access, uintptr_t context);
    ~Transition() { release(); }
    Transition(const Transition&) = delete;
    Transition& operator=(const Transition&) = delete;

    bool request(StorageAccess& access, uintptr_t context);
    bool requested() const { return _requested; }
    explicit operator bool() const { return requested(); }
    // Drained is not active: enter immediately before synchronous lifecycle
    // I/O, and release before returning to the event loop.
    bool ready() const;
    bool enter();
    void release();

   private:
    StorageAccess* _access = nullptr;
    uintptr_t _context = 0;
    bool _requested = false;
    bool _ownsReservation = false;
  };

  bool admissionClosed() const;
  uint32_t readerCount() const;

 private:
  friend class Reader;
  friend class Transition;
  static const uint32_t kWriterRequested = uint32_t(1) << 31;
  static const uint32_t kWriterActive = uint32_t(1) << 30;
  static const uint32_t kReaderMask = ~(kWriterRequested | kWriterActive);

  // One CAS closes admission and snapshots all prior reader admissions.
  std::atomic<uint32_t> _state{0};
  // Published only after the reservation CAS, cleared before reopening it.
  std::atomic<uintptr_t> _owner{0};
};

StorageAccess& storageAccess();
uintptr_t currentStorageContext();

class StorageLease : public StorageAccess::Reader {
 public:
  StorageLease() : Reader(storageAccess(), currentStorageContext()) {}
};

class StorageTransition : public StorageAccess::Transition {
 public:
  explicit StorageTransition(bool requestNow = true) {
    if (requestNow) request();
  }
  bool request() { return Transition::request(storageAccess(), currentStorageContext()); }
};

} } // namespace ui::platform

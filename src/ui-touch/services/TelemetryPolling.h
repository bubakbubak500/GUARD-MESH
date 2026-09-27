// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

namespace ui {

// Fixed-size per-node auto-poll schedule. The host owns SD I/O and contact
// lookup; this class owns parsing, admission, due times, and request budgets.
class TelemetryPolling {
public:
  static const size_t Capacity = 8;
  static const size_t ConfigCapacity = 1024;
  static const uint16_t DefaultRequests = 10;
  static const uint16_t MaxRequests = 9999;
  static const uint16_t MaxIntervalMinutes = 1440;

  enum class ReadStatus : uint8_t { Ok, Missing, Error, TooLarge };
  enum class Status : uint8_t {
    Ready, StorageUnavailable, ReadError, TooLarge,
    MergeConflict, WriteFailed, Full, Invalid
  };
  struct Host {
    void *context = nullptr;
    bool (*storageAvailable)(void *) = nullptr;
    ReadStatus (*readConfig)(void *, char *, size_t, size_t *) = nullptr;
    // The adapter must return true only after writing exactly length bytes.
    bool (*writeConfig)(void *, const char *, size_t) = nullptr;
    // Includes contact lookup and actual send; failure still spends the slot.
    bool (*send)(void *, const uint8_t key[6]) = nullptr;
  };
  struct Outcome {
    Status status = Status::Ready;
    bool accepted = false;
    bool persisted = false;
    bool consumed = false;
    bool sent = false;
  };

  Status refresh(uint32_t nowMs, const Host &);
  Outcome set(const uint8_t key[6], int intervalMinutes, int requests,
              uint32_t nowMs, const Host &);
  Outcome tick(uint32_t nowMs, bool manualPending, int txBudget, const Host &);
  int interval(const uint8_t key[6]) const;
  int requestsLeft(const uint8_t key[6]) const;
  bool pendingPersistence() const { return _dirty || _pendingCount != 0; }
  bool loaded() const { return _loaded; }

private:
  struct Record {
    uint8_t key[6] = {};
    uint16_t intervalMinutes = 0;
    uint16_t requestsLeft = 0;
    uint32_t nextMs = 0;
    bool used = false;
  };
  struct Edit {
    uint8_t key[6] = {};
    uint16_t intervalMinutes = 0; // zero is a deletion tombstone
    uint16_t requests = 0;
    uint32_t nextMs = 0;
    bool used = false;
  };
  Record _records[Capacity] = {};
  Edit _pending[Capacity] = {};
  uint8_t _pendingCount = 0;
  uint8_t _cursor = 0;
  uint32_t _gapUntil = 0;
  bool _gapArmed = false;
  bool _loaded = false;
  bool _dirty = false;
  uint32_t _retryAt = 0;
  bool _retryArmed = false;
  Status _retryReason = Status::Ready;

  Status save(uint32_t nowMs, const Host &, bool force);
  void backoff(uint32_t nowMs, Status reason);
  static int find(const Record records[Capacity], const uint8_t key[6]);
  static int vacant(const Record records[Capacity]);
};

} // namespace ui

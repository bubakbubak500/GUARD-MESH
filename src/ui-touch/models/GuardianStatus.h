// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace guardian {
static constexpr uint32_t StaleMs = 15000;
struct Status {
  uint32_t inbox = 0, unread = 0, outbox = 0, sequence = 0;
  uint8_t flags = 0;
};
enum class Result { Ok, Length, Invalid, Sequence, Disconnected };
// Whole snapshots only. Never cast an unaligned BLE payload to uint32_t*.
inline uint32_t read32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
struct Session {
  Status status{};
  uint32_t receivedAt = 0;
  bool connected = false, received = false;
  void connect() { *this = Session{}; connected = true; }
  void disconnect() { *this = Session{}; }
  bool fresh(uint32_t now) const { return connected && received && uint32_t(now - receivedAt) < StaleMs; }
  Result accept(const uint8_t* p, size_t n, uint32_t now) {
    if (!connected) return Result::Disconnected;
    if (!p || n != 20) return Result::Length;
    if (p[0] != 'G' || p[1] != 'M' || p[2] != 1 || !(p[3] & 1) || (p[3] & 0xc0))
      return Result::Invalid;
    const uint32_t sequence = read32(p + 16);
    // Serial-number arithmetic supports wrap; duplicate/backward packets cannot
    // renew the heartbeat. A new connection has no previous sequence.
    const uint32_t delta = sequence - status.sequence;
    if (received && (delta == 0 || delta >= 0x80000000u)) return Result::Sequence;
    status.inbox = read32(p + 4); status.unread = read32(p + 8);
    status.outbox = read32(p + 12); status.sequence = sequence; status.flags = p[3];
    receivedAt = now; received = true;
    return Result::Ok;
  }
};
struct Snapshot {
  Session session{};
  bool enabled = false, radio = false, pairing = false, ready = false;
  uint32_t pairingSeconds = 0;
  char deviceName[24]{};
};
} // namespace guardian

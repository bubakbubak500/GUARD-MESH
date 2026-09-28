// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../models/GuardianStatus.h"
namespace guardian {
enum class Command { None, Enable, Disable, Pair, CancelPair };
Snapshot snapshot(uint32_t now);
void request(Command command);
Command takeCommand();
// Transport-owned, bounded shared mailbox. No LVGL calls from BLE callbacks.
void configure(bool enabled, bool radio, bool ready);
void deviceName(const char* name);
void pairingUntil(uint32_t deadline);
void connected();
void disconnected();
Result receive(const uint8_t* bytes, size_t size, uint32_t now);
Result progress(const uint8_t* bytes, size_t size);
} // namespace guardian

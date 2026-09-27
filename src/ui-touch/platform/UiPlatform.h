// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>
namespace ui { class RadioTransport; namespace platform {
// Platform boundary for shared services. Firmware/desktop supply exactly one
// implementation. Hardware and mesh access belongs in the adapters.
bool savePreferences();
void applyRadioPreferences();
int maxTransmitPower();
RadioTransport& radioTransport();
void* allocate(size_t bytes, bool prefer_external);
void release(void* memory);
uint32_t milliseconds();
void yieldUiWork();
bool localTime(time_t timestamp, struct tm& result);
void applyLocalTimezone(const char *timezone);
} }

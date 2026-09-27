// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace fs { class FS; }
namespace ui { namespace audio {
// Configured on the UI thread before any playback starts. The service owns all
// playback tasks/queues/codec state; it never receives an LVGL object.
struct Host { bool (*muted)(); uint16_t (*pendingTiles)(); };
void configure(Host host);
void playSlot(int slot);
void playNotification();
void playMention();
void previewWav(const char* path);
bool supportsWav(const char* path);
bool storageBusy();
bool notificationActive();
void setVolume(uint8_t percent);
uint8_t volume();
void beep();
bool play(fs::FS* fs, const char* path, const char* shown, const char* source,
          uint32_t owner, char* error, size_t capacity);
bool pause(uint32_t owner, bool paused);
bool stop(uint32_t owner, bool release);
void status(uint32_t owner, char* state, size_t state_capacity,
            char* path, size_t path_capacity, char* source, size_t source_capacity,
            char* format, size_t format_capacity, char* error, size_t error_capacity);
} }

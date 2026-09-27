// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum class UIEventType {
  none, contactMessage, channelMessage, roomMessage, newContactMessage, ack
};

// One synchronous mesh-to-UI delivery. Owns every value before notification or
// application callbacks can reenter the receiver. No pointers into packets,
// contact tables, shared serial frames or a "last received" metadata slot.
struct UIMessageEvent {
  static const size_t MAX_TEXT = 256;  // covers the entire companion/RF frame
  static const size_t MAX_NAME = 32;
  static const size_t PATH_CAPACITY = 32;
  UIEventType kind = UIEventType::contactMessage;
  char name[MAX_NAME + 1]{};
  char author[MAX_NAME + 1]{};
  char text[MAX_TEXT + 1]{};
  uint8_t pub[32]{};
  bool hasPub = false;
  int messageCount = 0;
  uint32_t senderTimestamp = 0;  // 0: use local delivery time
  uint8_t pathLen = 0;
  bool hasRx = false;
  bool isFlood = false;
  int8_t snrQ4 = 0;
  int8_t rssi = 0;
  uint8_t path[PATH_CAPACITY]{};
  uint8_t pathBytes = 0;
  bool hasScope = false;
  uint16_t scope = 0;
  bool scopeHome = false;
  uint8_t scopeSlot = 0;

  UIMessageEvent() = default;
  UIMessageEvent(UIEventType type, uint8_t hops, const uint8_t* key,
                 const char* thread, const char* body, int count,
                 const char* sender = nullptr)
      : kind(type), hasPub(key != nullptr), messageCount(count), pathLen(hops) {
    copy(name, thread);
    copy(author, sender);
    copy(text, body);
    if (key) memcpy(pub, key, sizeof(pub));
  }

private:
  template <size_t N> static void copy(char (&out)[N], const char* value) {
    if (value) strncpy(out, value, N - 1);
    out[N - 1] = '\0';
  }
};

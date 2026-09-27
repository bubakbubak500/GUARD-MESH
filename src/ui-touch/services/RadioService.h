// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
namespace ui {
struct RadioSendResult {
  enum Status { Sent, Failed, MissingChannel, MissingContact } status = Failed;
  uint32_t ack = 0, fingerprint = 0, hash = 0;
  int code = 0;
  bool has_hash = false;
};

// The transport owns protocol-specific types. The service owns the sequencing
// shared by channel and direct sends; neither owns a screen or composer draft.
class RadioTransport {
public:
  virtual ~RadioTransport() = default;
  virtual uint32_t uniqueTime() = 0;
  virtual bool channelMatches(int slot, const char* name) = 0;
  virtual bool contactPath(const uint8_t* key, int& path) = 0;
  virtual RadioSendResult channel(int slot, const char* name, uint32_t timestamp,
                                  const char* sender, const char* text) = 0;
  virtual RadioSendResult direct(const uint8_t* key, uint32_t timestamp,
                                 uint8_t attempt, const char* text) = 0;
};
class RadioService {
public:
  using Trace = void (*)(const char*);
  RadioSendResult sendChannel(RadioTransport& transport, int slot, const char* name,
                             const char* sender, const char* text, Trace trace = nullptr);
  RadioSendResult sendDirect(RadioTransport& transport, const uint8_t* key,
                            const char* text, Trace trace = nullptr);
private:
  uint32_t nextTime(RadioTransport& transport);
  uint32_t _last_timestamp = 0;
  uint8_t _attempt = 4;
};
}

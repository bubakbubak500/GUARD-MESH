// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../services/RadioService.h"
#include "../models/MessageTypes.h"
#include "../../helpers/esp32/TouchPrefsStore.h"
#include <cstring>
namespace ui { namespace platform {
// Instantiated by the ESP32/desktop adapters after their MeshCore types are
// included. Both use the same identity checks, scope handling and ACK binding.
template<class Mesh> class MeshRadioTransport final : public RadioTransport {
public:
  explicit MeshRadioTransport(Mesh& mesh) : _mesh(mesh) {}
  uint32_t uniqueTime() override { return _mesh.getRTCClock()->getCurrentTimeUnique(); }
  bool channelMatches(int slot, const char* name) override {
    ChannelDetails channel;
    return slot >= 0 && name && _mesh.getChannel(slot, channel) && channel.name[0] &&
           strncmp(channel.name, name, MessageTypes::MAX_THREAD_NAME) == 0;
  }
  bool contactPath(const uint8_t* key, int& path) override {
    auto* contact = key ? _mesh.lookupContactByPubKey(key, PUB_KEY_SIZE) : nullptr;
    if (!contact) return false;
    path = contact->out_path_len;
    return true;
  }
  RadioSendResult channel(int slot, const char* name, uint32_t timestamp,
                          const char* sender, const char* text) override {
    RadioSendResult result;
    ChannelDetails channel;
    if (!_mesh.getChannel(slot, channel) || !channel.name[0] ||
        strncmp(channel.name, name, MessageTypes::MAX_THREAD_NAME) != 0) {
      result.status = RadioSendResult::MissingChannel;
      return result;
    }
    char region[TOUCH_REGION_SCOPE_MAXLEN] = {};
    touchPrefsGetChannelScope(slot, region, sizeof region);
    const bool scoped = _mesh.pushChannelScope(region);
    const bool sent = _mesh.sendGroupMessage(timestamp, channel.channel, sender, text,
                                            static_cast<int>(strlen(text)));
    if (scoped) _mesh.popChannelScope();
    result.status = sent ? RadioSendResult::Sent : RadioSendResult::Failed;
    if (sent) result.fingerprint = _mesh.uiLastSentFp();
    return result;
  }
  RadioSendResult direct(const uint8_t* key, uint32_t timestamp,
                         uint8_t attempt, const char* text) override {
    RadioSendResult result;
    auto* contact = _mesh.lookupContactByPubKey(key, PUB_KEY_SIZE);
    if (!contact) { result.status = RadioSendResult::MissingContact; return result; }
    ContactInfo recipient = *contact;
    // Preserve the UI's existing per-message flood policy without changing the
    // contact's cached route, which other mesh clients may still use.
    recipient.out_path_len = OUT_PATH_UNKNOWN;
    uint32_t timeout = 0;
    result.code = _mesh.sendMessage(recipient, timestamp, attempt, text, result.ack, timeout);
    result.has_hash = _mesh.getLastTxtTxHash4(result.hash);
    if (result.code != MSG_SEND_FAILED) {
      _mesh.uiRegisterExpectedAck(result.ack, key);
      result.fingerprint = _mesh.uiLastSentFp();
      result.status = RadioSendResult::Sent;
    }
    return result;
  }
private:
  Mesh& _mesh;
};
} }

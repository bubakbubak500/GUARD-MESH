// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <string>
#include <algorithm>
#include <cstring>
#include <array>
namespace guardian {
// One outstanding operation. Ownership stays with the caller until take().
// No JSON decoding before END: UTF-8 characters may span ATT fragments.
class Rpc {
public:
  static constexpr size_t MaxBytes = 32768;
  bool connected = false, subscribed = false;
  bool busy() const { return pending; }
  bool ready() const { return connected && subscribed && !pending && reply.empty() && error.empty(); }
  void reset(bool online = false) {
    connected = online; subscribed = false;
    if (pending || !reply.empty()) error = "disconnected";
    pending = assembling = false; request.clear(); response.clear(); reply.clear();
    pos = index = 0;
  }
  bool start(const std::string& json, uint32_t now) {
    if (!ready() || json.empty() || json.size() > MaxBytes) return false;
    request = json; response.clear(); pending = true; assembling = false;
    started = now; pos = index = 0; ++transfer;
    return true;
  }
  size_t peek(uint8_t bytes[20]) const {
    if (!pending || !subscribed || pos >= request.size()) return 0;
    const size_t n = std::min(size_t(16), request.size() - pos);
    bytes[0] = (pos == 0 ? 1 : 0) | (pos + n == request.size() ? 2 : 0);
    bytes[1] = transfer; bytes[2] = uint8_t(pos / 16); bytes[3] = uint8_t((pos / 16) >> 8);
    memcpy(bytes + 4, request.data() + pos, n); return n + 4;
  }
  void sent(size_t n) { if (n >= 5 && n <= 20) pos += n - 4; }
  bool accept(const uint8_t* p, size_t n, uint32_t now) {
    if (!pending || expired(now) || pos != request.size() || !p || n < 5 || n > 20 || (p[0] & ~3) || p[1] != transfer)
      return false;
    const unsigned incoming = unsigned(p[2]) | unsigned(p[3]) << 8;
    if (p[0] & 1) {
      if (assembling || incoming) return false;
      assembling = true; assemblyAt = now; index = 0;
    }
    if (!assembling || incoming != index || response.size() + n - 4 > MaxBytes) return false;
    response.append(reinterpret_cast<const char*>(p + 4), n - 4); ++index;
    if (p[0] & 2) {
      reply.swap(response); pending = assembling = false; request.clear();
    }
    return true;
  }
  bool expired(uint32_t now) const {
    return pending && (uint32_t(now - started) >= 60000 ||
      (pos < request.size() && uint32_t(now - started) >= 30000) ||
      (assembling && uint32_t(now - assemblyAt) >= 30000));
  }
  void fail(const char* why) { reset(); error = why; }
  bool take(std::string& json, std::string& why) {
    if (reply.empty() && error.empty()) return false;
    json.swap(reply); reply.clear(); why.swap(error); error.clear(); return true;
  }
private:
  bool pending = false, assembling = false;
  uint8_t transfer = 0;
  size_t pos = 0;
  unsigned index = 0;
  uint32_t started = 0, assemblyAt = 0;
  std::string request, response, reply, error;
};

// Independent from the pending RPC: Response and Event may share a transfer ID.
// The BLE callback stores raw bytes only; JSON/UTF-8 decoding belongs to the UI.
class EventQueue {
public:
  static constexpr size_t MaxBytes = Rpc::MaxBytes;
  static constexpr size_t MaxEvents = 20;
  static constexpr size_t MaxQueuedBytes = 65536;
  void reset() {
    assembling = false; index = 0; head = count = queuedBytes = 0;
    std::string().swap(assembly);
    for (auto& event : events) std::string().swap(event);
  }
  bool expired(uint32_t now) const {
    return assembling && uint32_t(now - assemblyAt) >= 30000;
  }
  bool accept(const uint8_t* p, size_t n, uint32_t now) {
    if (!p || n < 5 || n > 20 || (p[0] & ~3) || expired(now)) return false;
    const unsigned incoming = unsigned(p[2]) | unsigned(p[3]) << 8;
    if (p[0] & 1) {
      if (assembling || incoming || count >= MaxEvents) return false;
      assembling = true; transfer = p[1]; index = 0; assemblyAt = now;
    }
    if (!assembling || p[1] != transfer || incoming != index ||
        assembly.size() + n - 4 > MaxBytes || queuedBytes + assembly.size() + n - 4 > MaxQueuedBytes)
      return false;
    assembly.append(reinterpret_cast<const char*>(p + 4), n - 4); ++index;
    if (p[0] & 2) {
      queuedBytes += assembly.size();
      events[(head + count) % MaxEvents].swap(assembly); ++count;
      assembling = false;
    }
    return true;
  }
  bool take(std::string& json) {
    if (!count) return false;
    json.swap(events[head]); queuedBytes -= json.size();
    std::string().swap(events[head]);
    head = (head + 1) % MaxEvents; --count;
    return true;
  }
private:
  bool assembling = false;
  uint8_t transfer = 0;
  unsigned index = 0;
  uint32_t assemblyAt = 0;
  size_t head = 0, count = 0, queuedBytes = 0;
  std::string assembly;
  std::array<std::string, MaxEvents> events;
};
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "GuardianRpcLink.h"
#include "../models/GuardianRpc.h"
#if defined(ESP32) && !defined(GUARD_SIMULATOR)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
namespace {
SemaphoreHandle_t mutex() { static auto value = xSemaphoreCreateMutex(); return value; }
struct Lock { Lock() { xSemaphoreTake(mutex(), portMAX_DELAY); } ~Lock() { xSemaphoreGive(mutex()); } };
}
#else
#include <mutex>
namespace { std::mutex mutex; struct Lock { std::lock_guard<std::mutex> guard{mutex}; }; }
#endif
namespace guardian {
namespace {
Rpc rpc;
EventQueue events;
bool abortLink = false;
bool hasOwner = false;
void failLink(const char* why) {
  rpc.fail(why);
  // An idle Event fault has no RPC caller to consume its error. Retain errors
  // for an outstanding request, including a completed but untaken response.
  if (!hasOwner) { std::string json,error; rpc.take(json,error); }
  events.reset(); abortLink = true;
}
}
void rpcConnect() { Lock lock; rpc.reset(true); events.reset(); abortLink = false; }
void rpcDisconnect() { Lock lock; rpc.reset(); events.reset(); }
void rpcAbort() { Lock lock; rpc.reset(); events.reset(); abortLink = true; }
void rpcSubscribe(bool enabled) { Lock lock; rpc.subscribed = enabled; }
bool rpcReady() { Lock lock; return rpc.ready(); }
bool rpcBusy() { Lock lock; return rpc.busy(); }
bool rpcStart(const std::string& json, uint32_t now) { Lock lock; const bool ok=rpc.start(json, now); if(ok) hasOwner=true; return ok; }
bool rpcTake(std::string& json, std::string& error) { Lock lock; const bool ok=rpc.take(json, error); if(ok) hasOwner=false; return ok; }
bool rpcReceive(const uint8_t* bytes, size_t size, uint32_t now) {
  Lock lock;
  if (rpc.accept(bytes, size, now)) return true;
  failLink("protocol_error"); return false;
}
bool eventReceive(const uint8_t* bytes, size_t size, uint32_t now) {
  Lock lock;
  if (rpc.connected && events.accept(bytes, size, now)) return true;
  failLink("protocol_error"); return false;
}
bool eventTake(std::string& json) { Lock lock; return events.take(json); }
bool rpcTick(uint32_t now, bool (*notify)(const uint8_t*, size_t)) {
  Lock lock;
  if (abortLink) { abortLink = false; return false; }
  if (rpc.expired(now) || events.expired(now)) { failLink("timeout"); return false; }
  uint8_t bytes[20];
  const size_t n = rpc.peek(bytes);
  if (n && notify && notify(bytes, n)) rpc.sent(n);
  return true;
}
}

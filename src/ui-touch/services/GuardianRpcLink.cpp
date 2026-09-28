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
namespace { Rpc rpc; bool abortLink = false; }
void rpcConnect() { Lock lock; rpc.reset(true); abortLink = false; }
void rpcDisconnect() { Lock lock; rpc.reset(); }
void rpcAbort() { Lock lock; rpc.reset(); abortLink = true; }
void rpcSubscribe(bool enabled) { Lock lock; rpc.subscribed = enabled; }
bool rpcReady() { Lock lock; return rpc.ready(); }
bool rpcBusy() { Lock lock; return rpc.busy(); }
bool rpcStart(const std::string& json, uint32_t now) { Lock lock; return rpc.start(json, now); }
bool rpcTake(std::string& json, std::string& error) { Lock lock; return rpc.take(json, error); }
bool rpcReceive(const uint8_t* bytes, size_t size, uint32_t now) {
  Lock lock;
  if (rpc.accept(bytes, size, now)) return true;
  rpc.fail("protocol_error"); return false;
}
bool rpcTick(uint32_t now, bool (*notify)(const uint8_t*, size_t)) {
  Lock lock;
  if (abortLink) { abortLink = false; return false; }
  if (rpc.expired(now)) { rpc.fail("timeout"); return false; }
  uint8_t bytes[20];
  const size_t n = rpc.peek(bytes);
  if (n && notify && notify(bytes, n)) rpc.sent(n);
  return true;
}
}

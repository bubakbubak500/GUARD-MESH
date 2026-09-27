// SPDX-License-Identifier: GPL-3.0-or-later
#include "StorageUsage.h"
#include "../platform/StorageAccess.h"
namespace ui {
bool StorageUsage::refresh(uint32_t now, bool (*ensureExecutor)()) {
  poll();
  if (_state.load(std::memory_order_acquire) != Idle || (_scheduled && uint32_t(now - _lastRequest) < 30000))
    return false;
  _scheduled = true;
  _lastRequest = now;
  if (!ensureExecutor || !ensureExecutor())
    return false;
  _requestGeneration = _generation;
  _state.store(Queued, std::memory_order_release);
  return true;
}
void StorageUsage::poll() {
  const auto generation = _mountGeneration.load(std::memory_order_acquire);
  if (_generation != generation) {
    _generation = generation;
    _snapshot = Snapshot{};
    _scheduled = false;
    State expected = Queued;
    _state.compare_exchange_strong(expected, Idle, std::memory_order_acq_rel);
  }
  if (_state.load(std::memory_order_acquire) != Ready)
    return;
  if (_requestGeneration == _mountGeneration.load(std::memory_order_acquire))
    _snapshot = _result;
  _state.store(Idle, std::memory_order_release);
}
void StorageUsage::invalidate() { _mountGeneration.fetch_add(1, std::memory_order_release); }
bool StorageUsage::busy() const {
  auto state = _state.load(std::memory_order_acquire);
  return state == Queued || state == Running;
}
bool StorageUsage::run(void *context, Read read) {
  platform::StorageLease lease;
  if (!lease.acquired()) return false;
  State expected = Queued;
  if (!_state.compare_exchange_strong(expected, Running, std::memory_order_acquire))
    return false;
  _result = _requestGeneration == _mountGeneration.load(std::memory_order_acquire) && read ? read(context)
                                                                                           : Snapshot{};
  _result.done = true;
  if (!_result.ok || !_result.total || _result.free > _result.total) {
    _result.ok = false;
    _result.total = _result.free = 0;
  }
  _state.store(Ready, std::memory_order_release);
  return true;
}
} // namespace ui

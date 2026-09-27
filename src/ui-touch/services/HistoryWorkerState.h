// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
namespace ui { namespace history {
// Single producer, single worker. Claim and cancellation compete on ONE state,
// so the producer cannot mistake the queued -> running transition for idle.
class HistoryWorkerState {
public:
  bool queue() { return transition(Idle, Queued); }
  bool claim() { return transition(Queued, Running); }
  bool cancel() { return transition(Queued, Idle); }
  void finish() { _state.store(Idle, std::memory_order_release); }
  bool active() const { return _state.load(std::memory_order_acquire) != Idle; }
  bool running() const { return _state.load(std::memory_order_acquire) == Running; }
  bool pending() const { return _state.load(std::memory_order_acquire) == Queued; }
private:
  enum State : unsigned { Idle, Queued, Running };
  bool transition(State from, State to) {
    return _state.compare_exchange_strong(from, to, std::memory_order_acq_rel);
  }
  std::atomic<State> _state{Idle};
};
} }

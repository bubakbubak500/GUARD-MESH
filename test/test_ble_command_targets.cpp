// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/BleKeyboard.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>

void bleCommandTargetsRegression() {
  using namespace BleKbd;
  Device chosen = {};
  std::strcpy(chosen.name, "First keyboard");
  for (unsigned i = 0; i < sizeof chosen.addr; ++i) chosen.addr[i] = uint8_t(i + 1);
  chosen.addr_type = 2;

  // This is the same owned command target copied into the FreeRTOS queue.
  detail::CommandTarget queuedPair = detail::ownTarget(&chosen, 0);
  detail::CommandTarget copiedQueueSlot = queuedPair;
  chosen.addr[0] = 99;
  std::strcpy(chosen.name, "Rescanned keyboard");
  assert(copiedQueueSlot.device.addr[0] == 1);
  assert(std::strcmp(copiedQueueSlot.device.name, "First keyboard") == 0);

  Device current = copiedQueueSlot.device;
  detail::CommandTarget queuedForget = detail::ownTarget(&current, 17);
  std::strcpy(current.name, "Renamed keyboard");
  assert(detail::matchesPeer(queuedForget, true, current, 17));
  assert(!detail::matchesPeer(queuedForget, true, current, 18));
  current.addr_type = 1;
  assert(!detail::matchesPeer(queuedForget, true, current, 17));
  current = copiedQueueSlot.device;
  current.addr[5] ^= 1;
  assert(!detail::matchesPeer(queuedForget, true, current, 17));
  current = copiedQueueSlot.device;
  assert(!detail::matchesPeer(queuedForget, false, current, 17));

  detail::CommandTarget noPeer = detail::ownTarget(nullptr, 20);
  assert(detail::matchesPeer(noPeer, false, current, 20));
  assert(!detail::matchesPeer(noPeer, true, current, 20));
  assert(!detail::matchesPeer(noPeer, false, current, 21));

  // The same transition is used by the worker's final recheck and the
  // no-worker forget path; an old queued command cannot clear a new owner.
  bool havePeer = true;
  bool forgotten = false;
  uint32_t revision = 18;
  current = copiedQueueSlot.device;
  assert(!detail::clearMatchedPeer(queuedForget, havePeer, current, revision, forgotten));
  assert(havePeer && !forgotten && revision == 18);
  detail::CommandTarget freshForget = detail::ownTarget(&current, revision);
  assert(detail::clearMatchedPeer(freshForget, havePeer, current, revision, forgotten));
  assert(!havePeer && forgotten && revision == 19);
  assert(!detail::clearMatchedPeer(freshForget, havePeer, current, revision, forgotten));
}

#ifdef BLE_COMMAND_TARGET_STANDALONE
int main() { bleCommandTargetsRegression(); }
#endif

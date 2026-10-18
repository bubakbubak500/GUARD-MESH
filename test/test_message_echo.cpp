// SPDX-License-Identifier: GPL-3.0-or-later
// MessageEchoMethods.h contains production MyMesh fields and inline methods.
#include "MessageEchoMethods.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>

int main() {
  MessageEcho echo;
  echo.uiTrackSentFp(0);
  echo.uiTrackSentFp(7);
  assert(!echo.takeEchoDirty());
  assert(echo.uiCountEcho(7) && echo.uiRepeatsForFp(7) == 1);
  assert(echo.takeEchoDirty() && !echo.takeEchoDirty());
  echo.uiTrackSentFp(7);
  assert(echo.uiRepeatsForFp(7) == 0 && echo.takeEchoDirty());
  echo.uiTrackSentFp(7);
  assert(!echo.takeEchoDirty());
  // Replacing the oldest fingerprint removes its visible repeat suffix.
  assert(echo.uiCountEcho(7));
  echo.takeEchoDirty();
  for (uint32_t fp = 100; fp < 100 + MessageEcho::UI_ECHO_SLOTS - 1; ++fp) echo.uiTrackSentFp(fp);
  assert(!echo.takeEchoDirty() && echo.uiRepeatsForFp(7) == 1);
  echo.uiTrackSentFp(999);
  assert(echo.uiRepeatsForFp(7) == 0 && echo.takeEchoDirty() && !echo.takeEchoDirty());
  echo.uiTrackSentFp(1000);
  assert(!echo.takeEchoDirty());
  for (int i = 0; i < 300; ++i) assert(echo.uiCountEcho(999));
  assert(echo.uiRepeatsForFp(999) == 255 && echo.takeEchoDirty());
  echo.uiTrackSentFp(999);
  assert(echo.uiRepeatsForFp(999) == 0 && echo.takeEchoDirty());
  assert(!echo.uiCountEcho(42) && !echo.takeEchoDirty());
  puts("Message echoes: count/reset/eviction/saturation and one-shot UI invalidation PASS");
}

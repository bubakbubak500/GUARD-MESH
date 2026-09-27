// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/models/GpsStatus.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
void gpsStatusRegression() {
  using namespace ui::gps;
  Status status;
  Snapshot snapshot;
  const Labels labels{"GPS: off", "GPS: searching", "GPS: fix", "Cold start needs clear sky."};
  char text[240];
  status.format(snapshot, 0, false, labels, text, sizeof text);
  assert(!strcmp(text, "GPS: off"));
  snapshot.enabled = true;
  status.acquisition(true, 0);
  status.format(snapshot, 65000, false, labels, text, sizeof text);
  assert(!strcmp(text, "GPS: searching 1m05s\nCold start needs clear sky."));
  status.format(snapshot, 1200000, true, labels, text, sizeof text);
  assert(!strcmp(text, "GPS: searching 20m"));
  // The second consumer does not restart the acquisition clock.
  status.format(snapshot, 1201000, false, labels, text, sizeof text);
  assert(strstr(text, "20m\n"));
  status.acquisition(true, UINT32_MAX - 499);
  status.format(snapshot, 500, true, labels, text, sizeof text);
  assert(!strcmp(text, "GPS: searching 0m01s"));
  snapshot.fix = true;
  snapshot.satellites = 8;
  snapshot.altitude = 120;
  snapshot.latitude = 50.12345;
  snapshot.longitude = 14.54321;
  status.format(snapshot, 1000, false, labels, text, sizeof text);
  assert(strstr(text, "8 sats") && strstr(text, "120 m\n50.12345, 14.54321"));
  status.format(snapshot, 1000, true, labels, text, sizeof text);
  assert(!strchr(text, '\n') && !strstr(text, "120 m") && strstr(text, "50.12345"));
  snapshot.fix = false;
  status.format(snapshot, 3000, true, labels, text, sizeof text);
  assert(!strcmp(text, "GPS: searching 0m00s"));
  snapshot.enabled = false;
  status.format(snapshot, 4000, true, labels, text, sizeof text);
  snapshot.enabled = true;
  status.format(snapshot, 9000, true, labels, text, sizeof text);
  assert(!strcmp(text, "GPS: searching 0m00s"));
  for (size_t capacity = 0; capacity < 20; ++capacity) {
    char small[24];
    memset(small, '#', sizeof small);
    status.format(snapshot, 9000, false, labels, small + 1, capacity);
    assert(small[0] == '#' && small[capacity + 1] == '#');
    if (capacity)
      assert(memchr(small + 1, 0, capacity));
  }
  status.format(snapshot, 9000, false, labels, nullptr, 100);
  // Translations are text, never printf format strings.
  snapshot.enabled = false;
  status.format(snapshot, 0, false, {"off %s", "", "", ""}, text, sizeof text);
  assert(!strcmp(text, "off %s"));
}

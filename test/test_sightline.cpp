// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-touch/models/Sightline.h"
#include "ui-touch/services/SightlineJob.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
namespace {
using namespace ui;
struct Gate {
  std::mutex mutex;
  std::condition_variable wake;
  bool entered = false, release = false;
};
int fetch(void *context, const SightlineJob::Request &request, float *out, int &code) {
  auto &gate = *static_cast<Gate *>(context);
  std::unique_lock<std::mutex> lock(gate.mutex);
  gate.entered = true;
  gate.wake.notify_all();
  gate.wake.wait(lock, [&] { return gate.release; });
  assert(request.id == 7 && request.path.peerLat == 50.1 && !strcmp(request.server, "http://original"));
  for (int i = 0; i < sightline::Samples; ++i)
    out[i] = float(i);
  code = 200;
  return sightline::Samples;
}
} // namespace
void sightlineRegression() {
  using namespace ui::sightline;
  float elevations[Samples];
  for (auto &elevation : elevations)
    elevation = 100;
  Analysis a{};
  Path path{50, 14, 50, 14};
  assert(analyze(path, elevations, 2, 2, 868, a) && a.distanceKm == 0 && a.verdict == Clear);
  assert(std::isfinite(a.fresnelMargin) && a.clearance == 2);
  path.peerLon = 14.01;
  assert(analyze(path, elevations, 150, 150, 868, a) && a.verdict == Clear);
  assert(a.distanceKm > 0.714 && a.distanceKm < 0.716 && a.bearing > 89.9 && a.bearing < 90.1);
  assert(analyze(path, elevations, 2, 2, 868, a) && a.verdict == Marginal);
  elevations[12] = 300;
  assert(analyze(path, elevations, 2, 2, 868, a) && a.verdict == Blocked && a.worstIndex == 12);
  elevations[12] = INFINITY;
  assert(!analyze(path, elevations, 2, 2, 868, a));
  elevations[12] = 100;
  assert(analyze(path, elevations, 2, 2, NAN, a) && a.frequency == 868);
  assert(!analyze({91, 0, 0, 0}, elevations, 2, 2, 868, a));
  assert(!strcmp(compass(-90), "W") && !strcmp(compass(360), "N"));
  int count = parseElevations("null,10, null,30,inf,50, null ", elevations, Samples);
  assert(count == 7 && validElevations(elevations, count) == 3);
  assert(repairElevations(elevations, 8, count));
  assert(elevations[0] == 10 && elevations[2] == 20 && elevations[4] == 40 && elevations[7] == 50);
  assert(!repairElevations(elevations, 8, -1) && !repairElevations(elevations, 8, 9));
  assert(parseElevations("10junk,20", elevations, Samples) == 0);
  assert(parseElevations("nullx,20", elevations, Samples) == 0);
  assert(parseElevations("1,,2", elevations, Samples) == 0);
  count = parseElevations("1e999,NaN,null,3", elevations, Samples);
  assert(count == 4 && !repairElevations(elevations, Samples, count));
  char url[1100], small[32];
  assert(sampleUrl({0, 179, 0, -179}, "http://tiles///", url, sizeof url));
  assert(!strncmp(url, "http://tiles/elev?locations=0.00000,179.00000|", 43));
  assert(strstr(url, "0.00000,-179.00000") && !strstr(url, "0.00000,0.00000"));
  assert(!sampleUrl(path, "http://tiles", small, sizeof small) && small[0] == 0);
  SightlineJob job;
  SightlineJob::Request request{};
  request.id = 7;
  request.path = {50, 14, 50.1, 14.1};
  strcpy(request.server, "http://original");
  assert(job.request(request));
  request.id = 8;
  request.path.peerLat = 49;
  strcpy(request.server, "http://changed");
  Gate gate;
  std::thread worker([&] { assert(job.run({&gate, fetch, nullptr})); });
  {
    std::unique_lock<std::mutex> lock(gate.mutex);
    gate.wake.wait(lock, [&] { return gate.entered; });
  }
  SightlineJob::Result result{};
  assert(job.active() && job.attempt() == 1 && !job.take(result));
  assert(!job.request(request) && !job.run({nullptr, nullptr, nullptr}));
  {
    std::lock_guard<std::mutex> lock(gate.mutex);
    gate.release = true;
  }
  gate.wake.notify_all();
  worker.join();
  assert(!job.request(request)); // Ready remains owned until UI consumes it
  assert(job.take(result) && result.ok && result.request.id == 7 && result.elevations[23] == 23);
  assert(!job.active() && !job.take(result));
  struct Retry {
    int calls = 0, delays = 0;
  } retry;
  assert(job.request(request));
  assert(job.run({&retry,
                  [](void *context, const SightlineJob::Request &, float *out, int &code) {
                    auto &r = *static_cast<Retry *>(context);
                    ++r.calls;
                    code = 200;
                    for (int i = 0; i < Samples; ++i)
                      out[i] = r.calls < 3 ? NAN : 42;
                    return Samples;
                  },
                  [](void *context) { ++static_cast<Retry *>(context)->delays; }}));
  assert(job.take(result) && result.ok && result.valid == Samples && retry.calls == 3 && retry.delays == 2);
  assert(job.request(request) && job.run({nullptr, nullptr, nullptr}) && job.take(result) && !result.ok);
  request.server[0] = 0;
  assert(!job.request(request));
  std::puts("Sightline model/job: geometry, sparse data, bounded URL, immutable concurrent requests and "
            "retries passed.");
}

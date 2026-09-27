#include "ui-touch/services/TelemetryPolling.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using Poll = ui::TelemetryPolling;

struct Fake {
  bool available = true;
  bool missing = false;
  bool readError = false;
  bool tooLarge = false;
  bool writeFailure = false;
  bool sendResult = true;
  int reads = 0;
  int writes = 0;
  int sends = 0;
  Poll *reenter = nullptr;
  uint32_t reenterAt = 0;
  bool nestedConsumed = false;
  bool replacementAccepted = false;
  std::string config;
  std::vector<unsigned> sent;
  Poll::Host host() {
    Poll::Host h;
    h.context = this;
    h.storageAvailable = [](void *ctx) { return static_cast<Fake *>(ctx)->available; };
    h.readConfig = [](void *ctx, char *out, size_t capacity, size_t *length) {
      Fake &f = *static_cast<Fake *>(ctx);
      ++f.reads;
      if (f.readError) return Poll::ReadStatus::Error;
      if (f.tooLarge || f.config.size() > capacity) return Poll::ReadStatus::TooLarge;
      if (f.missing) return Poll::ReadStatus::Missing;
      std::memcpy(out, f.config.data(), f.config.size());
      *length = f.config.size();
      return Poll::ReadStatus::Ok;
    };
    h.writeConfig = [](void *ctx, const char *data, size_t length) {
      Fake &f = *static_cast<Fake *>(ctx);
      ++f.writes;
      if (f.writeFailure) return false;
      f.config.assign(data, length);
      return true;
    };
    h.send = [](void *ctx, const uint8_t key[6]) {
      Fake &f = *static_cast<Fake *>(ctx);
      ++f.sends;
      f.sent.push_back(key[5]);
      if (f.reenter) {
        Poll *poll = f.reenter;
        f.reenter = nullptr;
        f.nestedConsumed = poll->tick(f.reenterAt, false, 300, f.host()).consumed;
        f.replacementAccepted = poll->set(key, 2, 5, f.reenterAt, f.host()).accepted;
      }
      return f.sendResult;
    };
    return h;
  }
};

struct Key {
  uint8_t data[6];
  explicit Key(uint8_t last) : data{1,2,3,4,5,last} {}
};
std::string row(unsigned last, unsigned interval = 1, unsigned requests = 2) {
  char text[80];
  std::snprintf(text, sizeof(text), "0102030405%02X %u %u\n", last, interval, requests);
  return text;
}

void initialAvailabilityAndGates() {
  Poll poll; Fake f; f.available = false; f.config = row(7);
  Key key(7);
  assert(poll.refresh(0, f.host()) == Poll::Status::StorageUnavailable);
  assert(!poll.loaded() && f.reads == 0);
  f.available = true;
  assert(poll.refresh(100, f.host()) == Poll::Status::Ready);
  assert(poll.loaded() && poll.interval(key.data) == 1 && f.reads == 1);
  assert(!poll.tick(5099, false, 400, f.host()).consumed);
  assert(!poll.tick(5100, true, 400, f.host()).consumed);
  assert(!poll.tick(5100, false, 299, f.host()).consumed);
  f.sendResult = false;
  const Poll::Outcome due = poll.tick(5100, false, 300, f.host());
  assert(due.consumed && !due.sent && due.persisted && poll.requestsLeft(key.data) == 1);
  assert(f.sends == 1 && f.writes == 1);
  assert(!poll.tick(35100 - 1, false, 300, f.host()).consumed);
  assert(!poll.tick(35100, false, 300, f.host()).consumed); // next node deadline is later
  assert(poll.tick(65100, false, 300, f.host()).consumed);
  assert(poll.interval(key.data) == 0 && poll.requestsLeft(key.data) == 10);
}

void fairnessAndCapacity() {
  Poll poll; Fake f;
  for (unsigned i = 0; i < 8; ++i) f.config += row(i + 1, 1, 1);
  assert(poll.refresh(0, f.host()) == Poll::Status::Ready);
  Key ninth(9);
  const Poll::Outcome full = poll.set(ninth.data, 1, 1, 0, f.host());
  assert(!full.accepted && full.status == Poll::Status::Full);
  Key first(1);
  assert(poll.set(first.data, 1, 1, 0, f.host()).accepted);
  assert(poll.interval(first.data) == 1);
  for (unsigned i = 0; i < 8; ++i) {
    const Poll::Outcome done = poll.tick(5000 + i * 30000, false, 300, f.host());
    assert(done.consumed && done.sent);
    assert(f.sent.back() == i + 1);
  }
  assert(f.sends == 8);
  assert(!poll.tick(245000, false, 300, f.host()).consumed);
}

void wrapAndZeroDeadlines() {
  Poll poll; Fake f; f.missing = true;
  Key key(1);
  const uint32_t setTime = UINT32_MAX - 2999u;
  assert(poll.set(key.data, 1, 1, setTime, f.host()).accepted);
  assert(!poll.tick(UINT32_MAX, false, 300, f.host()).consumed);
  assert(poll.tick(0, false, 300, f.host()).consumed);
  assert(!poll.tick(29999, false, 300, f.host()).consumed);
  Key second(2);
  poll.set(second.data, 1, 1, 30000, f.host());
  assert(!poll.tick(32999, false, 300, f.host()).consumed);
  assert(poll.tick(33000, false, 300, f.host()).consumed);
}

void parsingAndMigration() {
  Poll poll; Fake f;
  f.config = "GG0203040501 1 3\n";
  f.config += std::string(90, 'x') + "\n";
  f.config += "010203040501 2\n"; // legacy defaults to ten requests
  f.config += "010203040501 99 8\n"; // duplicate: first lookup wins
  f.config += "010203040502 999999999999999999999999 99999999999999999999999\n";
  assert(poll.refresh(0, f.host()) == Poll::Status::Ready);
  Key first(1), second(2), bad(0);
  assert(poll.interval(first.data) == 2 && poll.requestsLeft(first.data) == 10);
  assert(poll.interval(second.data) == 1440 && poll.requestsLeft(second.data) == 9999);
  assert(poll.interval(bad.data) == 0);
  assert(f.writes == 1); // intentional migration of legacy, clamps, duplicate
  assert(f.config == row(1, 2, 10) + row(2, 1440, 9999));
}

void offlineMergeAndConflict() {
  Poll poll; Fake f; f.config = row(1) + row(2); f.available = false;
  Key one(1), two(2), three(3);
  assert(poll.set(one.data, 7, 4, 100, f.host()).accepted);
  assert(poll.set(two.data, 0, 1, 101, f.host()).accepted);
  assert(poll.set(three.data, 9, 2, 102, f.host()).accepted);
  assert(poll.interval(one.data) == 7 && poll.interval(two.data) == 0);
  f.available = true;
  assert(poll.refresh(200, f.host()) == Poll::Status::Ready);
  assert(poll.loaded() && !poll.pendingPersistence());
  assert(poll.interval(one.data) == 7 && poll.interval(two.data) == 0 && poll.interval(three.data) == 9);
  assert(f.config.find("010203040502") == std::string::npos);

  Poll conflict; Fake full; full.available = false;
  for (unsigned i = 1; i <= 8; ++i) full.config += row(i);
  Key extra(9);
  assert(conflict.set(extra.data, 1, 2, 0, full.host()).accepted);
  full.available = true;
  assert(conflict.refresh(100, full.host()) == Poll::Status::MergeConflict);
  assert(!conflict.loaded() && full.writes == 0 && conflict.interval(extra.data) == 1);
  assert(!conflict.tick(5100, false, 300, full.host()).consumed);
  Key remove(1);
  full.available = false;
  assert(conflict.set(remove.data, 0, 1, 5200, full.host()).accepted);
  full.available = true;
  assert(conflict.refresh(10200, full.host()) == Poll::Status::Ready);
  assert(conflict.loaded() && conflict.interval(extra.data) == 1 && conflict.interval(remove.data) == 0);
}

void readAndWriteFailures() {
  Poll poll; Fake f; f.readError = true;
  assert(poll.refresh(0, f.host()) == Poll::Status::ReadError);
  assert(poll.refresh(100, f.host()) == Poll::Status::ReadError && f.reads == 1);
  f.readError = false; f.missing = true;
  assert(poll.refresh(5000, f.host()) == Poll::Status::Ready && f.reads == 2);

  Key key(4);
  f.writeFailure = true;
  const Poll::Outcome edit = poll.set(key.data, 1, 1, 6000, f.host());
  assert(edit.accepted && !edit.persisted && edit.status == Poll::Status::WriteFailed);
  assert(poll.pendingPersistence() && f.writes == 1);
  assert(poll.refresh(6100, f.host()) == Poll::Status::WriteFailed && f.writes == 1);
  f.writeFailure = false;
  assert(poll.refresh(11000, f.host()) == Poll::Status::Ready && f.writes == 2);
  assert(!poll.pendingPersistence());

  f.writeFailure = true;
  const Poll::Outcome due = poll.tick(11000, false, 300, f.host());
  assert(due.consumed && due.sent && !due.persisted && due.status == Poll::Status::WriteFailed);
  assert(poll.interval(key.data) == 0 && poll.pendingPersistence());

  Poll tooLarge; Fake huge; huge.tooLarge = true;
  assert(tooLarge.refresh(0, huge.host()) == Poll::Status::TooLarge);
  assert(tooLarge.refresh(100, huge.host()) == Poll::Status::TooLarge && huge.reads == 1);
}

void synchronousSendReentry() {
  Poll poll; Fake f; f.config = row(1, 1, 1);
  Key key(1);
  assert(poll.refresh(0, f.host()) == Poll::Status::Ready);
  f.reenter = &poll;
  f.reenterAt = 5000;
  const Poll::Outcome outer = poll.tick(5000, false, 300, f.host());
  assert(outer.consumed && outer.sent);
  assert(!f.nestedConsumed && f.replacementAccepted && f.sends == 1);
  assert(poll.interval(key.data) == 2 && poll.requestsLeft(key.data) == 5);
  assert(f.config == row(1, 2, 5));
  assert(!poll.tick(7999, false, 300, f.host()).consumed);
  assert(poll.tick(35000, false, 300, f.host()).consumed);
}

void pendingCapacity() {
  Poll poll; Fake f; f.available = false;
  for (unsigned i = 1; i <= 8; ++i) {
    Key key{uint8_t(i)};
    assert(poll.set(key.data, 1, 1, i, f.host()).accepted);
  }
  Key ninth(9), first(1);
  assert(poll.set(ninth.data, 1, 1, 9, f.host()).status == Poll::Status::Full);
  assert(poll.interval(ninth.data) == 0);
  assert(poll.set(first.data, 2, 3, 10, f.host()).accepted);
  assert(poll.interval(first.data) == 2);
}

void emptyPlanRoundTrip() {
  Poll poll; Fake f; f.config = row(1);
  Key key(1);
  assert(poll.refresh(0, f.host()) == Poll::Status::Ready);
  const Poll::Outcome disabled = poll.set(key.data, 0, 1, 100, f.host());
  assert(disabled.accepted && disabled.persisted && f.config.empty());
  // StagedFileInstall requires a nonempty body; the SD adapter writes a
  // newline for this empty serialization. The parser accepts that form.
  f.config = "\n";
  Poll reloaded;
  assert(reloaded.refresh(200, f.host()) == Poll::Status::Ready);
  assert(reloaded.interval(key.data) == 0 && reloaded.requestsLeft(key.data) == 10);
}

void telemetryPollingRegression() {
  initialAvailabilityAndGates();
  fairnessAndCapacity();
  wrapAndZeroDeadlines();
  parsingAndMigration();
  offlineMergeAndConflict();
  readAndWriteFailures();
  synchronousSendReentry();
  pendingCapacity();
  emptyPlanRoundTrip();
  std::puts("telemetry polling: PASS");
}

#ifdef TELEMETRY_POLLING_STANDALONE
int main() { telemetryPollingRegression(); }
#endif

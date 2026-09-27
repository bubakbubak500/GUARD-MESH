// SPDX-License-Identifier: GPL-3.0-or-later
#include "BatteryHistory.h"
#include "../platform/UiPlatform.h"
#include <FS.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace ui {
namespace {
struct Paths {
  char log[64], temporary[64], backup[64];
  explicit Paths(const BatteryHistory::Backend &backend) {
    snprintf(log, sizeof log, "%s/battery.log", backend.root);
    snprintf(temporary, sizeof temporary, "%s/battery.tmp", backend.root);
    snprintf(backup, sizeof backup, "%s/battery.bak", backend.root);
  }
};
enum class Line { End, Read, Error };
Line readLine(File &file, char (&text)[128]) {
  size_t used = 0;
  const size_t end = file.size();
  for (size_t position = file.position(); position < end; ++position) {
    const int value = file.read();
    if (value < 0)
      return Line::Error;
    if (value == '\n') {
      text[used] = 0;
      return Line::Read;
    }
    if (used == sizeof text - 1 || value == 0)
      return Line::Error;
    text[used++] = static_cast<char>(value);
  }
  text[used] = 0;
  return used ? Line::Read : Line::End;
}
bool retained(uint32_t epoch, uint32_t now) {
  return !epoch || now <= epoch || now - epoch <= battery::HistorySeconds;
}
uint32_t checksum(uint32_t value, const uint8_t *bytes, size_t size) {
  while (size--) {
    value ^= *bytes++;
    value *= 16777619u;
  }
  return value;
}
} // namespace
BatteryHistory::Result BatteryHistory::finish(Result result, const Backend &backend) {
  _last = result;
  if (_host.failure &&
      (result == Result::ReadFailed || result == Result::WriteFailed || result == Result::CommitFailed))
    _host.failure(_host.context, backend);
  return result;
}
void BatteryHistory::tick(uint32_t now) {
  if (!due(now))
    return;
  _scheduled = true;
  _next = now + 5u * 60u * 1000u;
  if (_host.sample) {
    const auto sample = _host.sample(_host.context);
    if (sample.millivolts)
      append(sample);
  }
}
BatteryHistory::Result BatteryHistory::append(const battery::Sample &sample) {
  const auto backend = _host.resolve ? _host.resolve(_host.context) : Backend{};
  if (!backend.filesystem)
    return finish(Result::NoStorage, backend);
  if (!sample.millivolts || sample.percent < 0 || sample.percent > 100)
    return finish(Result::InvalidData, backend);
  auto &fs = *backend.filesystem;
  const Paths paths(backend);
  if (fs.exists(paths.temporary) || fs.exists(paths.backup))
    return finish(Result::RecoveryRequired, backend);
  const bool original = fs.exists(paths.log);
  File input = fs.open(paths.log, FILE_READ);
  if (original && (!input || input.isDirectory()))
    return finish(Result::ReadFailed, backend);
  size_t kept = 0;
  char line[128];
  Line read = Line::End;
  battery::Sample parsed{};
  if (input) {
    while ((read = readLine(input, line)) == Line::Read) {
      if (!line[0])
        continue;
      if (!battery::parseSample(line, parsed))
        return finish(Result::InvalidData, backend);
      if (retained(parsed.epoch, sample.epoch))
        ++kept;
    }
    if (read == Line::Error || !input.seek(0))
      return finish(Result::ReadFailed, backend);
  }
  File output = fs.open(paths.temporary, FILE_WRITE);
  if (!output)
    return finish(Result::WriteFailed, backend);
  auto discard = [&](Result result) {
    output.close();
    input.close();
    if (!fs.remove(paths.temporary))
      return finish(Result::RecoveryRequired, backend);
    return finish(result, backend);
  };
  size_t bytes = 0;
  uint32_t digest = 2166136261u;
  auto write = [&](const char *text, size_t size) {
    if (output.write(reinterpret_cast<const uint8_t *>(text), size) != size)
      return false;
    bytes += size;
    digest = checksum(digest, reinterpret_cast<const uint8_t *>(text), size);
    return true;
  };
  size_t skip = kept >= battery::HistoryCapacity ? kept - (battery::HistoryCapacity - 1) : 0;
  if (input) {
    while ((read = readLine(input, line)) == Line::Read) {
      if (!line[0])
        continue;
      if (!battery::parseSample(line, parsed))
        return discard(Result::InvalidData);
      if (!retained(parsed.epoch, sample.epoch))
        continue;
      if (skip) {
        --skip;
        continue;
      }
      if (!write(line, strlen(line)) || !write("\n", 1))
        return discard(Result::WriteFailed);
    }
    if (read == Line::Error)
      return discard(Result::ReadFailed);
  }
  input.close();
  char when[20] = "----------------";
  tm local{};
  if (sample.epoch > 1700000000u && platform::localTime(sample.epoch, local))
    strftime(when, sizeof when, "%Y-%m-%d %H:%M", &local);
  const int length =
      snprintf(line, sizeof line, "%lu\t%s\t%u\t%d\t%u\n", static_cast<unsigned long>(sample.epoch), when,
               sample.millivolts, sample.percent, sample.cpuMHz);
  if (length <= 0 || static_cast<size_t>(length) >= sizeof line || !write(line, length))
    return discard(Result::WriteFailed);
  output.flush();
  output.close();
  File verify = fs.open(paths.temporary, FILE_READ);
  uint32_t observed = 2166136261u;
  bool valid = verify && verify.size() == bytes;
  uint8_t buffer[128];
  for (size_t offset = 0; valid && offset < bytes;) {
    const size_t count = std::min(sizeof buffer, bytes - offset);
    valid = verify.read(buffer, count) == count;
    if (valid)
      observed = checksum(observed, buffer, count);
    offset += count;
  }
  verify.close();
  if (!valid || observed != digest)
    return discard(Result::WriteFailed);
  if (original && !fs.rename(paths.log, paths.backup))
    return discard(Result::CommitFailed);
  if (!fs.rename(paths.temporary, paths.log)) {
    if (original && !fs.rename(paths.backup, paths.log))
      return finish(Result::RecoveryRequired, backend);
    return discard(Result::CommitFailed);
  }
  if (original && !fs.remove(paths.backup))
    return finish(Result::RecoveryRequired, backend);
  return finish(Result::Ok, backend);
}
BatteryHistory::Result BatteryHistory::load(battery::Sample *samples, size_t capacity, size_t &count) {
  return load(resolve(), samples, capacity, count);
}
BatteryHistory::Result BatteryHistory::load(const Backend &backend, battery::Sample *samples, size_t capacity,
                                            size_t &count) {
  count = 0;
  if (!backend.filesystem)
    return finish(Result::NoStorage, backend);
  if (!samples || !capacity)
    return finish(Result::InvalidData, backend);
  auto &fs = *backend.filesystem;
  const Paths paths(backend);
  File input = fs.open(paths.log, FILE_READ);
  if (!input)
    return finish(fs.exists(paths.log)      ? Result::ReadFailed
                  : fs.exists(paths.backup) ? Result::RecoveryRequired
                                            : Result::Ok,
                  backend);
  if (input.isDirectory())
    return finish(Result::ReadFailed, backend);
  char line[128];
  size_t next = 0;
  Line read;
  while ((read = readLine(input, line)) == Line::Read) {
    if (!line[0])
      continue;
    battery::Sample sample{};
    if (!battery::parseSample(line, sample)) {
      count = 0;
      return finish(Result::InvalidData, backend);
    }
    samples[next] = sample;
    next = (next + 1) % capacity;
    if (count < capacity)
      ++count;
  }
  if (read == Line::Error) {
    count = 0;
    return finish(Result::ReadFailed, backend);
  }
  if (count == capacity)
    std::rotate(samples, samples + next, samples + count);
  return finish(Result::Ok, backend);
}
BatteryHistory::Result BatteryHistory::clear() { return clear(resolve()); }
BatteryHistory::Result BatteryHistory::clear(const Backend &backend) {
  if (!backend.filesystem)
    return finish(Result::NoStorage, backend);
  auto &fs = *backend.filesystem;
  const Paths paths(backend);
  if (fs.exists(paths.backup) || fs.exists(paths.temporary))
    return finish(Result::RecoveryRequired, backend);
  return finish(!fs.exists(paths.log) || fs.remove(paths.log) ? Result::Ok : Result::WriteFailed, backend);
}
} // namespace ui

// SPDX-License-Identifier: GPL-3.0-or-later
#include "TelemetryPolling.h"
#include <cstdio>
#include <cstring>

namespace ui {
namespace {

bool due(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

bool number(const char *line, size_t length, size_t &position,
            uint32_t limit, uint16_t &value, bool &clamped) {
  if (position == length || line[position] < '0' || line[position] > '9') return false;
  uint32_t parsed = 0;
  while (position < length && line[position] >= '0' && line[position] <= '9') {
    const uint32_t digit = uint32_t(line[position++] - '0');
    if (parsed > limit || parsed > (limit - digit) / 10u) {
      parsed = limit + 1u;
    } else {
      parsed = parsed * 10u + digit;
    }
  }
  if (parsed == 0) return false;
  if (parsed > limit) { parsed = limit; clamped = true; }
  value = uint16_t(parsed);
  return true;
}

struct ParsedLine {
  uint8_t key[6];
  uint16_t interval;
  uint16_t requests;
  bool migrated;
};

bool parseLine(const char *line, size_t length, ParsedLine &parsed) {
  while (length && space(*line)) { ++line; --length; }
  while (length && space(line[length - 1])) --length;
  if (length < 14 || length > 63) return false;
  for (size_t i = 0; i < 6; ++i) {
    const int high = hexDigit(line[i * 2]);
    const int low = hexDigit(line[i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    parsed.key[i] = uint8_t((high << 4) | low);
  }
  size_t position = 12;
  if (!space(line[position])) return false;
  while (position < length && space(line[position])) ++position;
  parsed.migrated = false;
  if (!number(line, length, position, TelemetryPolling::MaxIntervalMinutes,
              parsed.interval, parsed.migrated)) return false;
  if (position == length) {
    parsed.requests = TelemetryPolling::DefaultRequests;
    parsed.migrated = true;
    return true;
  }
  if (!space(line[position])) return false;
  while (position < length && space(line[position])) ++position;
  if (!number(line, length, position, TelemetryPolling::MaxRequests,
              parsed.requests, parsed.migrated)) return false;
  return position == length;
}

bool sameKey(const uint8_t a[6], const uint8_t b[6]) {
  return std::memcmp(a, b, 6) == 0;
}

} // namespace

int TelemetryPolling::find(const Record records[Capacity], const uint8_t key[6]) {
  for (size_t i = 0; i < Capacity; ++i)
    if (records[i].used && sameKey(records[i].key, key)) return int(i);
  return -1;
}

int TelemetryPolling::vacant(const Record records[Capacity]) {
  for (size_t i = 0; i < Capacity; ++i) if (!records[i].used) return int(i);
  return -1;
}

void TelemetryPolling::backoff(uint32_t nowMs, Status reason) {
  _retryAt = nowMs + 5000u;
  _retryArmed = true;
  _retryReason = reason;
}

TelemetryPolling::Status TelemetryPolling::save(uint32_t nowMs, const Host &host, bool force) {
  if (!_dirty) return Status::Ready;
  if (!host.storageAvailable || !host.writeConfig) return Status::Invalid;
  if (!host.storageAvailable(host.context)) return Status::StorageUnavailable;
  if (_retryArmed && !force && !due(nowMs, _retryAt)) return _retryReason;
  char output[256] = {};
  size_t length = 0;
  for (size_t i = 0; i < Capacity; ++i) {
    const Record &record = _records[i];
    if (!record.used) continue;
    const int count = std::snprintf(output + length, sizeof(output) - length,
        "%02X%02X%02X%02X%02X%02X %u %u\n",
        unsigned(record.key[0]), unsigned(record.key[1]), unsigned(record.key[2]),
        unsigned(record.key[3]), unsigned(record.key[4]), unsigned(record.key[5]),
        unsigned(record.intervalMinutes), unsigned(record.requestsLeft));
    if (count < 0 || size_t(count) >= sizeof(output) - length) {
      backoff(nowMs, Status::TooLarge);
      return Status::TooLarge;
    }
    length += size_t(count);
  }
  if (!host.writeConfig(host.context, output, length)) {
    backoff(nowMs, Status::WriteFailed);
    return Status::WriteFailed;
  }
  _dirty = false;
  _retryArmed = false;
  _retryReason = Status::Ready;
  return Status::Ready;
}

TelemetryPolling::Status TelemetryPolling::refresh(uint32_t nowMs, const Host &host) {
  if (!host.storageAvailable || !host.readConfig || !host.writeConfig) return Status::Invalid;
  if (!host.storageAvailable(host.context)) return Status::StorageUnavailable;
  if (_loaded) return save(nowMs, host, false);
  if (_retryArmed && !due(nowMs, _retryAt)) return _retryReason;

  char input[ConfigCapacity];
  size_t length = 0;
  const ReadStatus read = host.readConfig(host.context, input, sizeof(input), &length);
  if (read == ReadStatus::Error) { backoff(nowMs, Status::ReadError); return Status::ReadError; }
  if (read == ReadStatus::TooLarge || length > sizeof(input)) {
    backoff(nowMs, Status::TooLarge);
    return Status::TooLarge;
  }
  Record parsed[Capacity] = {};
  bool migrated = false;
  if (read == ReadStatus::Ok) {
    size_t start = 0;
    while (start < length) {
      size_t end = start;
      while (end < length && input[end] != '\n') ++end;
      ParsedLine line = {};
      if (parseLine(input + start, end - start, line)) {
        if (find(parsed, line.key) >= 0) {
          migrated = true; // keep the first row, which old lookups returned
        } else {
          const int slot = vacant(parsed);
          if (slot < 0) { backoff(nowMs, Status::TooLarge); return Status::TooLarge; }
          Record &record = parsed[slot];
          std::memcpy(record.key, line.key, 6);
          record.intervalMinutes = line.interval;
          record.requestsLeft = line.requests;
          record.nextMs = nowMs + 5000u;
          record.used = true;
          migrated = migrated || line.migrated;
        }
      }
      start = end + 1;
    }
  }
  // User edits made before the first successful load win. Apply deletions
  // first so a full file can still replace one of its own records.
  for (size_t i = 0; i < Capacity; ++i) {
    const Edit &edit = _pending[i];
    if (!edit.used || edit.intervalMinutes != 0) continue;
    const int old = find(parsed, edit.key);
    if (old >= 0) parsed[old].used = false;
  }
  for (size_t i = 0; i < Capacity; ++i) {
    const Edit &edit = _pending[i];
    if (!edit.used || edit.intervalMinutes == 0) continue;
    int slot = find(parsed, edit.key);
    if (slot < 0) slot = vacant(parsed);
    if (slot < 0) { backoff(nowMs, Status::MergeConflict); return Status::MergeConflict; }
    Record &record = parsed[slot];
    std::memcpy(record.key, edit.key, 6);
    record.intervalMinutes = edit.intervalMinutes;
    record.requestsLeft = edit.requests;
    record.nextMs = edit.nextMs;
    record.used = true;
  }

  for (size_t i = 0; i < Capacity; ++i) _records[i] = parsed[i];
  const bool hadPending = _pendingCount != 0;
  for (size_t i = 0; i < Capacity; ++i) _pending[i] = Edit();
  _pendingCount = 0;
  _loaded = true;
  _dirty = migrated || hadPending;
  _retryArmed = false;
  _retryReason = Status::Ready;
  return save(nowMs, host, true);
}

TelemetryPolling::Outcome TelemetryPolling::set(
    const uint8_t key[6], int intervalMinutes, int requests, uint32_t nowMs, const Host &host) {
  Outcome result;
  if (!key || intervalMinutes < 0) { result.status = Status::Invalid; return result; }
  if (intervalMinutes > MaxIntervalMinutes) intervalMinutes = MaxIntervalMinutes;
  if (requests <= 0) requests = DefaultRequests;
  if (requests > MaxRequests) requests = MaxRequests;
  result.status = refresh(nowMs, host);
  if (!_loaded) {
    int slot = -1;
    for (size_t i = 0; i < Capacity; ++i)
      if (_pending[i].used && sameKey(_pending[i].key, key)) { slot = int(i); break; }
    if (slot < 0)
      for (size_t i = 0; i < Capacity; ++i)
        if (!_pending[i].used) { slot = int(i); break; }
    if (slot < 0) { result.status = Status::Full; return result; }
    Edit &edit = _pending[slot];
    if (!edit.used) ++_pendingCount;
    std::memcpy(edit.key, key, 6);
    edit.intervalMinutes = uint16_t(intervalMinutes);
    edit.requests = uint16_t(requests);
    edit.nextMs = nowMs + 3000u;
    edit.used = true;
    result.accepted = true;
    return result;
  }
  int slot = find(_records, key);
  if (intervalMinutes > 0 && slot < 0) slot = vacant(_records);
  if (intervalMinutes > 0 && slot < 0) { result.status = Status::Full; return result; }
  if (intervalMinutes == 0 && slot < 0) {
    result.accepted = true;
    result.persisted = !_dirty;
    return result;
  }
  Record &record = _records[slot];
  if (intervalMinutes == 0) {
    record.used = false;
  } else {
    std::memcpy(record.key, key, 6);
    record.intervalMinutes = uint16_t(intervalMinutes);
    record.requestsLeft = uint16_t(requests);
    record.nextMs = nowMs + 3000u;
    record.used = true;
  }
  _dirty = true;
  result.accepted = true;
  result.status = save(nowMs, host, true);
  result.persisted = !_dirty;
  return result;
}

TelemetryPolling::Outcome TelemetryPolling::tick(
    uint32_t nowMs, bool manualPending, int txBudget, const Host &host) {
  Outcome result;
  result.status = refresh(nowMs, host);
  result.persisted = !pendingPersistence();
  if (!_loaded || result.status == Status::StorageUnavailable || result.status == Status::Invalid ||
      manualPending || txBudget < 300 || (_gapArmed && !due(nowMs, _gapUntil))) return result;
  for (size_t step = 0; step < Capacity; ++step) {
    const size_t index = (_cursor + step) % Capacity;
    Record &record = _records[index];
    if (!record.used || !due(nowMs, record.nextMs)) continue;
    uint8_t key[6];
    std::memcpy(key, record.key, sizeof(key));
    record.nextMs = nowMs + uint32_t(record.intervalMinutes) * 60000u;
    --record.requestsLeft;
    if (record.requestsLeft == 0) record.used = false;
    _gapUntil = nowMs + 30000u;
    _gapArmed = true;
    _cursor = uint8_t((index + 1) % Capacity);
    _dirty = true;
    result.consumed = true;
    // A host send may synchronously reenter set()/tick(). Publish the spent
    // slot and gap first; pass an owned key so a replacement cannot retarget
    // the in-flight send. Save the latest state after the callback returns.
    result.sent = host.send && host.send(host.context, key);
    result.status = save(nowMs, host, true);
    result.persisted = !_dirty;
    break;
  }
  return result;
}

int TelemetryPolling::interval(const uint8_t key[6]) const {
  if (!key) return 0;
  for (size_t i = 0; i < Capacity; ++i)
    if (_pending[i].used && sameKey(_pending[i].key, key)) return _pending[i].intervalMinutes;
  const int index = find(_records, key);
  return index < 0 ? 0 : _records[index].intervalMinutes;
}

int TelemetryPolling::requestsLeft(const uint8_t key[6]) const {
  if (!key) return DefaultRequests;
  for (size_t i = 0; i < Capacity; ++i)
    if (_pending[i].used && sameKey(_pending[i].key, key))
      return _pending[i].intervalMinutes ? _pending[i].requests : DefaultRequests;
  const int index = find(_records, key);
  return index < 0 ? DefaultRequests : _records[index].requestsLeft;
}

} // namespace ui

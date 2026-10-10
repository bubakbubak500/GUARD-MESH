// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(LILYGO_TDECK) && defined(ESP32)
#include <Arduino.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>
#include "GnssPowerControl.h"

// Observe the existing NMEA provider's reads rather than introducing a second
// consumer on Serial1. Binary identity/ACK frames are checksummed separately;
// only ASCII sentences are forwarded to MicroNMEA's line-oriented parser.
class TDeckGpsStream : public Stream {
public:
  explicit TDeckGpsStream(HardwareSerial &serial)
      : _serial(serial), _power(send, &serial) {}
  int available() override { return _budget ? _serial.available() : 0; }
  int peek() override { return _serial.peek(); }
  int read() override {
    if (!_budget) return -1;
    const int value = _serial.read();
    if (value >= 0) {
      --_budget;
      const uint32_t now = millis();
      _power.tick(now);
      const bool binary = _power.binaryFrame() || value == 0xb5;
      _power.feed(static_cast<uint8_t>(value), now);
      if (binary) {
        _sentence = false;
        return '\n';
      }
      if (value == '$') _sentence = true;
      else if (value == '\r' || value == '\n' || value < 0x20 || value > 0x7e)
        _sentence = false;
      if (!_sentence) return '\n';
    }
    return value;
  }
  void flush() override { _serial.flush(); }
  size_t write(uint8_t value) override { return _serial.write(value); }
  using Print::write;
  void budget(unsigned bytes) { _budget = bytes; }
  void discardBuffered() {
    // Discard only the already queued RX snapshot; never wait for new bytes.
    // Serial1's largest configured RX ring is 4096 bytes. Do not feed a stale
    // save ACK while beginning a new acquisition.
    const int queued = _serial.available();
    const unsigned count = queued > 4096 ? 4096 : queued > 0 ? static_cast<unsigned>(queued) : 0;
    for (unsigned i = 0; i < count; ++i) if (_serial.read() < 0) break;
    _sentence = false;
  }
  gnss::PowerControl &power() { return _power; }
  uint32_t baud() const { return _serial.baudRate(); }

private:
  static size_t send(void *context, const uint8_t *data, size_t size) {
    auto &serial = *static_cast<HardwareSerial *>(context);
    const size_t sent = serial.write(data, size);
    // At most 24 bytes at the selected baud. Complete the wake byte before a
    // GPS-on caller may reopen Serial1 to enlarge its RX ring.
    serial.flush();
    return sent;
  }
  HardwareSerial &_serial;
  gnss::PowerControl _power;
  unsigned _budget = 0;
  bool _sentence = false;
};

class TDeckLocationProvider : public MicroNMEALocationProvider {
public:
  TDeckLocationProvider(TDeckGpsStream &stream, mesh::RTCClock *clock)
      : MicroNMEALocationProvider(stream, clock), _stream(stream) {}
  void begin() override {
    if (!_active) {
      _stream.discardBuffered();
      MicroNMEALocationProvider::begin();
      // Never expose a fix buffered before standby as a fresh position.
      MicroNMEALocationProvider::syncTime();
    }
    _active = true;
    noteBaud();
    _stream.power().request(true, millis());
  }
  void stop() override {
    if (_active) MicroNMEALocationProvider::stop();
    _active = false;
    MicroNMEALocationProvider::syncTime();
    noteBaud();
    _stream.power().request(false, millis());
  }
  bool isEnabled() override { return _active; }
  void loop() override {
    if (!_active) return;
    _stream.budget(1024);
    MicroNMEALocationProvider::loop();
  }
  // EnvironmentSensorManager stops calling the provider when GPS is disabled.
  // Continue a bounded handshake there, without updating position or RTC.
  void service() {
    noteBaud();
    if (!_active) {
      _stream.budget(1024);
      while (_stream.available() > 0) {
        if (_stream.read() < 0) break;
      }
    }
    _stream.power().tick(millis());
  }
  const gnss::PowerControl &power() const { return _stream.power(); }

private:
  void noteBaud() {
    const uint32_t baud = _stream.baud();
    if (baud == _baud) return;
    // A user-selected UART speed invalidates the old receiver identity. Avoid
    // sending a model-specific command on a newly configured connection.
    if (_baud) _stream.power().resetIdentity(millis());
    _baud = baud;
  }
  TDeckGpsStream &_stream;
  uint32_t _baud = 0;
  bool _active = false;
};
#endif

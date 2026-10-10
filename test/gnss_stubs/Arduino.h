// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>
extern uint32_t gnssTestNow;
inline uint32_t millis() { return gnssTestNow; }
inline void delay(unsigned ms) { gnssTestNow += ms; }
enum { HIGH = 1, LOW = 0, OUTPUT = 1 };
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return HIGH; }
class Print {
public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t *p, size_t size) {
    size_t sent = 0;
    while (sent < size && write(p[sent]) == 1) ++sent;
    return sent;
  }
  size_t print(const char *text) {
    return write(reinterpret_cast<const uint8_t *>(text), strlen(text));
  }
  size_t print(char c) { return write(static_cast<uint8_t>(c)); }
};
class Stream : public Print {
public:
  virtual int available() = 0;
  virtual int peek() = 0;
  virtual int read() = 0;
  virtual void flush() = 0;
};
class HardwareSerial : public Stream {
public:
  std::deque<uint8_t> incoming;
  std::vector<std::vector<uint8_t>> writes;
  uint32_t speed = 38400;
  int available() override { return static_cast<int>(incoming.size()); }
  int peek() override { return incoming.empty() ? -1 : incoming.front(); }
  int read() override {
    if (incoming.empty()) return -1;
    const auto value = incoming.front();
    incoming.pop_front();
    return value;
  }
  void flush() override {}
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t *data, size_t size) override {
    writes.emplace_back(data, data + size);
    return size;
  }
  uint32_t baudRate() const { return speed; }
};

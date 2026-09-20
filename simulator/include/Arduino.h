// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <chrono>
#include <thread>
using std::max;
using std::min;
class String : public std::string {
public:
  using std::string::string;
  String(const std::string &s) : std::string(s) {}
  int indexOf(char c, size_t start = 0) const {
    auto p = find(c, start);
    return p == npos ? -1 : (int)p;
  }
  String substring(size_t start, size_t end = npos) const {
    return start > size() ? String() : String(substr(start, end == npos ? npos : end - start));
  }
  void trim() {
    auto a = find_first_not_of(" \t\r\n");
    if (a == npos)
      clear();
    else
      *this = substr(a, find_last_not_of(" \t\r\n") - a + 1);
  }
  long toInt() const { return strtol(c_str(), nullptr, 10); }
  float toFloat() const { return strtof(c_str(), nullptr); }
};
class Print {
public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t *, size_t) = 0;
};
#define PROGMEM
#define IRAM_ATTR
#define HIGH 1
#define LOW 0
#define INPUT_PULLUP 2
#define OUTPUT 1
#define F(x) x
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_word(p) (*(const uint16_t *)(p))
inline uint64_t simMicros() {
  static auto start = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                               start)
      .count();
}
inline unsigned long millis() {
  return (unsigned long)(simMicros() / 1000);
}
inline unsigned long micros() {
  return (unsigned long)simMicros();
}
inline void delay(unsigned long ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
inline void pinMode(int, int) {}
int simDigitalRead(int pin);
inline int digitalRead(int pin) {
  return simDigitalRead(pin);
}
inline void digitalWrite(int, int) {}
inline long random(long n) {
  return n ? std::rand() % n : 0;
}
inline long random(long a, long b) {
  return a + random(b - a);
}
template <class T> T constrain(T x, T a, T b) {
  return std::clamp(x, a, b);
}
struct SimSerial {
  template <class... A> void printf(const char *f, A... a) { std::printf(f, a...); }
  void println(const char *s = "") { std::puts(s); }
  void print(const char *s) { std::fputs(s, stdout); }
  void flush() {}
};
inline SimSerial Serial;

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Arduino.h"
class Stream {
public:
  virtual ~Stream() = default;
  virtual size_t write(const uint8_t *, size_t) { return 0; }
  virtual int read() { return -1; }
  virtual int available() { return 0; }
  virtual int peek() { return -1; }
};

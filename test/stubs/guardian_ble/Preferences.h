#pragma once
#include "helpers/esp32/SerialBLEInterface.h"
class Preferences {
public:
  bool begin(const char*,bool) { return true; }
  bool getBool(const char*,bool) { return fake::saved; }
  void putBool(const char*,bool value) { fake::saved=value; }
  void end() {}
};

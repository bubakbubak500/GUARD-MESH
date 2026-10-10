// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <helpers/ui/DisplayDriver.h>
#include <array>
void simBlit(int x, int y, int w, int h, const uint16_t *pixels);
class SimDisplay : public DisplayDriver {
  bool on = true;

public:
  bool backlightOn = true;
  unsigned writes = 0, writesAtLastLight = 0;
  std::array<uint16_t, 320 * 240> pixels{}, pixelsAtLastLight{};
  SimDisplay() : DisplayDriver(320, 240) {}
  bool isOn() override { return on; }
  void turnOn() override { on = true; }
  void turnOff() override { on = false; }
  void clear() override {}
  void startFrame(ColorVal = 0) override {}
  void endFrame() override {}
  void setTextSize(int) override {}
  void setColor(ColorVal) override {}
  void setCursor(int, int) override {}
  void print(const char *) override {}
  void fillRect(int, int, int, int) override {}
  void drawRect(int, int, int, int) override {}
  void drawXbm(int, int, const uint8_t *, int, int) override {}
  uint16_t getTextWidth(const char *s) override { return (uint16_t)strlen(s) * 8; }
  void setDisplayRotation(int) {}
  void setBrightness(int) {}
  void setScreenBacklight(bool enabled) {
    backlightOn = enabled;
    if (enabled) {
      writesAtLastLight = writes;
      pixelsAtLastLight = pixels;
    }
  }
  void writePixelsRGB565(int x, int y, int w, int h, const uint16_t *p) {
    ++writes;
    for (int row = 0; row < h; ++row)
      for (int col = 0; col < w; ++col)
        if (x + col >= 0 && x + col < 320 && y + row >= 0 && y + row < 240)
          pixels[(y + row) * 320 + x + col] = p[row * w + col];
    simBlit(x, y, w, h, p);
  }
};
inline SimDisplay display;

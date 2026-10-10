// SPDX-License-Identifier: GPL-3.0-or-later
#include "helpers/TDeckGpsPower.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <string>
uint32_t gnssTestNow = 100;
namespace {
struct Clock : mesh::RTCClock {
  unsigned writes = 0;
  void setCurrentTime(uint32_t) override { ++writes; }
};
void sentence(HardwareSerial &uart, const char *body) {
  unsigned sum = 0;
  for (const char *p = body; *p; ++p) sum ^= static_cast<unsigned char>(*p);
  char tail[8];
  snprintf(tail, sizeof tail, "*%02X\r\n", sum);
  const std::string line = std::string("$") + body + tail;
  uart.incoming.insert(uart.incoming.end(), line.begin(), line.end());
}
void packet(HardwareSerial &uart, uint8_t cls, uint8_t id,
            const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> bytes{0xb5, 0x62, cls, id,
                           static_cast<uint8_t>(payload.size()),
                           static_cast<uint8_t>(payload.size() >> 8)};
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  uint8_t a = 0, b = 0;
  for (size_t i = 2; i < bytes.size(); ++i) { a += bytes[i]; b += a; }
  bytes.push_back(a); bytes.push_back(b);
  uart.incoming.insert(uart.incoming.end(), bytes.begin(), bytes.end());
}
void identify(HardwareSerial &uart) {
  std::vector<uint8_t> payload(100, 0);
  memcpy(payload.data(), "ROM SPG 5.10", 12);
  memcpy(payload.data() + 30, "000A0000", 8);
  memcpy(payload.data() + 40, "PROTVER=34.10", sizeof("PROTVER=34.10") - 1);
  memcpy(payload.data() + 70, "MOD=MIA-M10Q", sizeof("MOD=MIA-M10Q") - 1);
  packet(uart, 0x0a, 0x04, payload);
}
bool sent(const HardwareSerial &uart, uint8_t cls, uint8_t id) {
  for (const auto &bytes : uart.writes)
    if (bytes.size() > 4 && bytes[0] == 0xb5 && bytes[1] == 0x62 &&
        bytes[2] == cls && bytes[3] == id) return true;
  return false;
}
}
int main() {
  HardwareSerial uart;
  Clock clock;
  TDeckGpsStream stream(uart);
  TDeckLocationProvider gps(stream, &clock);
  assert(uart.writes.empty() && !gps.isEnabled());
  gps.begin();
  // A valid NMEA sentence inside a UBX payload belongs to the binary protocol,
  // and must never manufacture a position or time through MicroNMEA.
  HardwareSerial nested;
  sentence(nested, "GPRMC,123519.000,A,4807.038,N,01131.000,E,022.4,084.4,230326,003.1,W");
  packet(uart, 0x0a, 0x04, std::vector<uint8_t>(nested.incoming.begin(), nested.incoming.end()));
  gps.loop(); gps.service();
  assert(!gps.isValid() && clock.writes == 0 && !gps.power().supported());
  identify(uart);
  sentence(uart, "GPRMC,123519.000,A,4807.038,N,01131.000,E,022.4,084.4,230326,003.1,W");
  sentence(uart, "GPGGA,123519.000,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
  gps.loop(); gps.service();
  assert(uart.incoming.empty() && gps.isEnabled() && gps.isValid());
  assert(gps.getLatitude() == 48117300 && gps.getLongitude() == 11516666);
  assert(gps.satellitesCount() == 8 && gps.power().supported());
  // The existing provider still disciplines RTC after a stable, dated fix.
  for (unsigned i = 0; i < 4; ++i) { gnssTestNow += 1001; gps.loop(); gps.service(); }
  assert(clock.writes == 1);
  gps.stop();
  assert(!gps.isEnabled() && !gps.isValid() && sent(uart, 0x06, 0x09));
  const auto before = uart.writes.size();
  gps.stop();
  assert(uart.writes.size() == before); // repeated settings applications are silent
  packet(uart, 0x05, 0x01, {0x06, 0x09});
  gps.loop();
  assert(!uart.incoming.empty()); // sensor-manager's inactive loop cannot consume ACK
  gps.service();
  assert(uart.incoming.empty() && sent(uart, 0x02, 0x41));
  assert(gps.power().power() == gnss::Power::StandbyRequested);
  sentence(uart, "GPRMC,123519.000,A,4807.038,N,01131.000,E,022.4,084.4,230326,003.1,W");
  gps.service();
  assert(!gps.isValid() && clock.writes == 1); // off-mode drains never update fix or time
  uart.incoming.insert(uart.incoming.end(), 8192, 'x');
  gps.service();
  assert(uart.incoming.size() == 8192 - 1024); // bounded work even with a flooded UART
  uart.incoming.clear();
  sentence(uart, "GPRMC,123519.000,A,4807.038,N,01131.000,E,022.4,084.4,230326,003.1,W");
  gps.begin();
  gps.loop();
  assert(gps.isEnabled() && !gps.isValid() && uart.incoming.empty());
  assert(uart.writes.back() == std::vector<uint8_t>{0xff});
  gnssTestNow += 31; gps.service();
  identify(uart); gps.loop(); gps.service();
  sentence(uart, "GPRMC,123519.000,A,4807.038,N,01131.000,E,022.4,084.4,230326,003.1,W");
  gps.loop();
  assert(gps.isValid() && gps.power().supported());
  uart.speed = 9600; gps.service();
  assert(!gps.power().supported()); // UART reconfiguration cannot reuse an old model proof
  // Cold boot with GPS off: the manager never invokes the active provider loop.
  // Background service must still complete identity/save/standby on its own.
  HardwareSerial offUart;
  Clock offClock;
  TDeckGpsStream offStream(offUart);
  TDeckLocationProvider offGps(offStream, &offClock);
  offGps.stop();
  identify(offUart);
  sentence(offUart, "GPRMC,123519.000,A,4807.038,N,01131.000,E,022.4,084.4,230326,003.1,W");
  offGps.loop();
  assert(!offUart.incoming.empty());
  offGps.service();
  assert(offUart.incoming.empty() && !offGps.isEnabled() && !offGps.isValid());
  assert(offClock.writes == 0 && sent(offUart, 0x06, 0x09));
  packet(offUart, 0x05, 0x01, {0x06, 0x09});
  offGps.service();
  assert(sent(offUart, 0x02, 0x41) && offClock.writes == 0);
  assert(offGps.power().power() == gnss::Power::StandbyRequested);
  puts("T-Deck GPS adapter + production MicroNMEA: PASS");
}

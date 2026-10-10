// SPDX-License-Identifier: GPL-3.0-or-later
#include "helpers/GnssPowerControl.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

using gnss::Power;
using gnss::PowerControl;
using gnss::Receiver;
typedef std::vector<uint8_t> Bytes;

namespace {
const Bytes kPoll = {0xB5, 0x62, 0x0A, 0x04, 0, 0, 0x0E, 0x34};
const Bytes kSave = {0xB5, 0x62, 0x06, 0x09, 0x0D, 0,
    0, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 1, 0x1B, 0xA9};
const Bytes kSleep = {0xB5, 0x62, 0x02, 0x41, 0x10, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0, 8, 0, 0, 0, 0x61, 0x6B};
// Literal SPG 5.10 MON-VER wire-format fixture, not a physical-device capture.
const Bytes kMonVerFixture = {
    0xB5, 0x62, 0x0A, 0x04, 0x64, 0x00, 0x52, 0x4F, 0x4D, 0x20, 0x53, 0x50,
    0x47, 0x20, 0x35, 0x2E, 0x31, 0x30, 0x20, 0x28, 0x37, 0x62, 0x32, 0x30,
    0x32, 0x65, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x30, 0x30, 0x30, 0x41, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x50, 0x52,
    0x4F, 0x54, 0x56, 0x45, 0x52, 0x3D, 0x33, 0x34, 0x2E, 0x31, 0x30, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x4D, 0x4F, 0x44, 0x3D, 0x4D, 0x49, 0x41, 0x2D,
    0x4D, 0x31, 0x30, 0x51, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x67, 0x4E
};
struct UART {
    std::vector<Bytes> writes;
    int shortAt = -1;
    static size_t write(void *context, const uint8_t *data, size_t bytes) {
        UART &uart = *static_cast<UART *>(context);
        uart.writes.push_back(Bytes(data, data + bytes));
        return int(uart.writes.size()) - 1 == uart.shortAt ? bytes - 1 : bytes;
    }
};
Bytes ubx(uint8_t cls, uint8_t id, const Bytes &payload) {
    Bytes bytes = {0xB5, 0x62, cls, id, uint8_t(payload.size()), uint8_t(payload.size() >> 8)};
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    uint8_t a = 0, b = 0;
    for (size_t i = 2; i < bytes.size(); ++i) { a += bytes[i]; b += a; }
    bytes.push_back(a); bytes.push_back(b);
    return bytes;
}
Bytes monver(const char *software = "ROM SPG 5.10 (7b202e)", const char *hardware = "000A0000",
             const char *protocol = "PROTVER=34.10", const char *module = "MOD=MIA-M10Q") {
    Bytes payload(module ? 100 : 70, 0);
    assert(strlen(software) < 30 && strlen(hardware) < 10 && strlen(protocol) < 30);
    memcpy(payload.data(), software, strlen(software));
    memcpy(payload.data() + 30, hardware, strlen(hardware));
    memcpy(payload.data() + 40, protocol, strlen(protocol));
    if (module) memcpy(payload.data() + 70, module, strlen(module));
    return ubx(0x0A, 0x04, payload);
}
Bytes nmea(const std::string &body) {
    uint8_t checksum = 0;
    for (char c : body) checksum ^= uint8_t(c);
    const char *hex = "0123456789ABCDEF";
    std::string text = "$" + body + "*";
    text += hex[checksum >> 4]; text += hex[checksum & 15]; text += "\r\n";
    return Bytes(text.begin(), text.end());
}
void feed(PowerControl &control, const Bytes &bytes, uint32_t now) {
    for (uint8_t byte : bytes) control.feed(byte, now);
}
void detect(PowerControl &control, bool enabled = false, uint32_t now = 0) {
    control.request(enabled, now);
    feed(control, kMonVerFixture, now + 10);
    assert(control.receiver() == Receiver::UbloxM10 && control.supported());
    assert(control.isMiaM10Q());
}
const Bytes kAck = ubx(5, 1, Bytes{6, 9});
const Bytes kNak = ubx(5, 0, Bytes{6, 9});

void exactCommandsAndSleep() {
    UART uart; PowerControl c(UART::write, &uart);
    assert(c.receiver() == Receiver::Unknown && !c.supported() && !c.isMiaM10Q());
    c.tick(5000); feed(c, monver(), 5000);
    assert(uart.writes.empty());
    detect(c);
    assert(c.power() == Power::Saving && uart.writes.size() == 3);
    assert(uart.writes[0] == kPoll);
    assert(uart.writes[1] == nmea("PCAS06,0"));
    assert(uart.writes[2] == kSave);
    assert(kMonVerFixture == monver());
    // These expected packets also independently validate the supplied checksums.
    assert(kSave == ubx(6, 9, Bytes(kSave.begin() + 6, kSave.end() - 2)));
    assert(kSleep == ubx(2, 0x41, Bytes(kSleep.begin() + 6, kSleep.end() - 2)));
    c.request(false, 15); c.tick(20);
    assert(uart.writes.size() == 3);
    feed(c, ubx(5, 1, Bytes{6, 8}), 20);
    feed(c, ubx(5, 1, Bytes{}), 21);
    feed(c, ubx(5, 1, Bytes{6}), 22);
    feed(c, ubx(5, 1, Bytes{6, 9, 0}), 23);
    assert(c.power() == Power::Saving && uart.writes.size() == 3);
    Bytes badAck = kAck; badAck.back() ^= 1;
    feed(c, badAck, 30);
    assert(c.power() == Power::Saving);
    feed(c, kAck, 40);
    assert(c.power() == Power::StandbyRequested && uart.writes.back() == kSleep);
    feed(c, kAck, 50); c.tick(100000); c.request(false, 100001);
    assert(c.power() == Power::StandbyRequested && uart.writes.size() == 4);
}
void saveFailures() {
    for (int mode = 0; mode < 3; ++mode) {
        UART uart; if (mode == 2) uart.shortAt = 2;
        PowerControl c(UART::write, &uart); detect(c);
        if (mode == 0) {
            c.tick(1009); assert(c.power() == Power::Saving);
            c.tick(1010);
        } else if (mode == 1) feed(c, kNak, 20);
        assert(c.power() == Power::SaveFailed && uart.writes.size() == 3);
        feed(c, kAck, 1100); c.tick(10000); c.request(false, 10001);
        assert(c.power() == Power::SaveFailed && uart.writes.size() == 3);
        c.request(true, 11000); c.request(false, 11001);
        assert(c.power() == Power::Saving && uart.writes.size() == 4);
    }
    UART uart; uart.shortAt = 3;
    PowerControl c(UART::write, &uart); detect(c); feed(c, kAck, 20);
    assert(c.power() == Power::SleepFailed && uart.writes.size() == 4);
}
void identityRestrictions() {
    const Bytes variants[] = {
        monver("ROM SPG 5.20"), monver("ROM SPG 5.100"), monver("EXT CORE 5.10"),
        monver("ROM SPG 5.10", "00090000"),
        monver("ROM SPG 5.10", "000A0000", "PROTVER=34.100"),
        monver("ROM SPG 5.10", "000A0000", "PROTVER=34.20"),
        monver("ROM SPG 5.10", "000A0000", "FWVER=SPG 5.10")
    };
    for (const Bytes &version : variants) {
        UART uart; PowerControl c(UART::write, &uart); c.request(false, 0);
        feed(c, version, 10); feed(c, kAck, 20); c.tick(10000);
        assert(c.receiver() == Receiver::Unsupported && !c.supported());
        assert(c.power() == Power::Unsupported && uart.writes.size() == 2);
    }
    UART uart; PowerControl c(UART::write, &uart); c.request(true, 0);
    feed(c, monver("ROM SPG 5.10", "000A0000", "PROTVER=34.10", nullptr), 10);
    assert(c.supported() && !c.isMiaM10Q() && c.power() == Power::Active);
    assert(uart.writes.size() == 2);
    c.resetIdentity(20);
    feed(c, monver("ROM SPG 5.10", "000A0000", "PROTVER=34.10", "MOD=MIA-M10Q-other"), 30);
    assert(c.supported() && !c.isMiaM10Q());
}
void casicAndUnknown() {
    for (const char *identity : {"GPTXT,01,01,02,L76K", "GPTXT,01,01,02,MA=CASIC"}) {
        UART uart; PowerControl c(UART::write, &uart); c.request(false, 0);
        feed(c, nmea(identity), 10); feed(c, kAck, 20); c.tick(10000);
        assert(c.receiver() == Receiver::L76K && !c.supported() && !c.isMiaM10Q());
        assert(c.power() == Power::Unsupported && uart.writes.size() == 2);
    }
    UART uart; PowerControl c(UART::write, &uart); c.request(false, 0);
    Bytes bad = nmea("GPTXT,01,01,02,L76K"); bad[bad.size() - 4] ^= 1;
    feed(c, bad, 10); feed(c, nmea("GNTXT,01,01,02,L76K"), 20);
    feed(c, nmea("GPTXT,01,01,02,unidentified"), 30);
    assert(c.receiver() == Receiver::Unknown);
    c.tick(1499); assert(uart.writes.size() == 2);
    c.tick(1500); assert(uart.writes.size() == 4);
    c.tick(2999); assert(c.power() == Power::Detecting);
    c.tick(3000); assert(c.power() == Power::Unsupported && !c.supported());
    c.tick(100000); c.request(false, 100001);
    assert(uart.writes.size() == 4);
}
void corruptOversizedAndPartial() {
    UART uart; PowerControl c(UART::write, &uart); c.request(false, 0);
    Bytes bad = monver(); bad.back() ^= 1; feed(c, bad, 10);
    feed(c, ubx(0x0A, 4, Bytes(41, 0)), 20);
    Bytes oversized(513, 0);
    const Bytes nested = monver();
    memcpy(oversized.data(), nested.data(), nested.size());
    feed(c, ubx(0x0A, 4, oversized), 30);
    feed(c, nmea("GPTXT,01,01,02," + std::string(125, 'x') + "L76K"), 40);
    assert(c.receiver() == Receiver::Unknown && uart.writes.size() == 2);
    const Bytes good = monver();
    feed(c, Bytes(good.begin(), good.begin() + 8), 50);
    c.tick(300); feed(c, Bytes(good.begin() + 8, good.end()), 301);
    assert(c.receiver() == Receiver::Unknown);
    feed(c, Bytes{0xB5, 0xB5}, 310); feed(c, Bytes(good.begin() + 1, good.end()), 311);
    assert(c.supported() && c.power() == Power::Saving);
    // The same bound also protects a save from nested ACK bytes in an oversized frame.
    memcpy(oversized.data(), kAck.data(), kAck.size());
    feed(c, ubx(5, 1, oversized), 320);
    assert(c.power() == Power::Saving && uart.writes.size() == 3);
    feed(c, kAck, 330); assert(c.power() == Power::StandbyRequested);
    UART other; PowerControl d(UART::write, &other); d.request(true, 0);
    feed(d, Bytes{0xB5, 0x62, 0x0A, 4, 0xFF, 0xFF, 0, 0}, 10);
    feed(d, good, 11); // Ignore nested frames inside a huge, incomplete payload.
    assert(d.receiver() == Receiver::Unknown);
    d.tick(261); feed(d, good, 262);
    assert(d.supported() && d.power() == Power::Active);
}
void cancelAndWake() {
    UART uart; PowerControl c(UART::write, &uart); detect(c);
    feed(c, Bytes(kAck.begin(), kAck.begin() + 6), 20);
    c.request(true, 21);
    feed(c, Bytes(kAck.begin() + 6, kAck.end()), 22); feed(c, kAck, 23);
    assert(c.power() == Power::Active && uart.writes.size() == 3);
    c.request(true, 24); assert(uart.writes.size() == 3);
    c.request(false, 25); feed(c, kAck, 26);
    assert(c.power() == Power::StandbyRequested && uart.writes.size() == 5);
    c.request(true, 30);
    assert(c.power() == Power::Waking && uart.writes.back() == Bytes{0xFF});
    assert(c.supported() && c.isMiaM10Q());
    c.request(true, 31); c.tick(59); assert(uart.writes.size() == 6);
    c.tick(60); assert(c.power() == Power::Detecting && uart.writes.size() == 8);
    feed(c, monver(), 61);
    assert(c.power() == Power::Active && uart.writes.size() == 8);
    c.request(false, 70); feed(c, kAck, 71); c.request(true, 72); c.request(false, 73);
    assert(c.power() == Power::Waking);
    c.tick(102); feed(c, monver(), 103);
    assert(c.power() == Power::Saving && uart.writes.back() == kSave);
    c.request(true, 104); feed(c, kAck, 105);
    assert(c.power() == Power::Active && uart.writes.back() == kSave);
    c.resetIdentity(110); assert(!c.supported() && !c.isMiaM10Q());
    const size_t writes = uart.writes.size(); c.request(true, 111);
    feed(c, monver(), 112); assert(c.power() == Power::Active && uart.writes.size() == writes);
}
void sleepFailureAndWakeTimeout() {
    UART uart; PowerControl c(UART::write, &uart); detect(c); feed(c, kAck, 20);
    feed(c, nmea("GPRMC,,V,,,,,,,,,,N"), 519);
    assert(c.power() == Power::StandbyRequested);
    Bytes bad = nmea("GPRMC,,V,,,,,,,,,,N"); bad[bad.size() - 4] ^= 1;
    feed(c, bad, 520); assert(c.power() == Power::StandbyRequested);
    feed(c, nmea(""), 520); feed(c, nmea("not-a-sentence"), 520);
    assert(c.power() == Power::StandbyRequested);
    feed(c, nmea("GPRMC,,V,,,,,,,,,,N"), 521);
    assert(c.power() == Power::SleepFailed);
    c.request(true, 600); c.tick(630); c.tick(2130); c.tick(3630);
    assert(c.power() == Power::Unsupported && !c.supported());
    assert(uart.writes.size() == 9);
    UART failed; PowerControl d(UART::write, &failed); detect(d); feed(d, kAck, 20);
    failed.shortAt = 4; d.request(true, 30);
    assert(d.power() == Power::SleepFailed && failed.writes.size() == 5);
}
void rollover() {
    const uint32_t start = 0xFFFFFF00UL;
    UART uart; PowerControl c(UART::write, &uart); detect(c, false, start);
    c.tick(start + 1009); assert(c.power() == Power::Saving);
    feed(c, kAck, start + 1010 - 1);
    assert(c.power() == Power::StandbyRequested);
    feed(c, nmea("GPRMC,,V,,,,,,,,,,N"), start + 1508);
    assert(c.power() == Power::StandbyRequested);
    feed(c, nmea("GPRMC,,V,,,,,,,,,,N"), start + 1509);
    assert(c.power() == Power::SleepFailed);
    UART unknown; PowerControl d(UART::write, &unknown); d.request(true, start);
    d.tick(start - 1); assert(unknown.writes.size() == 2);
    d.tick(start + 1500); d.tick(start + 3000);
    assert(d.power() == Power::Unsupported && unknown.writes.size() == 4);
}
} // namespace

int main() {
    exactCommandsAndSleep(); saveFailures(); identityRestrictions(); casicAndUnknown();
    corruptOversizedAndPartial(); cancelAndWake(); sleepFailureAndWakeTimeout(); rollover();
    return 0;
}

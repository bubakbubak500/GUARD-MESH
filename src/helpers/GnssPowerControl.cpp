// SPDX-License-Identifier: GPL-3.0-or-later
#include "GnssPowerControl.h"
#include <string.h>

namespace gnss {
namespace {
const uint8_t kMonVer[] = {0xB5, 0x62, 0x0A, 0x04, 0, 0, 0x0E, 0x34};
const uint8_t kCasIdentity[] = "$PCAS06,0*1B\r\n";
// SPG 5.10 CFG-CFG: save to battery-backed RAM only, never flash or reset.
const uint8_t kSaveBbr[] = {0xB5, 0x62, 0x06, 0x09, 0x0D, 0,
    0, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 1, 0x1B, 0xA9};
// RXM-PMREQ v0: indefinite backup, force flag, UART RX wake source.
const uint8_t kStandby[] = {0xB5, 0x62, 0x02, 0x41, 0x10, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0, 8, 0, 0, 0, 0x61, 0x6B};

bool elapsed(uint32_t now, uint32_t then, uint32_t period) {
    const uint32_t age = now - then;
    return age < 0x80000000UL && age >= period;
}
bool field(const uint8_t *data, size_t bytes, const char *text, bool suffix = false) {
    const size_t length = strlen(text);
    if (length >= bytes || !memchr(data, 0, bytes) || memcmp(data, text, length)) return false;
    return data[length] == 0 || (suffix && data[length] == ' ');
}
int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
} // namespace

PowerControl::PowerControl(Write write, void *context) : write_(write), context_(context) {}

bool PowerControl::send(const uint8_t *data, size_t bytes) {
    return write_ && write_(context_, data, bytes) == bytes;
}
void PowerControl::clearParser() {
    ubxState_ = 0;
    nmeaLength_ = 0;
}
void PowerControl::probe(uint32_t now) {
    power_ = Power::Detecting;
    phaseAt_ = now;
    ++attempts_;
    clearParser();
    send(kMonVer, sizeof kMonVer);
    send(kCasIdentity, sizeof kCasIdentity - 1);
}
void PowerControl::save(uint32_t now) {
    if (enabled_ || !supported()) return;
    clearParser();
    power_ = Power::Saving;
    phaseAt_ = now;
    if (!send(kSaveBbr, sizeof kSaveBbr)) power_ = Power::SaveFailed;
}
void PowerControl::wake(uint32_t now) {
    clearParser();
    power_ = Power::Waking;
    phaseAt_ = now;
    const uint8_t byte = 0xFF;
    if (!send(&byte, 1)) power_ = Power::SleepFailed;
}
void PowerControl::request(bool enabled, uint32_t now) {
    if (requested_ && enabled_ == enabled) return;
    const bool first = !requested_;
    requested_ = true;
    enabled_ = enabled;
    if (first) {
        attempts_ = 0;
        probe(now);
        return;
    }
    if (power_ == Power::Detecting || power_ == Power::Waking) return;
    clearParser(); // A partial/late save ACK must not finish a cancelled request.
    if (enabled) {
        if (power_ == Power::StandbyRequested || power_ == Power::SleepFailed) wake(now);
        else power_ = supported() ? Power::Active : Power::Unsupported;
    } else if (supported()) {
        save(now);
    }
}
void PowerControl::resetIdentity(uint32_t now) {
    receiver_ = Receiver::Unknown;
    miaM10Q_ = false;
    clearParser();
    attempts_ = 0;
    power_ = Power::Detecting;
    if (requested_) probe(now);
}
void PowerControl::identified(Receiver receiver, uint32_t now) {
    receiver_ = receiver;
    power_ = supported() ? Power::Active : Power::Unsupported;
    if (!enabled_ && supported()) save(now);
}
void PowerControl::tick(uint32_t now) {
    if ((ubxState_ || nmeaLength_) && elapsed(now, frameAt_, 250)) clearParser();
    if (!requested_) return;
    if (power_ == Power::Detecting && elapsed(now, phaseAt_, 1500)) {
        if (attempts_ < 2) probe(now);
        else identified(Receiver::Unsupported, now);
    } else if (power_ == Power::Saving && elapsed(now, phaseAt_, 1000)) {
        power_ = Power::SaveFailed;
        clearParser();
    } else if (power_ == Power::Waking && elapsed(now, phaseAt_, 30)) {
        attempts_ = 0;
        probe(now);
    }
}
void PowerControl::ubxFrame(uint32_t now) {
    if (!requested_) return;
    if (power_ == Power::Detecting && ubxClass_ == 0x0A && ubxId_ == 0x04) {
        if (ubxLength_ < 40 || (ubxLength_ - 40) % 30) return;
        bool protocol = false;
        miaM10Q_ = false;
        for (size_t i = 40; i < ubxLength_; i += 30) {
            protocol = protocol || field(payload_ + i, 30, "PROTVER=34.10");
            miaM10Q_ = miaM10Q_ || field(payload_ + i, 30, "MOD=MIA-M10Q");
        }
        identified(field(payload_, 30, "ROM SPG 5.10", true) &&
                   field(payload_ + 30, 10, "000A0000") && protocol
                   ? Receiver::UbloxM10 : Receiver::Unsupported, now);
    } else if (power_ == Power::Saving && !enabled_ && ubxClass_ == 0x05 &&
               ubxLength_ == 2 && payload_[0] == 0x06 && payload_[1] == 0x09) {
        if (ubxId_ == 0) power_ = Power::SaveFailed;
        else if (ubxId_ == 1) {
            power_ = send(kStandby, sizeof kStandby) ? Power::StandbyRequested : Power::SleepFailed;
            phaseAt_ = now;
        }
    }
}
void PowerControl::nmeaFrame(uint32_t now) {
    nmea_[nmeaLength_] = 0;
    char *star = strchr(nmea_, '*');
    if (!star || size_t(star - nmea_) + 3 != nmeaLength_ || star <= nmea_ + 6 || nmea_[6] != ',') return;
    for (size_t i = 1; i < 6; ++i)
        if (nmea_[i] < 'A' || nmea_[i] > 'Z') return;
    const int high = hex(star[1]), low = hex(star[2]);
    if (high < 0 || low < 0) return;
    uint8_t checksum = 0;
    for (const char *p = nmea_ + 1; p != star; ++p) checksum ^= uint8_t(*p);
    if (checksum != uint8_t(high * 16 + low)) return;
    if (power_ == Power::StandbyRequested && elapsed(now, phaseAt_, 500)) {
        power_ = Power::SleepFailed;
    } else if (power_ == Power::Detecting && !strncmp(nmea_, "$GPTXT,", 7)) {
        *star = 0;
        if (strstr(nmea_ + 7, "L76K") || strstr(nmea_ + 7, "CASIC")) {
            miaM10Q_ = false;
            identified(Receiver::L76K, now);
        }
    }
}
void PowerControl::feed(uint8_t byte, uint32_t now) {
    tick(now);
    if (!requested_) return;
    if (ubxState_) {
        frameAt_ = now;
        switch (ubxState_) {
        case 1:
            if (byte == 0x62) { ubxState_ = 2; checksumA_ = checksumB_ = 0; }
            else ubxState_ = byte == 0xB5 ? 1 : 0;
            return;
        case 2: ubxClass_ = byte; ubxState_ = 3; break;
        case 3: ubxId_ = byte; ubxState_ = 4; break;
        case 4: ubxLength_ = byte; ubxState_ = 5; break;
        case 5:
            ubxLength_ |= uint16_t(byte) << 8;
            ubxPosition_ = 0;
            ubxState_ = ubxLength_ ? 6 : 7;
            break;
        case 6:
            if (ubxLength_ <= sizeof payload_) payload_[ubxPosition_] = byte;
            if (++ubxPosition_ == ubxLength_) ubxState_ = 7;
            break;
        case 7: receivedA_ = byte; ubxState_ = 8; return;
        case 8:
            ubxState_ = 0;
            if (ubxLength_ <= sizeof payload_ && receivedA_ == checksumA_ && byte == checksumB_)
                ubxFrame(now);
            return;
        default: clearParser(); return;
        }
        checksumA_ += byte;
        checksumB_ += checksumA_;
        return;
    }
    if (byte == 0xB5) { nmeaLength_ = 0; ubxState_ = 1; frameAt_ = now; return; }
    if (byte == '$') { nmea_[0] = '$'; nmeaLength_ = 1; frameAt_ = now; return; }
    if (!nmeaLength_) return;
    frameAt_ = now;
    if (byte == '\r') return;
    if (byte == '\n') { nmeaFrame(now); nmeaLength_ = 0; return; }
    if (byte < 0x20 || byte > 0x7E || nmeaLength_ == 120) { nmeaLength_ = 0; return; }
    nmea_[nmeaLength_++] = char(byte);
}
} // namespace gnss

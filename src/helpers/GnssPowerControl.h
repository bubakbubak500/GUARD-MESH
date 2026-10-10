// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace gnss {
enum class Receiver { Unknown, UbloxM10, L76K, Unsupported };
enum class Power { Detecting, Active, Unsupported, Saving, StandbyRequested, SaveFailed, SleepFailed, Waking };

// UART protocol state only. StandbyRequested is not a measurement of power draw.
class PowerControl {
public:
    typedef size_t (*Write)(void *, const uint8_t *, size_t);
    PowerControl(Write write, void *context);
    void request(bool enabled, uint32_t now);
    void feed(uint8_t byte, uint32_t now);
    void tick(uint32_t now);
    void resetIdentity(uint32_t now);
    Receiver receiver() const { return receiver_; }
    Power power() const { return power_; }
    bool supported() const { return receiver_ == Receiver::UbloxM10; }
    bool isMiaM10Q() const { return miaM10Q_; }
    bool binaryFrame() const { return ubxState_ != 0; }

private:
    void clearParser();
    void probe(uint32_t now);
    void save(uint32_t now);
    void wake(uint32_t now);
    void identified(Receiver receiver, uint32_t now);
    void ubxFrame(uint32_t now);
    void nmeaFrame(uint32_t now);
    bool send(const uint8_t *data, size_t bytes);

    Write write_;
    void *context_;
    Receiver receiver_ = Receiver::Unknown;
    Power power_ = Power::Detecting;
    bool requested_ = false;
    bool enabled_ = false;
    bool miaM10Q_ = false;
    uint8_t attempts_ = 0;
    uint32_t phaseAt_ = 0;
    uint32_t frameAt_ = 0;

    uint8_t ubxState_ = 0;
    uint8_t ubxClass_ = 0;
    uint8_t ubxId_ = 0;
    uint16_t ubxLength_ = 0;
    uint32_t ubxPosition_ = 0;
    uint8_t checksumA_ = 0;
    uint8_t checksumB_ = 0;
    uint8_t receivedA_ = 0;
    uint8_t payload_[512] = {};
    char nmea_[121] = {};
    uint8_t nmeaLength_ = 0;
};
} // namespace gnss

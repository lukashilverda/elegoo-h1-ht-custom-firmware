#pragma once

#include "sensors.hpp"

#include <cstddef>

namespace h1 {

class HumidityBus {
public:
    virtual bool write(std::uint8_t address, const std::uint8_t* data, std::size_t size) = 0;
    virtual bool read(std::uint8_t address, std::uint8_t* data, std::size_t size) = 0;

protected:
    ~HumidityBus() = default;
};

enum class HumidityPollResult {
    waiting,
    sample,
    busError,
};

class HumiditySensor {
public:
    HumiditySensor(HumidityBus& bus, std::uint32_t powerOnMs);
    HumidityPollResult poll(std::uint32_t nowMs, ChamberReading& reading);

private:
    enum class Phase {
        powerOn,
        initialization,
        conversion,
        idle,
    };

    HumidityPollResult startMeasurement(std::uint32_t nowMs);
    void enter(Phase phase, std::uint32_t nowMs, std::uint32_t waitMs);

    HumidityBus& bus_;
    Phase phase_ = Phase::powerOn;
    std::uint32_t sinceMs_;
    std::uint32_t waitMs_ = humidityPowerOnDelayMs;
};

}  // namespace h1

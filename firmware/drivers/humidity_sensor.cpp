#include "humidity_sensor.hpp"

namespace h1 {

HumiditySensor::HumiditySensor(HumidityBus& bus, std::uint32_t powerOnMs)
    : bus_(bus), sinceMs_(powerOnMs) {}

HumidityPollResult HumiditySensor::poll(std::uint32_t nowMs, ChamberReading& reading) {
    if (nowMs - sinceMs_ < waitMs_) {
        return HumidityPollResult::waiting;
    }

    if (phase_ == Phase::powerOn) {
        std::uint8_t status = 0;
        if (!bus_.read(humiditySensorAddress, &status, 1)) {
            enter(Phase::powerOn, nowMs, humidityRuntimeIdleMs);
            return HumidityPollResult::busError;
        }
        if ((status & 0x18u) != 0x18u) {
            if (!bus_.write(humiditySensorAddress, humidityInitialize.data(), humidityInitialize.size())) {
                enter(Phase::powerOn, nowMs, humidityRuntimeIdleMs);
                return HumidityPollResult::busError;
            }
            enter(Phase::initialization, nowMs, humidityInitializationDelayMs);
            return HumidityPollResult::waiting;
        }
    }

    if (phase_ == Phase::conversion) {
        std::array<std::uint8_t, 7> frame{};
        const bool readSucceeded = bus_.read(humiditySensorAddress, frame.data(), frame.size());
        enter(Phase::idle, nowMs, humidityRuntimeIdleMs);
        if (!readSucceeded) {
            return HumidityPollResult::busError;
        }
        reading = decodeHumidityFrame(frame);
        return HumidityPollResult::sample;
    }
    return startMeasurement(nowMs);
}

HumidityPollResult HumiditySensor::startMeasurement(std::uint32_t nowMs) {
    if (!bus_.write(humiditySensorAddress, humidityMeasure.data(), humidityMeasure.size())) {
        enter(Phase::idle, nowMs, humidityRuntimeIdleMs);
        return HumidityPollResult::busError;
    }
    enter(Phase::conversion, nowMs, humidityConversionDelayMs);
    return HumidityPollResult::waiting;
}

void HumiditySensor::enter(Phase phase, std::uint32_t nowMs, std::uint32_t waitMs) {
    phase_ = phase;
    sinceMs_ = nowMs;
    waitMs_ = waitMs;
}

}  // namespace h1

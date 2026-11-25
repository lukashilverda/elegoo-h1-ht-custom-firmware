#include "thermistor.h"
#include "../board.hpp"

namespace h1::hal::thermistor {
using namespace h1::board;
namespace {

constexpr std::uint32_t adc = 0x40012400;
bool adcReady;

bool waitForBits(std::uint32_t address, std::uint32_t mask, bool set) {
    const auto start = milliseconds();
    do {
        if (((reg(address) & mask) != 0) == set) return true;
    } while (milliseconds() - start < 2);
    return false;
}

}  // namespace

bool initialize() {
    adcReady = false;
    reg(rcu + 0x18) |= (1u << 2) | (1u << 9);
    constexpr std::uint32_t resetCalibration = 1u << 3;
    constexpr std::uint32_t calibrate = 1u << 2;
    constexpr std::uint32_t enable = 1;
    pinMode(gpioA, 0, 0);
    reg(rcu + 0x0c) |= 1u << 9;
    reg(rcu + 0x0c) &= ~(1u << 9);
    reg(adc + 0x10) = 7; // Channel 0 uses the longest sample time.
    reg(adc + 8) = enable;
    delay(1);
    reg(adc + 8) |= resetCalibration;
    if (!waitForBits(adc + 8, resetCalibration, false)) return false;
    reg(adc + 8) |= calibrate;
    adcReady = waitForBits(adc + 8, calibrate, false);
    // Enable the regular conversion software trigger (ETSRC 111, ETERC).
    if (adcReady) reg(adc + 8) = enable | (7u << 17) | (1u << 20);
    return adcReady;
}

ThermistorReading read() {
    if (!adcReady) return {0, false};
    constexpr std::uint32_t endOfConversion = 1u << 1;
    ThermistorSamples samples;
    for (auto& sample : samples) {
        reg(adc + 8) |= 1u << 22;
        if (!waitForBits(adc, endOfConversion, true)) return {0, false};
        sample = static_cast<std::uint16_t>(reg(adc + 0x4c) & 0xfffu);
    }
    return decodeThermistorSamples(samples);
}

}  // namespace h1::hal::thermistor

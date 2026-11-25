#pragma once

#include <array>
#include <cstdint>

namespace h1 {

inline constexpr std::uint8_t humiditySensorAddress = 0x38;
inline constexpr std::array<std::uint8_t, 3> humidityInitialize{0xbe, 0x08, 0x00};
inline constexpr std::array<std::uint8_t, 3> humidityMeasure{0xac, 0x33, 0x00};
inline constexpr std::uint32_t humidityPowerOnDelayMs = 40;
inline constexpr std::uint32_t humidityInitializationDelayMs = 10;
inline constexpr std::uint32_t humidityConversionDelayMs = 80;
inline constexpr std::uint32_t humidityRuntimeIdleMs = 120;

enum class HumidityStatus {
    ready,
    busy,
    badChecksum,
};

struct ChamberReading {
    HumidityStatus status;
    float temperatureCelsius;
    float relativeHumidityPercent;
};

ChamberReading decodeHumidityFrame(const std::array<std::uint8_t, 7>& frame);

// The factory ADC supplies six right-aligned 12-bit samples per result.
using ThermistorSamples = std::array<std::uint16_t, 6>;

struct ThermistorReading {
    float temperatureCelsius;
    // Describes the calibration table range only. This is not a fault diagnosis.
    bool withinCalibrationRange;
};

ThermistorReading decodeThermistorSamples(const ThermistorSamples& samples);

}  // namespace h1

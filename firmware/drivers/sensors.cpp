#include "sensors.hpp"

namespace h1 {
namespace {

// Factory table at 0x080454FC. Temperatures run from 300 to -15 C in 5 C steps.
constexpr std::array<std::uint16_t, 64> thermistorAdcSums{
    138, 150, 162, 168, 186, 198, 210, 228,
    246, 264, 288, 312, 336, 366, 396, 426,
    468, 504, 552, 600, 654, 720, 786, 858,
    936, 1026, 1122, 1230, 1344, 1470, 1608, 1758,
    1920, 2088, 2274, 2466, 2670, 2880, 3096, 3318,
    3546, 3768, 3990, 4212, 4422, 4620, 4806, 4980,
    5142, 5286, 5418, 5532, 5634, 5724, 5796, 5862,
    5910, 5958, 5994, 6024, 6048, 6072, 6096, 6120,
};

std::uint8_t humidityChecksum(const std::array<std::uint8_t, 7>& frame) {
    std::uint8_t crc = 0xff;
    for (std::size_t index = 0; index < frame.size() - 1; ++index) {
        crc ^= frame[index];
        for (unsigned bit = 0; bit < 8; ++bit) {
            const bool highBitSet = (crc & 0x80u) != 0;
            crc = static_cast<std::uint8_t>(crc << 1);
            if (highBitSet) {
                crc ^= 0x31u;
            }
        }
    }
    return crc;
}

}  // namespace

ChamberReading decodeHumidityFrame(const std::array<std::uint8_t, 7>& frame) {
    if (humidityChecksum(frame) != frame.back()) {
        return {HumidityStatus::badChecksum, 0.0f, 0.0f};
    }
    if ((frame[0] & 0x80u) != 0) {
        return {HumidityStatus::busy, 0.0f, 0.0f};
    }

    const std::uint32_t rawHumidity = (std::uint32_t{frame[1]} << 12)
        | (std::uint32_t{frame[2]} << 4) | (frame[3] >> 4);
    const std::uint32_t rawTemperature = (std::uint32_t{frame[3] & 0x0fu} << 16)
        | (std::uint32_t{frame[4]} << 8) | frame[5];
    constexpr float fraction = 1.0f / 1048576.0f;
    return {
        HumidityStatus::ready,
        static_cast<float>(rawTemperature) * fraction * 200.0f - 50.0f,
        static_cast<float>(rawHumidity) * fraction * 100.0f,
    };
}

ThermistorReading decodeThermistorSamples(const ThermistorSamples& samples) {
    int adcSum = 0;
    for (const auto sample : samples) {
        adcSum += sample >> 2;
    }

    const bool withinCalibrationRange =
        adcSum >= thermistorAdcSums.front() && adcSum <= thermistorAdcSums.back();
    for (std::size_t index = 1; index < thermistorAdcSums.size(); ++index) {
        const int upperSum = thermistorAdcSums[index];
        if (adcSum < upperSum) {
            const int lowerSum = thermistorAdcSums[index - 1];
            const auto fraction = static_cast<float>(adcSum - lowerSum)
                / static_cast<float>(upperSum - lowerSum);
            const float lowerSumTemperature = 300.0f - 5.0f * static_cast<float>(index - 1);
            return {lowerSumTemperature - 5.0f * fraction, withinCalibrationRange};
        }
    }
    return {-15.0f, withinCalibrationRange};
}

}  // namespace h1

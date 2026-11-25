#pragma once

#include <cstdint>

namespace h1 {

inline constexpr unsigned maxDryingTemperature = 100;
inline constexpr unsigned thermalSafetyCeiling = 100;

// The regulator returns a demand. The output driver checks the interlocks separately.
class HeatControl {
public:
    bool demand(std::uint32_t now, unsigned target, float thermistor, int chamberHundredths, bool permitted);

private:
    enum class Phase { idle, warmup, soak, heating, cooling };
    bool chamberHold_ = false;
    Phase phase_ = Phase::idle;
    std::uint32_t soakStarted_ = 0;
};

}  // namespace h1

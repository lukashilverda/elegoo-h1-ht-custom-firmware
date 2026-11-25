#include "heat_control.hpp"

namespace h1 {

bool HeatControl::demand(std::uint32_t now, unsigned target, float thermistor, int chamberHundredths, bool permitted) {
    const auto degrees = static_cast<float>(target);
    const int chamberTarget = static_cast<int>(target) * 100;
    if (chamberHundredths >= chamberTarget) chamberHold_ = true;
    if (chamberHundredths <= chamberTarget - 200) chamberHold_ = false;

    if (!permitted || chamberHold_ || target < 35 || target > maxDryingTemperature
        || !(thermistor > -15.0f && thermistor < static_cast<float>(thermalSafetyCeiling))) {
        phase_ = Phase::idle;
        return false;
    }

    switch (phase_) {
        case Phase::idle:
        case Phase::warmup:
            if (thermistor < degrees - 4.0f) {
                phase_ = Phase::warmup;
                return true;
            }
            phase_ = Phase::soak;
            soakStarted_ = now;
            return false;
        case Phase::soak:
            if (now - soakStarted_ < 8000) return false;
            break;
        case Phase::cooling:
            if (thermistor > degrees - 2.0f) return false;
            break;
        case Phase::heating:
            break;
    }
    const bool request = thermistor < degrees;
    phase_ = request ? Phase::heating : Phase::cooling;
    return request;
}

}  // namespace h1

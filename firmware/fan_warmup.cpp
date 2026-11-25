#include "fan_warmup.hpp"

namespace h1::cold {
namespace {
constexpr unsigned spinUpMs = 750;
constexpr unsigned windowMs = 500;
constexpr unsigned recoveryMs = 250;
constexpr unsigned minReferencePulses = 4;
}

void FanWarmup::enter(Phase phase, std::uint32_t now) {
    phase_ = phase;
    windowAt_ = now;
    pulses_ = 0;
}

void FanWarmup::update(std::uint32_t now, bool running, bool warmup, bool pulse, bool airflow) {
    if (!running) {
        phase_ = Phase::off;
        return;
    }
    if (phase_ == Phase::off) enter(Phase::spinUp, now);
    if ((phase_ == Phase::trial || phase_ == Phase::low) && (!warmup || !airflow)) {
        enter(Phase::recovering, now);
    }
    if (pulse && phase_ != Phase::normal) ++pulses_;
    const auto elapsed = now - windowAt_;

    switch (phase_) {
    case Phase::spinUp:
        if (elapsed >= spinUpMs) enter(warmup && airflow ? Phase::reference : Phase::normal, now);
        break;
    case Phase::reference:
        if (!warmup || !airflow) {
            enter(Phase::normal, now);
        } else if (elapsed >= windowMs) {
            // Need at least 85% of the pulse count at 90% PWM.
            minimum_ = (pulses_ * 85 + 99) / 100;
            enter(pulses_ >= minReferencePulses ? Phase::trial : Phase::normal, now);
        }
        break;
    case Phase::trial:
    case Phase::low:
        if (elapsed >= windowMs) {
            enter(pulses_ >= minimum_ ? Phase::low : Phase::recovering, now);
        }
        break;
    case Phase::recovering:
        if (elapsed >= recoveryMs && airflow && pulses_ >= 3) enter(Phase::normal, now);
        break;
    case Phase::off:
    case Phase::normal:
        break;
    }
}

unsigned FanWarmup::duty() const {
    const auto phase = phase_;
    return phase == Phase::trial || phase == Phase::low ? warmupDuty : normalDuty;
}

bool FanWarmup::allowsHeat() const {
    const auto phase = phase_;
    return phase != Phase::trial && phase != Phase::recovering;
}

}  // namespace h1::cold


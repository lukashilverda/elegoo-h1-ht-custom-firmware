#pragma once

#include <cstdint>

namespace h1::cold {

// Uses the fan's own full-speed feedback as the reference for a single lower-duty trial.
class FanWarmup {
public:
    static constexpr unsigned normalDuty = 90;
    static constexpr unsigned warmupDuty = 80;

    void update(std::uint32_t now, bool running, bool warmup, bool pulse, bool airflow);
    unsigned duty() const;
    bool allowsHeat() const;

private:
    enum class Phase { off, spinUp, reference, trial, low, recovering, normal };
    void enter(Phase phase, std::uint32_t now);

    volatile Phase phase_ = Phase::off; // SysTick writes it, the foreground reads the interlock.
    std::uint32_t windowAt_ = 0;
    unsigned pulses_ = 0;
    unsigned minimum_ = 0;
};

}  // namespace h1::cold


#pragma once

#include <cstdint>

namespace h1 {

enum class VentTarget : std::uint8_t { hold, closed, open };

struct VentReport {
    bool exhaustClosed;
    bool inletClosed;
    unsigned faultMask;
};

class VentDrive {
public:
    VentTarget update(VentTarget request, bool closedFeedback, std::uint32_t now);
    bool reached() const { return state_ == State::reached; }
    bool fault() const { return state_ == State::fault; }

private:
    enum class State { stopped, settling, drive, check, reverse, retryWait, reached, fault };
    VentTarget target_ = VentTarget::hold;
    State state_ = State::stopped;
    std::uint32_t since_ = 0;
    unsigned attempts_ = 0;
};

}  // namespace h1

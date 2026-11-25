#include "vent_control.hpp"

namespace h1 {

VentTarget VentDrive::update(VentTarget request, bool closedFeedback, std::uint32_t now) {
    if (request != target_) {
        target_ = request;
        since_ = now;
        attempts_ = 0;
        state_ = request == VentTarget::hold ? State::stopped : State::settling;
    }

    const auto elapsed = now - since_;
    switch (state_) {
        case State::settling:
        case State::retryWait:
            if (elapsed >= 300) {
                ++attempts_;
                since_ = now;
                state_ = State::drive;
            }
            break;
        case State::drive:
            if (elapsed >= (attempts_ == 1 ? 500u : 900u)) {
                since_ = now;
                state_ = State::check;
            }
            break;
        case State::check:
            if (elapsed < 300) break;
            if (closedFeedback == (target_ == VentTarget::closed)) {
                state_ = State::reached;
            } else if (attempts_ == 4) {
                state_ = State::fault;
            } else {
                state_ = State::reverse;
                since_ = now;
            }
            break;
        case State::reverse:
            if (elapsed >= 900) {
                since_ = now;
                state_ = State::retryWait;
            }
            break;
        case State::reached:
        case State::stopped:
        case State::fault:
            break;
    }

    if (state_ == State::drive) return target_;
    if (state_ == State::reverse) {
        return target_ == VentTarget::closed ? VentTarget::open : VentTarget::closed;
    }
    return VentTarget::hold;
}

}  // namespace h1

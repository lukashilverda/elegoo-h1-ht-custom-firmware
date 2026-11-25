#include "inlet_vent.h"
#include "../board.hpp"

namespace h1::hal::inletVent {
using namespace h1::board;

void initialize() {
    reg(rcu + 0x18) |= 1u << 4;
    pinWrite(gpioC, 0, true);
    pinMode(gpioC, 0, 0x8);
    drive(VentDirection::stop);
    pinMode(gpioC, 8, 3);
    pinMode(gpioC, 9, 3);
}

void drive(VentDirection direction) {
    if (direction != VentDirection::towardClosed) pinWrite(gpioC, 9, false);
    if (direction != VentDirection::towardOpen) pinWrite(gpioC, 8, false);
    if (direction == VentDirection::towardClosed) pinWrite(gpioC, 9, true);
    if (direction == VentDirection::towardOpen) pinWrite(gpioC, 8, true);
}

bool feedbackHigh() {
    return pinRead(gpioC, 0);
}

}  // namespace h1::hal::inletVent

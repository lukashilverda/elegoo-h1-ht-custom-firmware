#include "exhaust_vent.h"
#include "../board.hpp"

namespace h1::hal::exhaustVent {
using namespace h1::board;

void initialize() {
    reg(rcu + 0x18) |= (1u << 2) | (1u << 4);
    pinWrite(gpioA, 1, true);
    pinMode(gpioA, 1, 0x8);
    drive(VentDirection::stop);
    pinMode(gpioC, 6, 3);
    pinMode(gpioC, 7, 3);
}

void drive(VentDirection direction) {
    // Clear the opposite bridge input before asserting either direction.
    if (direction != VentDirection::towardClosed) pinWrite(gpioC, 6, false);
    if (direction != VentDirection::towardOpen) pinWrite(gpioC, 7, false);
    if (direction == VentDirection::towardClosed) pinWrite(gpioC, 6, true);
    if (direction == VentDirection::towardOpen) pinWrite(gpioC, 7, true);
}

bool feedbackHigh() {
    return pinRead(gpioA, 1);
}

}  // namespace h1::hal::exhaustVent

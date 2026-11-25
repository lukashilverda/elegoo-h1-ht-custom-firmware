#include "vent_bridge.h"
#include "../board.hpp"

namespace h1::hal::ventBridge {
using namespace h1::board;

void initialize() {
    reg(rcu + 0x18) |= 1u << 4;
    setEnabled(false);
    pinMode(gpioC, 10, 3);
    pinWrite(gpioC, 11, true);
    pinMode(gpioC, 11, 0x8);
}

void setEnabled(bool enabled) {
    pinWrite(gpioC, 10, enabled);
}

bool statusHigh() {
    return pinRead(gpioC, 11);
}

}  // namespace h1::hal::ventBridge

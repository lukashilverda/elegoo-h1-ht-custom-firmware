#include "heater.h"
#include "../board.hpp"

namespace h1::hal::heater {
using namespace h1::board;

void initialize() {
    reg(rcu + 0x18) |= 1u << 3;
    setEnabled(false);
    pinMode(gpioB, 1, 0x2);
}

void setEnabled(bool enabled) {
    pinWrite(gpioB, 1, enabled);
}

}  // namespace h1::hal::heater

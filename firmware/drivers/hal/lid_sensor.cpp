#include "lid_sensor.h"
#include "../board.hpp"

namespace h1::hal::lidSensor {
using namespace h1::board;

void initialize() {
    reg(rcu + 0x18) |= 1u << 2;
    pinWrite(gpioA, 2, true);
    pinMode(gpioA, 2, 0x8);
}

bool levelHigh() {
    return pinRead(gpioA, 2);
}

}  // namespace h1::hal::lidSensor

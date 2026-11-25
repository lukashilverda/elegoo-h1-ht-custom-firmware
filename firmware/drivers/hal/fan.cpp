#include "fan.h"
#include "../board.hpp"

namespace h1::hal::fan {
using namespace h1::board;
namespace {
constexpr std::uint32_t timer = 0x40012c00;
constexpr std::uint32_t captureFlag = 1u << 2;
unsigned periodCounts = 320;
}

void initialize() {
    reg(rcu + 0x18) |= (1u << 2) | (1u << 11);
    reg(rcu + 0x0c) |= 1u << 11;
    reg(rcu + 0x0c) &= ~(1u << 11);
    pinWrite(gpioA, 9, true);
    pinMode(gpioA, 9, 0x8);
    // CH0 PWM0 and CH1 direct input capture.
    reg(timer + 0x18) = 0x160;
    reg(timer + 0x20) = (1u << 4) | 1u;
    periodCounts = coreClockHz() / 25000;
    reg(timer + 0x2c) = periodCounts - 1;
    setDutyPercent(0);
    reg(timer + 0x14) = 1;
    reg(timer + 0x44) = 1u << 15;
    reg(timer) = 1;
    pinMode(gpioA, 8, 0x0b);
}

void setDutyPercent(unsigned percent) {
    if (percent > 100) percent = 100;
    reg(timer + 0x34) = periodCounts * percent / 100;
}

bool takePulse() {
    if ((reg(timer + 0x10) & captureFlag) == 0) return false;
    reg(timer + 0x10) = ~captureFlag;
    return true;
}

}  // namespace h1::hal::fan

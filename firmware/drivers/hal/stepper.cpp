#include "stepper.h"
#include "../board.hpp"

namespace h1::hal::stepper {
using namespace h1::board;

namespace {
constexpr std::uint32_t timer = 0x40000c00;
constexpr auto enableInterrupts = timer + 0x0c;
constexpr auto flags = timer + 0x10;
constexpr auto counter = timer + 0x24;
constexpr auto compare = timer + 0x34;
constexpr unsigned compareEvent = 1u << 1;
constexpr unsigned irq = 50;
constexpr std::uint16_t stepHighUs = 40;
constexpr unsigned minimumLeadUs = 16;
constexpr unsigned initialStepsPerSecond = 125;
constexpr unsigned targetStepsPerSecond = 500;
constexpr unsigned accelerationMs = 650;
volatile bool running;
bool high;
std::uint16_t plannedRise;
std::uint32_t startedAt;

unsigned stepPeriod() {
    const auto elapsed = milliseconds() - startedAt;
    if (elapsed >= accelerationMs) return 1000000 / targetStepsPerSecond;
    const auto rate = initialStepsPerSecond +
                      (targetStepsPerSecond - initialStepsPerSecond) * elapsed / accelerationMs;
    return 1000000 / rate;
}
}

void initialize() {
    reg(rcu + 0x18) |= 1u << 4;
    setEnabled(false);
    setDirectionHigh(false);
    setStepHigh(false);
    pinMode(gpioC, 1, 3);
    pinMode(gpioC, 2, 3);
    pinMode(gpioC, 3, 3);

    reg(rcu + 0x1c) |= 1u << 3;
    reg(rcu + 0x10) |= 1u << 3;
    reg(rcu + 0x10) &= ~(1u << 3);
    reg(timer + 0x28) = coreClockHz() / 1000000 - 1;
    reg(timer + 0x2c) = 0xffff;
    reg(timer + 0x18) = 0; // Channel 0 output compare in timing mode.
    reg(timer + 0x20) = 1u; // No GPIO is configured for the channel output.
    reg(timer + 0x14) = 1u;
    reg(flags) = 0;
    constexpr auto priorityAddress = 0xe000e400u + (irq & ~3u);
    constexpr auto priorityShift = (irq % 4) * 8;
    reg(priorityAddress) = (reg(priorityAddress) & ~(0xffu << priorityShift)) | (0x40u << priorityShift);
    reg(0xe000e280 + (irq / 32) * 4) = 1u << (irq % 32);
    reg(0xe000e100 + (irq / 32) * 4) = 1u << (irq % 32);
}

void setEnabled(bool enabled) {
    pinWrite(gpioC, 1, !enabled);
}

void setDirectionHigh(bool high) {
    pinWrite(gpioC, 2, high);
}

void setStepHigh(bool high) {
    pinWrite(gpioC, 3, high);
}

void emergencyStop() {
    reg(enableInterrupts) = 0;
    reg(timer) = 0;
    reg(flags) = 0;
    running = false;
    high = false;
    setEnabled(false);
    setStepHigh(false);
}

void setRotating(bool rotate) {
    if (rotate == running) return;
    if (!rotate) {
        emergencyStop();
        return;
    }
    startedAt = milliseconds();
    plannedRise = static_cast<std::uint16_t>(1000000 / initialStepsPerSecond);
    high = false;
    running = true;
    setStepHigh(false);
    setEnabled(true);
    reg(counter) = 0;
    reg(compare) = plannedRise;
    reg(flags) = 0;
    reg(enableInterrupts) = compareEvent;
    reg(timer) = 1u;
}

void pulse() {
    if (!(reg(flags) & compareEvent)) return;
    reg(flags) = 0;
    if (!running) return;

    high = !high;
    setStepHigh(high);
    if (high) {
        // Width is relative to the actual rising edge, even if the IRQ was late.
        reg(compare) = static_cast<std::uint16_t>(reg(counter) + stepHighUs);
        return;
    }

    const auto period = stepPeriod();
    const auto now = static_cast<std::uint16_t>(reg(counter));
    const auto sincePlan = static_cast<std::uint16_t>(now - plannedRise);
    // Keep the cadence in phase after a short delay. Skip a missed edge after a long delay.
    if (sincePlan + minimumLeadUs >= period) plannedRise = static_cast<std::uint16_t>(now + period);
    else plannedRise = static_cast<std::uint16_t>(plannedRise + period);
    reg(compare) = plannedRise;
}

}  // namespace h1::hal::stepper

extern "C" void Timer4_Handler() {
    h1::hal::stepper::pulse();
}


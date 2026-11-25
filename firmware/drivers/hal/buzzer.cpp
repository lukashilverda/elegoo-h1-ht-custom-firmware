#include "buzzer.h"
#include "../board.hpp"

namespace h1::hal::buzzer {
using namespace h1::board;
namespace {
constexpr std::uint32_t timer = 0x40001000;
constexpr unsigned irq = 54;
constexpr unsigned update = 1u;
bool ready;
bool high;
unsigned volume;
unsigned highCounts;
unsigned lowCounts;

void stopTimer() {
    if (!ready) return;
    reg(timer + 0x0c) = 0;
    reg(timer) = 4;
    reg(timer + 0x10) = 0;
}

std::uint32_t maskInterrupts() {
    std::uint32_t primask;
    asm volatile("mrs %0, primask\ncpsid i" : "=r"(primask) :: "memory");
    return primask;
}

void restoreInterrupts(std::uint32_t primask) {
    asm volatile("msr primask, %0" :: "r"(primask) : "memory");
}
}

void initialize() {
    reg(rcu + 0x18) |= 1u << 2;
    pinWrite(gpioA, 10, false);
    pinMode(gpioA, 10, 3);

    reg(rcu + 0x1c) |= 1u << 4;
    reg(rcu + 0x10) |= 1u << 4;
    reg(rcu + 0x10) &= ~(1u << 4);
    reg(timer + 0x28) = coreClockHz() / 1000000 - 1;
    reg(timer) = 4;
    reg(timer + 0x14) = 1u;
    reg(timer + 0x10) = 0;
    constexpr auto priorityAddress = 0xe000e400u + (irq & ~3u);
    constexpr auto priorityShift = (irq % 4) * 8;
    reg(priorityAddress) = (reg(priorityAddress) & ~(0xffu << priorityShift)) | (0x80u << priorityShift);
    reg(0xe000e280 + (irq / 32) * 4) = 1u << (irq % 32);
    reg(0xe000e100 + (irq / 32) * 4) = 1u << (irq % 32);
    volume = 100;
    ready = true;
}

void setEnabled(bool enabled) {
    const auto mask = maskInterrupts();
    stopTimer();
    high = enabled;
    pinWrite(gpioA, 10, enabled);
    restoreInterrupts(mask);
}

void setVolume(unsigned percent) {
    volume = percent > 100 ? 100 : percent;
}

void setTone(unsigned hertz) {
    if (!ready || !volume || hertz < 100 || hertz > 4000) {
        setEnabled(false);
        return;
    }
    const auto halfPeriod = (500000 + hertz / 2) / hertz;
    const auto mask = maskInterrupts();
    stopTimer();
    high = true;
    pinWrite(gpioA, 10, true);
    highCounts = halfPeriod * (20 + (volume * 80) / 100) / 100;
    lowCounts = 2 * halfPeriod - highCounts;
    reg(timer + 0x2c) = highCounts - 1;
    reg(timer + 0x24) = 0;
    reg(timer + 0x0c) = update;
    reg(timer) = 5;
    restoreInterrupts(mask);
}

void toggleTone() {
    if (!(reg(timer + 0x10) & update)) return;
    reg(timer + 0x10) = 0;
    high = !high;
    reg(timer + 0x2c) = (high ? highCounts : lowCounts) - 1;
    pinWrite(gpioA, 10, high);
}

}  // namespace h1::hal::buzzer

extern "C" void Timer5_Handler() {
    h1::hal::buzzer::toggleTone();
}


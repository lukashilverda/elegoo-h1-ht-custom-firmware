#include "board.hpp"
#include "hal/heater.h"

extern "C" {
extern std::uint32_t _stack_top;
extern std::uint32_t _data_start;
extern std::uint32_t _data_end;
extern std::uint32_t _data_load;
extern std::uint32_t _bss_start;
extern std::uint32_t _bss_end;
void runFirmware();
[[gnu::weak]] void firmwareTick(std::uint32_t) {}
[[gnu::weak]] void firmwareFault() {}
void SysTick_Handler();
[[noreturn]] void Default_Handler();
void Timer4_Handler();
void Timer5_Handler();
[[noreturn]] void Reset_Handler();
}

namespace {
volatile std::uint32_t ticks;
std::uint32_t clockHz = 8000000;
bool watchdogStarted;

bool waitRegister(std::uint32_t address, std::uint32_t mask, std::uint32_t value) {
    for (unsigned attempt = 0; attempt < 100000; ++attempt) {
        if ((h1::board::reg(address) & mask) == value) return true;
    }
    return false;
}

}

extern "C" [[noreturn]] void Default_Handler() {
    asm volatile("cpsid i" ::: "memory");
    h1::hal::heater::initialize();
    firmwareFault();
    for (;;) {
        asm volatile("wfi");
    }
}

extern "C" void SysTick_Handler() {
    ++ticks;
    firmwareTick(ticks);
}

extern "C" [[gnu::used, noreturn]] void boot() {
    h1::hal::heater::initialize();
    auto* source = &_data_load;
    for (auto* address = &_data_start; address != &_data_end; ++address) {
        *address = *source++;
    }
    for (auto* address = &_bss_start; address != &_bss_end; ++address) {
        *address = 0;
    }
    h1::board::initialize();
    runFirmware();
    Default_Handler();
}

extern "C" [[gnu::naked]] void Reset_Handler() {
    asm volatile(
        "cpsid i\n"
        "ldr r0, =_stack_top\n"
        "msr msp, r0\n"
        "movs r0, #0\n"
        "msr control, r0\n"
        "isb\n"
        "b boot\n"
    );
}

asm(
    ".section .vectors, \"a\", %progbits\n"
    ".word _stack_top, Reset_Handler\n"
    ".rept 13\n.word Default_Handler\n.endr\n"
    ".word SysTick_Handler\n"
    ".rept 50\n.word Default_Handler\n.endr\n"
    ".word Timer4_Handler\n"
    ".rept 3\n.word Default_Handler\n.endr\n"
    ".word Timer5_Handler\n"
    ".rept 9\n.word Default_Handler\n.endr\n"
    ".previous\n"
);

namespace h1::board {

void initialize() {
    constexpr auto config = rcu + 4;
    constexpr std::uint32_t pllEnable = 1u << 24;
    constexpr std::uint32_t pllStable = 1u << 25;

    // Switch off the loader's clock configuration while executing on IRC8M.
    reg(rcu) |= 1u;
    if (!waitRegister(rcu, 2u, 2u)) Default_Handler();
    reg(config) &= ~3u;
    if (!waitRegister(config, 0x0cu, 0)) Default_Handler();

    constexpr auto pmu = 0x40007000u;
    constexpr auto hxtalEnable = 1u << 16;
    constexpr auto hxtalStable = 1u << 17;
    constexpr auto highDriveEnable = 1u << 16;
    constexpr auto highDriveSelect = 1u << 17;
    constexpr auto clockFields = 0x003ffff0u | (1u << 27) | (1u << 28) | (1u << 30);

    const auto selectPll = [&](std::uint32_t frequency, std::uint32_t fields, unsigned latency) {
        reg(rcu) &= ~pllEnable;
        if (!waitRegister(rcu, pllStable, 0)) return false;
        reg(rcu + 0x2c) &= ~((1u << 30) | (1u << 29));
        reg(config) = (reg(config) & ~clockFields) | fields;
        reg(0x40022000) = (reg(0x40022000) & ~7u) | latency;
        reg(0x400220fc) |= 1u;
        if (frequency == 120000000) reg(pmu) |= 3u << 14; // High LDO voltage, before PLL enable.
        reg(rcu) |= pllEnable;
        if (!waitRegister(rcu, pllStable, pllStable)) return false;
        if (frequency == 120000000) {
            reg(pmu) |= highDriveEnable;
            if (!waitRegister(pmu + 4, highDriveEnable, highDriveEnable)) return false;
            reg(pmu) |= highDriveSelect;
            if (!waitRegister(pmu + 4, highDriveSelect, highDriveSelect)) return false;
        }
        reg(config) = (reg(config) & ~3u) | 2u;
        if (!waitRegister(config, 0x0cu, 8u)) {
            reg(config) &= ~3u;
            if (!waitRegister(config, 0x0cu, 0)) Default_Handler();
            return false;
        }
        clockHz = frequency;
        return true;
    };

    reg(rcu) &= ~pllEnable;
    reg(rcu + 0x1c) |= 1u << 28; // PMU clock.
    reg(pmu) &= ~(highDriveSelect | highDriveEnable);
    reg(rcu) &= ~(1u << 18); // Crystal oscillator, no external bypass.
    reg(rcu) |= hxtalEnable;
    // Factory HXTAL / 2 × 30 with /2 for APB1. Both timer buses still run at 120 MHz.
    // Divide the ADC clock by 12 to stay close to the existing 12 MHz sampling clock.
    constexpr std::uint32_t fastFields = 0x08370400u | (1u << 28) | (1u << 14);
    if (!waitRegister(rcu, hxtalStable, hxtalStable) || !selectPll(120000000, fastFields, 3)) {
        reg(pmu) &= ~(highDriveSelect | highDriveEnable);
        // IRC8M / 2 × 12, APB1 and APB2 undivided, ADC at 12 MHz.
        if (!selectPll(48000000, (10u << 18) | (1u << 14), 2)) {
            reg(config) &= ~0x3ff0u;
            reg(rcu) &= ~pllEnable;
        }
    }
    reg(0xe000ed08) = 0x08008000;
    for (unsigned index = 0; index < 3; ++index) {
        reg(0xe000e180 + index * 4) = 0xffffffffu;
        reg(0xe000e280 + index * 4) = 0xffffffffu;
    }
    reg(0xe000ed04) = (1u << 25) | (1u << 27);
    reg(0xe000edfc) |= 1u << 24;
    reg(0xe0001004) = 0;
    reg(0xe0001000) |= 1u;
    // Use the same preemption priority for SysTick and the STEP timer.
    reg(0xe000ed20) = (reg(0xe000ed20) & 0x00ffffffu) | 0x40000000u;
    reg(0xe000e014) = clockHz / 1000 - 1;
    reg(0xe000e018) = 0;
    reg(0xe000e010) = 7;
    asm volatile("dsb\nisb\ncpsie i" ::: "memory");
}

std::uint32_t coreClockHz() {
    return clockHz;
}

bool startWatchdog() {
    constexpr auto watchdog = 0x40003000u;
    reg(rcu + 0x24) |= 1u;
    if (!waitRegister(rcu + 0x24, 2u, 2u)) return false;
    reg(watchdog) = 0x5555;
    reg(watchdog + 4) = 3; // Divide IRC40K by 32.
    reg(watchdog + 8) = 1250;
    if (!waitRegister(watchdog + 12, 3u, 0)) return false;
    reg(watchdog) = 0xaaaa;
    reg(watchdog) = 0xcccc;
    watchdogStarted = true;
    return true;
}

void feedWatchdog() {
    reg(0x40003000) = 0xaaaau;
}

std::uint32_t milliseconds() {
    return ticks;
}

void delay(std::uint32_t ms) {
    const auto start = milliseconds();
    while (milliseconds() - start < ms) {
        // Reload an already-running independent watchdog without enabling it.
        if (!watchdogStarted) feedWatchdog();
        asm volatile("wfi");
    }
}

void delayMicroseconds(std::uint32_t us) {
    const auto start = reg(0xe0001004);
    const auto cycles = us * (clockHz / 1000000);
    while (reg(0xe0001004) - start < cycles) {}
}

}  // namespace h1::board

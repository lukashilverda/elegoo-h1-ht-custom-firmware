#pragma once

#include <cstdint>

namespace h1::board {

inline volatile std::uint32_t& reg(std::uint32_t address) {
    return *reinterpret_cast<volatile std::uint32_t*>(address);
}

inline constexpr std::uint32_t gpioA = 0x40010800;
inline constexpr std::uint32_t gpioB = 0x40010c00;
inline constexpr std::uint32_t gpioC = 0x40011000;
inline constexpr std::uint32_t gpioD = 0x40011400;
inline constexpr std::uint32_t gpioE = 0x40011800;
inline constexpr std::uint32_t rcu = 0x40021000;

inline void pinMode(std::uint32_t port, unsigned pin, std::uint32_t mode) {
    auto& config = reg(port + (pin >= 8 ? 4 : 0));
    const unsigned shift = (pin % 8) * 4;
    config = (config & ~(0x0fu << shift)) | (mode << shift);
}

inline void pinWrite(std::uint32_t port, unsigned pin, bool high) {
    reg(port + (high ? 0x10u : 0x14u)) = 1u << pin;
}

inline bool pinRead(std::uint32_t port, unsigned pin) {
    return (reg(port + 8) & (1u << pin)) != 0;
}

void initialize();
std::uint32_t coreClockHz();
bool startWatchdog();
void feedWatchdog();
std::uint32_t milliseconds();
void delay(std::uint32_t ms);
void delayMicroseconds(std::uint32_t us);

}  // namespace h1::board

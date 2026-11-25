#include "display.h"
#include "../board.hpp"
#include "../panel_sequences.hpp"

namespace h1::board {
namespace {

volatile std::uint16_t& lcdCommand = *reinterpret_cast<volatile std::uint16_t*>(0x60000000);
volatile std::uint16_t& lcdData = *reinterpret_cast<volatile std::uint16_t*>(0x60020000);

void window(unsigned x0, unsigned y0, unsigned x1, unsigned y1) {
    lcdCommand = 0x2a;
    lcdData = static_cast<std::uint16_t>(x0 >> 8);
    lcdData = static_cast<std::uint16_t>(x0 & 0xff);
    lcdData = static_cast<std::uint16_t>(x1 >> 8);
    lcdData = static_cast<std::uint16_t>(x1 & 0xff);
    lcdCommand = 0x2b;
    lcdData = static_cast<std::uint16_t>(y0 >> 8);
    lcdData = static_cast<std::uint16_t>(y0 & 0xff);
    lcdData = static_cast<std::uint16_t>(y1 >> 8);
    lcdData = static_cast<std::uint16_t>(y1 & 0xff);
    lcdCommand = 0x2c;
}

template<unsigned length>
void sendSequence(const panel::Step (&steps)[length]) {
    for (const auto& step : steps) {
        lcdCommand = step.command;
        for (unsigned index = 0; index < step.length; ++index) {
            lcdData = step.parameters[index];
        }
        if (step.delayMs != 0) {
            delay(step.delayMs);
        }
    }
}

std::uint16_t readPanelId() {
    lcdCommand = 0xd3;
    for (unsigned count = 0; count < 2; ++count) {
        [[maybe_unused]] const auto discarded = lcdData;
    }
    auto high = lcdData & 0xffu;
    auto low = lcdData & 0xffu;
    if ((high | low) == 0) {
        lcdCommand = 4;
        delayMicroseconds(10);
        for (unsigned count = 0; count < 6; ++count) {
            [[maybe_unused]] const auto discarded = lcdData;
        }
        high = lcdData & 0xffu;
        low = lcdData & 0xffu;
    }
    return static_cast<std::uint16_t>((high << 8) | low);
}

}  // namespace

std::uint16_t initializeDisplay() {
    reg(rcu + 0x14) |= 1u << 8;
    reg(rcu + 0x18) |= (1u << 5) | (1u << 6);

    // EXMC 16-bit data, address A16, and bus control pins on ports D and E.
    constexpr unsigned portDPins[]{0, 1, 4, 5, 7, 8, 9, 10, 11, 14, 15};
    for (const auto pin : portDPins) {
        pinMode(gpioD, pin, 0x0b);
    }
    for (unsigned pin = 7; pin <= 15; ++pin) {
        pinMode(gpioE, pin, 0x0b);
    }
    const bool at48MHz = coreClockHz() == 48000000;
    const std::uint32_t config = at48MHz ? 0x5010u : 0x1010u;
    reg(0xa0000000) = config;
    // Use the factory EXMC setup with the factory 120 MHz clock.
    reg(0xa0000004) = at48MHz ? 0x0e03u : 0x0201u;
    reg(0xa0000104) = at48MHz ? 0x0603u : 0x0fffffffu;
    reg(0xa0000000) = config | 1u;

    pinWrite(gpioD, 13, false);
    pinMode(gpioD, 13, 3);
    pinWrite(gpioD, 6, true);
    pinMode(gpioD, 6, 3);
    delay(1);
    pinWrite(gpioD, 6, false);
    delay(10);
    pinWrite(gpioD, 6, true);
    delay(120);

    const auto id = readPanelId();
    if (id == 0x7796) {
        sendSequence(panel::st7796);
    } else if (id == 0x9488) {
        sendSequence(panel::ili9488);
    } else {
        sendSequence(panel::fallback);
    }

    fill(0, 0, screenWidth, screenHeight, 0);
    setBacklight(true);
    return id;
}

void setBacklight(bool enabled) {
    pinWrite(gpioD, 13, enabled);
}

void fill(unsigned x, unsigned y, unsigned width, unsigned height, std::uint16_t color) {
    if (width == 0 || height == 0 || x >= screenWidth || y >= screenHeight) {
        return;
    }
    if (width > screenWidth - x) width = screenWidth - x;
    if (height > screenHeight - y) height = screenHeight - y;
    window(x, y, x + width - 1, y + height - 1);
    for (unsigned pixel = 0; pixel < width * height; ++pixel) {
        lcdData = color;
    }
}

void writePixels(unsigned x, unsigned y, unsigned width, unsigned height, const void* pixels) {
    if (width == 0 || height == 0 || x >= screenWidth || y >= screenHeight ||
        width > screenWidth - x || height > screenHeight - y) return;
    window(x, y, x + width - 1, y + height - 1);
    const auto* data = static_cast<const unsigned char*>(pixels);
    auto next = [&data]() {
        std::uint16_t pixel;
        __builtin_memcpy(&pixel, data, sizeof pixel);
        data += sizeof pixel;
        lcdData = pixel;
    };
    unsigned count = width * height;
    for (; count >= 8; count -= 8) {
        next(); next(); next(); next();
        next(); next(); next(); next();
    }
    for (; count != 0; --count) next();
}

void writeRow(unsigned x, unsigned y, const std::uint16_t* colors, unsigned count,
              unsigned repeat) {
    if (x >= screenWidth || y >= screenHeight || count == 0 || repeat == 0 ||
        count > (screenWidth - x) / repeat) return;

    if (repeat == 1) {
        writePixels(x, y, count, 1, colors);
        return;
    }
    window(x, y, x + count * repeat - 1, y);
    for (unsigned index = 0; index < count; ++index) {
        for (unsigned pixel = 0; pixel < repeat; ++pixel) {
            lcdData = colors[index];
        }
    }
}

}  // namespace h1::board

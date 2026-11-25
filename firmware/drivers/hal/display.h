#pragma once

#include <cstdint>

namespace h1::board {

inline constexpr unsigned screenWidth = 480;
inline constexpr unsigned screenHeight = 320;

std::uint16_t initializeDisplay();
void fill(unsigned x, unsigned y, unsigned width, unsigned height, std::uint16_t color);
// Write a scanline of RGB565 colors, optionally repeating each pixel.
void writeRow(unsigned x, unsigned y, const std::uint16_t* colors, unsigned count,
              unsigned repeat = 1);
// Packed native RGB565 pixels, with no row padding.
void writePixels(unsigned x, unsigned y, unsigned width, unsigned height, const void* pixels);
void setBacklight(bool enabled);

}  // namespace h1::board

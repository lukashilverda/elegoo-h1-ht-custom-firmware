#pragma once

#include "../i2c.hpp"

namespace h1::board {

struct TouchPoint { unsigned x; unsigned y; };
enum class TouchKind : std::uint8_t { unknown, ft = 0x38, goodix = 0x5d, hynitron = 0x5a, cst = 0x15 };

class Touchscreen {
public:
    explicit Touchscreen(I2cBus& bus);
    TouchKind kind() const { return kind_; }
    bool read(TouchPoint& point);

private:
    bool exchange(std::uint8_t address, const std::uint8_t* regAddress, std::size_t regSize,
                  std::uint8_t* result, std::size_t resultSize);
    I2cBus& bus_;
    TouchKind kind_ = TouchKind::unknown;
};

}  // namespace h1::board

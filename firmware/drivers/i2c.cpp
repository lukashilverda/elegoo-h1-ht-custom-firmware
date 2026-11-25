#include "i2c.hpp"
#include "board.hpp"

namespace h1::board {

I2cBus::I2cBus(unsigned clockPin, unsigned dataPin)
    : clockPin_(clockPin), dataPin_(dataPin) {
    reg(rcu + 0x18) |= 1u << 3;
    pinWrite(gpioB, clockPin_, true);
    pinWrite(gpioB, dataPin_, true);
    pinMode(gpioB, clockPin_, 0x6);
    pinMode(gpioB, dataPin_, 0x6);
}

bool I2cBus::raiseClock() {
    pinWrite(gpioB, clockPin_, true);
    for (unsigned attempt = 0; attempt < 250; ++attempt) {
        if (pinRead(gpioB, clockPin_)) {
            delayMicroseconds(5);
            return true;
        }
        delayMicroseconds(2);
    }
    return false;
}

bool I2cBus::start() {
    pinWrite(gpioB, dataPin_, true);
    if (!raiseClock() || !pinRead(gpioB, dataPin_)) {
        return false;
    }
    pinWrite(gpioB, dataPin_, false);
    delayMicroseconds(5);
    pinWrite(gpioB, clockPin_, false);
    return true;
}

void I2cBus::stop() {
    pinWrite(gpioB, dataPin_, false);
    delayMicroseconds(5);
    raiseClock();
    pinWrite(gpioB, dataPin_, true);
    delayMicroseconds(5);
}

bool I2cBus::send(std::uint8_t byte) {
    for (unsigned bit = 0x80; bit != 0; bit >>= 1) {
        pinWrite(gpioB, dataPin_, (byte & bit) != 0);
        delayMicroseconds(5);
        if (!raiseClock()) {
            return false;
        }
        pinWrite(gpioB, clockPin_, false);
    }
    pinWrite(gpioB, dataPin_, true);
    delayMicroseconds(5);
    const bool acknowledged = raiseClock() && !pinRead(gpioB, dataPin_);
    pinWrite(gpioB, clockPin_, false);
    return acknowledged;
}

bool I2cBus::receive(std::uint8_t& byte, bool acknowledge) {
    byte = 0;
    pinWrite(gpioB, dataPin_, true);
    for (unsigned bit = 0x80; bit != 0; bit >>= 1) {
        delayMicroseconds(5);
        if (!raiseClock()) {
            return false;
        }
        if (pinRead(gpioB, dataPin_)) {
            byte |= static_cast<std::uint8_t>(bit);
        }
        pinWrite(gpioB, clockPin_, false);
    }
    pinWrite(gpioB, dataPin_, !acknowledge);
    delayMicroseconds(5);
    const bool completed = raiseClock();
    pinWrite(gpioB, clockPin_, false);
    pinWrite(gpioB, dataPin_, true);
    return completed;
}

bool I2cBus::write(std::uint8_t address, const std::uint8_t* data, std::size_t size) {
    bool success = start() && send(static_cast<std::uint8_t>(address << 1));
    for (std::size_t index = 0; success && index < size; ++index) {
        success = send(data[index]);
    }
    stop();
    return success;
}

bool I2cBus::read(std::uint8_t address, std::uint8_t* data, std::size_t size) {
    bool success = start() && send(static_cast<std::uint8_t>((address << 1) | 1u));
    for (std::size_t index = 0; success && index < size; ++index) {
        success = receive(data[index], index + 1 < size);
    }
    stop();
    return success;
}

}  // namespace h1::board

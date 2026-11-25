#pragma once

#include "humidity_sensor.hpp"

namespace h1::board {

class I2cBus final : public HumidityBus {
public:
    I2cBus(unsigned clockPin, unsigned dataPin);
    bool write(std::uint8_t address, const std::uint8_t* data, std::size_t size) override;
    bool read(std::uint8_t address, std::uint8_t* data, std::size_t size) override;

private:
    bool start();
    void stop();
    bool raiseClock();
    bool send(std::uint8_t byte);
    bool receive(std::uint8_t& byte, bool acknowledge);

    unsigned clockPin_;
    unsigned dataPin_;
};

}  // namespace h1::board

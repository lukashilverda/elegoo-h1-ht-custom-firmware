#include "touchscreen.h"
#include "../board.hpp"

namespace h1::board {

Touchscreen::Touchscreen(I2cBus& bus) : bus_(bus) {
    pinWrite(gpioB, 12, false);
    pinMode(gpioB, 12, 0x3);
    pinMode(gpioB, 13, 0x4);
    delay(20);
    pinWrite(gpioB, 12, true);
    delay(200);

    std::uint8_t id[4]{};
    const std::uint8_t ftId[]{0xa8};
    const std::uint8_t goodixId[]{0x81, 0x40};
    if (exchange(0x38, ftId, 1, id, 1) && id[0] == 0x11) {
        kind_ = TouchKind::ft;
    }
    if (exchange(0x5d, goodixId, 2, id, 4)
        && id[0] == '9' && id[1] == '1' && id[2] == '1') {
        kind_ = TouchKind::goodix;
    }
    if (bus_.read(0x5a, id, 1)) {
        kind_ = TouchKind::hynitron;
    }
    if (bus_.read(0x15, id, 1)) {
        kind_ = TouchKind::cst;
    }
}

bool Touchscreen::exchange(std::uint8_t address, const std::uint8_t* regAddress, std::size_t regSize,
                           std::uint8_t* result, std::size_t resultSize) {
    return bus_.write(address, regAddress, regSize) && bus_.read(address, result, resultSize);
}

bool Touchscreen::read(TouchPoint& point) {
    std::uint8_t data[7]{};
    unsigned nativeX = 0;
    unsigned nativeY = 0;
    switch (kind_) {
        case TouchKind::ft: {
            const std::uint8_t address[]{0x02};
            if (!exchange(0x38, address, 1, data, 5) || (data[0] & 0x0fu) == 0
                || (data[1] & 0xc0u) == 0x40u) {
                return false;
            }
            nativeX = ((data[1] & 0x0fu) << 8) | data[2];
            nativeY = ((data[3] & 0x0fu) << 8) | data[4];
            break;
        }
        case TouchKind::goodix: {
            const std::uint8_t address[]{0x81, 0x4e};
            const std::uint8_t clear[]{0x81, 0x4e, 0};
            if (!exchange(0x5d, address, 2, data, 6) || (data[0] & 0x80u) == 0) {
                return false;
            }
            if (!bus_.write(0x5d, clear, 3) || (data[0] & 0x0fu) == 0) {
                return false;
            }
            nativeX = (static_cast<unsigned>(data[3]) << 8) | data[2];
            nativeY = (static_cast<unsigned>(data[5]) << 8) | data[4];
            break;
        }
        case TouchKind::hynitron: {
            const std::uint8_t address[]{0xd0, 0};
            const std::uint8_t clear[]{0xd0, 0, 0xab};
            if (!exchange(0x5a, address, 2, data, 7) || data[6] != 0xab) {
                return false;
            }
            if (!bus_.write(0x5a, clear, 3) || (data[5] & 0x7fu) == 0
                || (data[0] & 0x0fu) != 6) {
                return false;
            }
            nativeX = (static_cast<unsigned>(data[1]) << 4) | (data[3] >> 4);
            nativeY = (static_cast<unsigned>(data[2]) << 4) | (data[3] & 0x0fu);
            break;
        }
        case TouchKind::cst: {
            const std::uint8_t address[]{0};
            if (!exchange(0x15, address, 1, data, 7) || (data[2] & 0x0fu) == 0
                || (data[3] & 0xc0u) == 0x40u) {
                return false;
            }
            nativeX = ((data[3] & 0x0fu) << 8) | data[4];
            nativeY = ((data[5] & 0x0fu) << 8) | data[6];
            break;
        }
        case TouchKind::unknown:
            return false;
    }
    if (nativeX >= 320 || nativeY >= 480) {
        return false;
    }
    point = {479 - nativeY, nativeX};
    return true;
}

}  // namespace h1::board

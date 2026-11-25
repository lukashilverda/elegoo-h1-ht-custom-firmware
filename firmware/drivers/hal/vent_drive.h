#pragma once

#include <cstdint>

namespace h1::hal {

enum class VentDirection : std::uint8_t { stop, towardClosed, towardOpen };

}  // namespace h1::hal

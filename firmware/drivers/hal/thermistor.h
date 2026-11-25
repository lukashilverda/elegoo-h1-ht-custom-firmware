#pragma once

#include "../sensors.hpp"

namespace h1::hal::thermistor {

// Initialize after board::initialize() so calibration can use its timebase.
bool initialize();
ThermistorReading read();

}  // namespace h1::hal::thermistor

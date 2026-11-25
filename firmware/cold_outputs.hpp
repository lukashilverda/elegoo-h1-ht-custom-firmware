#pragma once

#include "cycle.hpp"
#include "sensors.hpp"

namespace h1::cold::hardware {

void initialize();
void publish(Outputs outputs, std::uint32_t now, bool restart = false, std::uint32_t heatForMs = 0, bool lowerWhileWarming = false);
bool takeFanPulse();
bool takeLidOpen();
bool airflowReady();
bool heaterActive();
unsigned ventsReached();
void inhibitHeat();
unsigned ventFaults();
void tick(std::uint32_t now);
void emergencyOff();

}  // namespace h1::cold::hardware


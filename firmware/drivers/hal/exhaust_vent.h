#pragma once

#include "vent_drive.h"

namespace h1::hal::exhaustVent {

void initialize();
void drive(VentDirection direction);
bool feedbackHigh();

}  // namespace h1::hal::exhaustVent

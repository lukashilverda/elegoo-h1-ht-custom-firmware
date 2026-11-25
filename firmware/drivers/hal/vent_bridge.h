#pragma once

namespace h1::hal::ventBridge {

// PC10 and PC11 are shared by both vent channels.
void initialize();
void setEnabled(bool enabled);
bool statusHigh();

}  // namespace h1::hal::ventBridge

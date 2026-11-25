#pragma once

namespace h1::hal::fan {

// TIMER0 PWM at 25 kHz with the board's 8 MHz clock. Starts at 0%.
void initialize();
void setDutyPercent(unsigned percent);
// Reports whether at least one FG edge was captured, then clears the flag.
bool takePulse();

}  // namespace h1::hal::fan

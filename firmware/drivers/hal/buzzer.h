#pragma once

namespace h1::hal::buzzer {

// PA10 supports a steady piep request or a square-wave tone on passive hardware.
void initialize();
void setEnabled(bool enabled);
void setVolume(unsigned percent);
void setTone(unsigned hertz);

}  // namespace h1::hal::buzzer

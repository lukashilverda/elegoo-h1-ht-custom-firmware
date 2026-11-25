#pragma once

namespace h1::hal::heater {

// Can be called before the C++ runtime is initialized. PB1 starts low.
void initialize();
// Direct PB1 control. The application owns all interlocks and regulation.
void setEnabled(bool enabled);

}  // namespace h1::hal::heater

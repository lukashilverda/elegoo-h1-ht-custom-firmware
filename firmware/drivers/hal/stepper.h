#pragma once

namespace h1::hal::stepper {

// PC1 (active-low enable), PC2 (direction), and PC3 (step).
void initialize();
void setEnabled(bool enabled);
void setDirectionHigh(bool high);
void setStepHigh(bool high);
// Request the timed pulse train. Called with interrupts masked or from SysTick.
void setRotating(bool rotating);
void emergencyStop();

}  // namespace h1::hal::stepper

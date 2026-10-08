// Motor power and board safety: owns the stepper ENABLE pins, the heater pins and the fan.
// Step pulses come from ToneGenerator; this module only decides which drivers are powered.
#pragma once
#include <Arduino.h>

// Puts every output in a safe state: heaters off, all stepper drivers disabled, tone timers ready.
// Must be the first thing called in setup().
void motorsInitSafe();

// The one function that releases the motors: stops all step pulses, then disables every driver.
// Every stop, error and timeout path calls it.
void disableAllMotors();

void enableMotor(uint8_t motor);
void disableMotor(uint8_t motor);  // stops its step pulses first

// Bit n set = motor n driver enabled.
uint8_t motorEnableMask();

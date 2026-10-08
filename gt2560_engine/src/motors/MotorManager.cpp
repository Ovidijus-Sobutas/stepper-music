// Motor power and board safety. See MotorManager.h.
#include "MotorManager.h"
#include "ToneGenerator.h"
#include "../config.h"

static const uint8_t STEP_PINS[NUM_MOTORS] = MOTOR_STEP_PINS;
static const uint8_t DIR_PINS[NUM_MOTORS] = MOTOR_DIR_PINS;
static const uint8_t ENABLE_PINS[NUM_MOTORS] = MOTOR_ENABLE_PINS;  // active LOW
static const uint8_t HEATER_PIN_LIST[] = HEATER_PINS;

static uint8_t enabledMotorsMask = 0;

void motorsInitSafe() {
  for (uint8_t pin : HEATER_PIN_LIST) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
  }
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    digitalWrite(ENABLE_PINS[i], HIGH);  // set level before switching to output: no LOW glitch
    pinMode(ENABLE_PINS[i], OUTPUT);
    digitalWrite(STEP_PINS[i], LOW);
    pinMode(STEP_PINS[i], OUTPUT);
    digitalWrite(DIR_PINS[i], LOW);      // one direction; the motors vibrate/turn to make sound
    pinMode(DIR_PINS[i], OUTPUT);
  }
  enabledMotorsMask = 0;
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, HIGH);
  toneInit();
}

void disableAllMotors() {
  toneStopAll();
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    digitalWrite(STEP_PINS[i], LOW);
    digitalWrite(ENABLE_PINS[i], HIGH);
  }
  enabledMotorsMask = 0;
}

void enableMotor(uint8_t motor) {
  if (motor >= NUM_MOTORS) return;
  digitalWrite(ENABLE_PINS[motor], LOW);
  enabledMotorsMask |= _BV(motor);
}

void disableMotor(uint8_t motor) {
  if (motor >= NUM_MOTORS) return;
  toneStop(motor);
  digitalWrite(ENABLE_PINS[motor], HIGH);
  enabledMotorsMask &= ~_BV(motor);
}

uint8_t motorEnableMask() { return enabledMotorsMask; }

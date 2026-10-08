// Makes a stepper "sing" a MIDI note by stepping it at the note frequency.
//
// Timing: Timer1 and Timer3 run freely at 2 MHz; each motor has its own output-compare channel
// (1A, 1B, 1C, 3A, 3B) whose interrupt sends its next step, so all five motors get the same
// precision. Only the STEP pins are driven here; the ENABLE pins belong to MotorManager.
#pragma once
#include <Arduino.h>

enum ToneMode : uint8_t {
  TONE_EVEN = 0,  // continuous rotation: one full step per note period, microsteps evenly spaced
  TONE_PUSH = 1,  // one push of level * 2 microsteps per note period (stepper_music style)
};

// Drive tuning, changed by the CFG command. Changes apply to the next note started.
struct ToneSettings {
  uint8_t mode;          // ToneMode
  uint8_t level;         // push mode only: push size = level * 2 microsteps (capped at MAX_PULSE_BURST)
  uint8_t microsteps;    // driver microstepping (1, 2, 4, 8, 16), for the stall guard and even mode
  uint16_t spacingUs;    // push mode: time between the steps of one push
  uint8_t rampPeriods;   // push mode soft start length in note periods (0 = off)
  uint16_t maxFullSteps; // stall guard, full steps per second (0 = off)
  uint16_t stepBudget;   // CPU guard, step pulses per second per motor (0 = off)
  uint8_t group;         // even mode grain: microsteps sent together (1 smooth .. microsteps = full step)
  int8_t octave;         // shift every note by this many octaves (-2..+2)
};

void toneInit();  // starts Timer1/Timer3; called once by motorsInitSafe()
void toneStart(uint8_t motor, uint8_t note);  // also changes the note of a motor that is playing
void toneStop(uint8_t motor);
void toneStopAll();
uint16_t toneLateSteps();  // steps dropped because the CPU was behind (should stay 0)

ToneSettings &toneSettings();
void toneSetLevel(uint8_t level);  // clamped to 1..MAX_LEVEL
uint8_t toneLevel();

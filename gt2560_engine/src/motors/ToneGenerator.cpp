// Tone generator: turns "motor m plays MIDI note n" into timed STEP pulses, one output-compare
// interrupt per step (or group of steps). See ToneGenerator.h for the timer layout.
#include "ToneGenerator.h"
#include <math.h>
#include "../config.h"

// Timer ticks are 0.5 us (16 MHz / prescaler 8). Both timers run freely (normal mode, 0..65535);
// each channel's compare register holds the time of that motor's next step.
static const uint32_t TICKS_PER_SECOND = 2000000UL;
static volatile uint16_t *const COMPARE_REG[NUM_MOTORS] = {&OCR1A, &OCR1B, &OCR1C, &OCR3A, &OCR3B};
static volatile uint16_t *const COUNTER_REG[NUM_MOTORS] = {&TCNT1, &TCNT1, &TCNT1, &TCNT3, &TCNT3};
static volatile uint8_t *const INTERRUPT_MASK_REG[NUM_MOTORS] = {&TIMSK1, &TIMSK1, &TIMSK1, &TIMSK3, &TIMSK3};
static volatile uint8_t *const INTERRUPT_FLAG_REG[NUM_MOTORS] = {&TIFR1, &TIFR1, &TIFR1, &TIFR3, &TIFR3};
// Bit of the channel in its TIMSKn (enable) register; the same bit position is its flag in TIFRn.
static const uint8_t COMPARE_BIT[NUM_MOTORS] = {_BV(OCIE1A), _BV(OCIE1B), _BV(OCIE1C), _BV(OCIE3A), _BV(OCIE3B)};

static const uint8_t STEP_PINS[NUM_MOTORS] = MOTOR_STEP_PINS;
static const uint16_t MIN_AHEAD_TICKS = 24;  // never schedule closer than 12 us ahead
static const uint8_t MIDI_NOTE_COUNT = 128;

// Per-motor state shared with that motor's ISR. Written by the main loop only with interrupts off.
struct Channel {
  volatile bool active;
  uint16_t periodTicks;      // note period (push to push)
  uint16_t stepSpacingTicks; // between the steps of a push
  uint8_t pushTarget;        // full push size (interrupts per push) after the caps
  uint8_t rampLeft;          // soft-start pushes still to come
  uint8_t rampTotal;
  uint8_t stepsLeft;         // interrupts still to come in the current push
  uint16_t pushStartTicks;   // time the current push started
  uint8_t pulsesPerStep;     // step pulses sent together per interrupt (even mode "grain")
  volatile uint8_t *stepPort;
  uint8_t stepPinMask;
};

static Channel channels[NUM_MOTORS];
static uint16_t notePeriodTicks[MIDI_NOTE_COUNT];
static ToneSettings settings = {DEFAULT_TONE_MODE, DEFAULT_LEVEL,         DEFAULT_MICROSTEPS,
                                DEFAULT_STEP_SPACING_US, DEFAULT_RAMP_PERIODS, DEFAULT_MAX_FULL_STEPS,
                                DEFAULT_STEP_BUDGET,     DEFAULT_GROUP,     DEFAULT_OCTAVE};
static volatile uint16_t lateSteps = 0;  // steps dropped because the CPU was behind

// Size of the next push: grows linearly from 1 to pushTarget during the soft start.
static inline uint8_t currentPushSize(const Channel &c) {
  if (!c.rampLeft || !c.rampTotal) return c.pushTarget;
  return 1 + (uint16_t)(c.pushTarget - 1) * (c.rampTotal - c.rampLeft) / c.rampTotal;
}

// Schedule the compare for time t, but never in the past (a late interrupt would otherwise
// wait for the timer to wrap around: a 33 ms gap).
static inline void scheduleStepAt(uint8_t motor, uint16_t t) {
  uint16_t now = *COUNTER_REG[motor];
  if ((int16_t)(t - now) < (int16_t)MIN_AHEAD_TICKS) t = now + MIN_AHEAD_TICKS;
  *COMPARE_REG[motor] = t;
}

// One step (or one group of steps) per interrupt. Interrupts are off inside the ISR, so the port
// writes are safe. A group of g pulses ~3 us apart moves the rotor g microsteps at once, which
// sounds like coarser stepping (g = microsteps: full step).
static inline void serviceChannel(uint8_t motor) {
  Channel &c = channels[motor];
  for (uint8_t i = c.pulsesPerStep; i; i--) {
    *c.stepPort |= c.stepPinMask;
    __builtin_avr_delay_cycles(STEP_PULSE_US * 16 * 3 / 4);  // ~1.5 us high (plus write overhead)
    *c.stepPort &= (uint8_t)~c.stepPinMask;
    if (i > 1) __builtin_avr_delay_cycles(20);               // >= 1 us low before the next pulse
  }
  if (c.stepsLeft > 1) {  // more steps in this push
    uint16_t next = *COMPARE_REG[motor] + c.stepSpacingTicks;
    // Running late (all motors pushing at once): end this push early instead of catching up.
    // Catching up would keep the CPU busy with step interrupts and starve the main loop.
    if ((int16_t)(next - *COUNTER_REG[motor]) >= (int16_t)MIN_AHEAD_TICKS) {
      c.stepsLeft--;
      *COMPARE_REG[motor] = next;
      return;
    }
    lateSteps++;
  }
  // Push done: the next one starts one note period after this one started.
  c.pushStartTicks += c.periodTicks;
  if (c.rampLeft) c.rampLeft--;
  c.stepsLeft = currentPushSize(c);
  scheduleStepAt(motor, c.pushStartTicks);
}

ISR(TIMER1_COMPA_vect) { serviceChannel(0); }
ISR(TIMER1_COMPB_vect) { serviceChannel(1); }
ISR(TIMER1_COMPC_vect) { serviceChannel(2); }
ISR(TIMER3_COMPA_vect) { serviceChannel(3); }
ISR(TIMER3_COMPB_vect) { serviceChannel(4); }

// Equal temperament, A4 (note 69) = 440 Hz, clamped to NOTE_MIN_HZ..NOTE_MAX_HZ.
static void buildNotePeriodTable() {
  for (int n = 0; n < MIDI_NOTE_COUNT; n++) {
    float hz = 440.0f * powf(2.0f, (n - 69) / 12.0f);
    hz = constrain(hz, (float)NOTE_MIN_HZ, (float)NOTE_MAX_HZ);
    notePeriodTicks[n] = (uint16_t)constrain((float)TICKS_PER_SECOND / hz + 0.5f, 100.0f, 65535.0f);
  }
}

void toneInit() {
  for (uint8_t m = 0; m < NUM_MOTORS; m++) {
    channels[m].active = false;
    channels[m].stepPort = portOutputRegister(digitalPinToPort(STEP_PINS[m]));
    channels[m].stepPinMask = digitalPinToBitMask(STEP_PINS[m]);
  }
  buildNotePeriodTable();
  noInterrupts();
  // Normal mode, prescaler 8, no compare outputs on pins (COM bits 0), all compare interrupts off.
  TCCR1A = 0; TCCR1B = _BV(CS11); TCCR1C = 0; TIMSK1 = 0;
  TCCR3A = 0; TCCR3B = _BV(CS31); TCCR3C = 0; TIMSK3 = 0;
  interrupts();
}

static uint16_t pushSpacingTicks() {
  uint16_t t = settings.spacingUs * 2;
  return t < MIN_AHEAD_TICKS ? MIN_AHEAD_TICKS : t;
}

// Push mode: push size for a note at the current settings, after the stall guard, the CPU guard
// and the period limit.
static uint8_t pushSizeFor(uint16_t period, uint16_t spacing) {
  uint16_t t = (uint16_t)settings.level * 2;
  if (t > MAX_PULSE_BURST) t = MAX_PULSE_BURST;
  if (settings.maxFullSteps) {
    // average speed = t microsteps per period = t * f / microsteps full steps per second
    uint32_t limit = (uint32_t)settings.maxFullSteps * settings.microsteps * period / TICKS_PER_SECOND;
    if (t > limit) t = limit;
  }
  if (settings.stepBudget) {
    // CPU guard: t * f step interrupts per second for this motor
    uint32_t limit = (uint32_t)settings.stepBudget * period / TICKS_PER_SECOND;
    if (t > limit) t = limit;
  }
  uint16_t fit = period / spacing;  // all steps of a push must fit in one period
  if (fit) fit--;
  if (t > fit) t = fit;
  return t < 1 ? 1 : (uint8_t)t;
}

// Even mode: the motor turns continuously at one full step per note period, so the step rate
// heard is the note itself (as in jzkmath's Arduino MIDI stepper instrument, which runs the
// drivers in full-step mode with one step per period). With microstepping, one full step is
// `microsteps` evenly spaced steps. A note that would exceed the step budget is played an
// octave lower instead of being cut short.
// The grain: microsteps sent together, a power of two that divides the microstepping.
static uint8_t evenModePulsesPerStep() {
  uint8_t g = settings.group ? settings.group : 1;
  while (g > 1 && (g > settings.microsteps || settings.microsteps % g)) g >>= 1;
  return g;
}

// Even mode: `microsteps` steps per note period, sent as microsteps / group interrupts of
// `group` pulses each, evenly spaced over the period.
static void planEvenMode(uint16_t notePeriod, uint16_t &period, uint16_t &spacing, uint8_t &target,
                         uint8_t &group) {
  uint8_t k = settings.microsteps;
  group = evenModePulsesPerStep();
  uint32_t p = notePeriod;
  if (settings.stepBudget)  // drop octaves; p < 32768 keeps the doubled period within 16 bits
    while (p < 32768 && (uint32_t)k * TICKS_PER_SECOND / p > settings.stepBudget) p *= 2;
  period = (uint16_t)p;
  target = k / group;
  spacing = period / target;
  if (spacing < MIN_AHEAD_TICKS) spacing = MIN_AHEAD_TICKS;
}

// Style octave shift, folded back by whole octaves into the MIDI range 0..127.
static uint8_t applyOctaveShift(uint8_t note) {
  int shifted = (int)note + 12 * settings.octave;
  while (shifted > 127) shifted -= 12;
  while (shifted < 0) shifted += 12;
  return (uint8_t)shifted;
}

void toneStart(uint8_t motor, uint8_t note) {
  if (motor >= NUM_MOTORS || note > 127) return;
  note = applyOctaveShift(note);
  Channel &c = channels[motor];
  uint16_t period = notePeriodTicks[note];
  uint16_t spacing;
  uint8_t target, group = 1;
  bool even = settings.mode == TONE_EVEN;
  if (even) {
    planEvenMode(notePeriodTicks[note], period, spacing, target, group);
  } else {
    spacing = pushSpacingTicks();
    target = pushSizeFor(period, spacing);
  }

  noInterrupts();
  bool wasActive = c.active;
  c.periodTicks = period;
  c.stepSpacingTicks = spacing;
  c.pushTarget = target;
  c.pulsesPerStep = group;
  if (!wasActive) {
    // Soft start from standstill (push mode). A motor that is already turning just changes speed.
    c.rampTotal = even ? 0 : settings.rampPeriods;
    c.rampLeft = c.rampTotal;
    c.stepsLeft = currentPushSize(c);
    c.pushStartTicks = *COUNTER_REG[motor] + MIN_AHEAD_TICKS;
    *COMPARE_REG[motor] = c.pushStartTicks;
    *INTERRUPT_FLAG_REG[motor] = COMPARE_BIT[motor];  // clear a stale compare flag (writing 1 clears it)
    *INTERRUPT_MASK_REG[motor] |= COMPARE_BIT[motor];
    c.active = true;
  } else if (c.rampLeft == 0) {
    c.rampTotal = 0;
  }
  interrupts();
}

void toneStop(uint8_t motor) {
  if (motor >= NUM_MOTORS) return;
  noInterrupts();
  *INTERRUPT_MASK_REG[motor] &= (uint8_t)~COMPARE_BIT[motor];
  *channels[motor].stepPort &= (uint8_t)~channels[motor].stepPinMask;  // leave STEP low
  channels[motor].active = false;
  interrupts();
}

void toneStopAll() {
  for (uint8_t m = 0; m < NUM_MOTORS; m++) toneStop(m);
}

uint16_t toneLateSteps() {
  noInterrupts();  // 16-bit value also written by the ISRs: read it atomically
  uint16_t n = lateSteps;
  interrupts();
  return n;
}

ToneSettings &toneSettings() { return settings; }

void toneSetLevel(uint8_t level) { settings.level = constrain(level, 1, MAX_LEVEL); }

uint8_t toneLevel() { return settings.level; }

// GT2560 Rev A music engine: every hardware, protocol and tuning setting in one place.
// Pins verified against Marlin pins_GT2560_REV_A.h and the stepper_music project.
#pragma once

#define FW_VERSION "0.6.0"  // reported in BOOT and VER replies and on the USB log

// ---------------------------------------------------------------------------------------------
// Serial ports and the link to the ESP8266 (protocol: docs/PLAN.md 5.3)
// ---------------------------------------------------------------------------------------------
#define USB_BAUD 115200   // Serial (USB / FTDI): debug log only
#define LINK_BAUD 57600   // Serial1 (TX1 = D18, RX1 = D19) to the ESP8266; must match the ESP side
// ms without any valid frame before the GT2560 gives up: motors off, state IDLE. Must be longer
// than the ESP web server's worst-case blocking send (5 s on a weak Wi-Fi link), which otherwise
// stopped songs (tested 2026-10-01).
#define LINK_TIMEOUT_MS 6000
// ms: the same seq + type arriving again within this time is a retry of a lost reply, so only
// the stored reply is resent and the command does not run twice.
#define DUPLICATE_WINDOW_MS 1000
#define STATUS_LOG_MS 10000  // ms between status lines on the USB log (not while playing)

// ---------------------------------------------------------------------------------------------
// Steppers: X, Y, Z, E0, E1 (index 0..4 = motor number in the protocol).
// ENABLE is active LOW (HIGH = driver off, motor free).
// ---------------------------------------------------------------------------------------------
#define NUM_MOTORS 5
#define MOTOR_STEP_PINS   {25, 31, 37, 43, 49}
#define MOTOR_DIR_PINS    {23, 33, 39, 45, 47}
#define MOTOR_ENABLE_PINS {27, 29, 35, 41, 48}

// ---------------------------------------------------------------------------------------------
// Tone generation (ToneGenerator.cpp). The settings marked [CFG] are only start values: the
// CFG command changes them at run time (see EspLink.cpp), and "CFG default" restores them.
//
// Push mode: every note period the motor gets one "push" of level * 2 microsteps, so the pitch is
// the note frequency and the push size is the loudness.
//  - The steps of a push are spread STEP_SPACING_US apart. The stepper_music player sent them ~3 us
//    apart: an instant jump the rotor cannot follow, which vibrated and stalled at level 7-10.
//  - A new note starts with small pushes that grow over RAMP_PERIODS periods (soft start).
//  - Stall guard: a push is made smaller when it would need more than MAX_FULL_STEPS per second.
// Even mode: the motor turns continuously at one full step per note period.
// ---------------------------------------------------------------------------------------------
#define DEFAULT_TONE_MODE 0          // [CFG mode] 0 = even (continuous rotation), 1 = push
#define DEFAULT_GROUP 1              // [CFG group] even mode: microsteps sent together (1 = smoothest)
#define DEFAULT_OCTAVE 0             // [CFG oct] every note shifted by this many octaves (-2..+2)
#define DEFAULT_LEVEL 5              // [CFG level / LEVEL] push mode loudness, 1..MAX_LEVEL
#define MAX_LEVEL 20
#define MAX_PULSE_BURST 40           // push mode: upper limit of one push, in microsteps
#define STEP_PULSE_US 2              // STEP pulse width basis; A4988/HR4988 need >= 1 us high
#define DEFAULT_MICROSTEPS 16        // [CFG micro] must match the driver STEP_SIZE jumpers (all three fitted = 1/16)
#define DEFAULT_STEP_SPACING_US 25   // [CFG spacing] push mode: us between the steps of one push
#define DEFAULT_RAMP_PERIODS 8       // [CFG ramp] push mode soft start, in note periods (0 = off)
#define DEFAULT_MAX_FULL_STEPS 1500  // [CFG maxfs] stall guard, full steps per second (0 = off)
#define DEFAULT_RELEASE_MS 400       // [CFG release] a silent motor's driver is switched off after this long
// [CFG budget] CPU guard, step pulses per second per motor (0 = off). Each step is an interrupt
// (~4 us). 16000 steps/s per motor = ~32% of the CPU with all five motors at their limit, which
// leaves the main loop (link to the ESP) enough time. Without it, level 10 with five busy motors
// used ~100% and the link timed out (tested 2026-10-02).
// 24000 = even mode at 1/16: 16 steps per note period, notes up to ~1500 Hz.
#define DEFAULT_STEP_BUDGET 24000
#define NOTE_MIN_HZ 31               // lowest playable pitch: the longest period that fits the 16-bit timer at 2 MHz
#define NOTE_MAX_HZ 4000             // highest pitch; MIDI notes above are clamped to this

// ---------------------------------------------------------------------------------------------
// Playback (MusicEngine.cpp)
// ---------------------------------------------------------------------------------------------
#define EVENT_BUFFER_SIZE 256        // note events buffered ahead (8 bytes each, RAM!): 6-12 s of music.
                                     // Must be a power of two. Reported to the ESP as free slots.
#define LATE_NOTE_TOLERANCE_MS 50    // a note started more than this after its time counts as late
#define TEST_TONE_MAX_MS 3000        // TEST tones are cut to this length
#define PLAY_LOG_MS 5000             // ms between progress lines on the USB log while playing

// ---------------------------------------------------------------------------------------------
// Other outputs
// ---------------------------------------------------------------------------------------------
// Heaters (hotend 1, hotend 2, bed): forced LOW at boot and never switched on.
#define HEATER_PINS {2, 3, 4}

// Board cooling fan, kept on while powered (same as the stepper_music firmware).
#define FAN_PIN 7

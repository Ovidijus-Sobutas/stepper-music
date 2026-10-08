// NodeMCU ESP8266 music player: every hardware pin, timing and default in one place.
// Values marked "GT2560" must agree with gt2560_engine/src/config.h.
#pragma once
#include <Arduino.h>

#define FW_VERSION "0.11.0"

// ---------------------------------------------------------------- USB
#define USB_BAUD 115200                 // Serial (USB / CP2102): debug log and test console

// ---------------------------------------------------------------- link to the GT2560
// SoftwareSerial on D5/D6 (the hardware UART is the USB console). Moves to other pins once the
// SD card needs D5/D6, see docs/PLAN.md.
#define LINK_RX_PIN D5                  // from GT2560 TX1 (D18) via level shifter HV1/LV1
#define LINK_TX_PIN D6                  // to GT2560 RX1 (D19) via level shifter HV2/LV2
#define LINK_BAUD 57600                 // GT2560: LINK_BAUD

#define HEARTBEAT_INTERVAL_MS 500       // idle link: send a PING this often (keeps the GT2560's link timeout away)
#define GT_CONNECTED_TIMEOUT_MS 1500    // no valid frame from the GT2560 for this long = NOT CONNECTED
#define REQUEST_TIMEOUT_MS 100          // no reply within this time -> send the same frame again
#define LINK_TRIES 8                    // attempts per request (~0.8 s); the GT2560 answers repeats from its cache
#define STARTUP_REPORT_WAIT_MS 2000     // the startup report on USB waits this long for the GT2560's version

// Motors in GT2560 order (index = motor number in the link protocol).
#define MOTOR_COUNT 5
static const char *const MOTOR_NAMES[MOTOR_COUNT] = {"X", "Y", "Z", "E0", "E1"};

// ---------------------------------------------------------------- OLED
// 0.96" SSD1306 128x64, 7-pin SPI module, driven by software SPI. CS is tied to GND.
#define OLED_CLOCK_PIN D1               // module pin "D0"
#define OLED_DATA_PIN D2                // module pin "D1"
#define OLED_DC_PIN D0
#define OLED_RESET_PIN D7
#define DISPLAY_REFRESH_MS 250          // redraw at most this often, and only when something changed

// ---------------------------------------------------------------- Wi-Fi
// WIFI_AP_PASSWORD (setup hotspot password, shown on the OLED setup screen) and MDNS_NAME
// (http://<name>.local) come from the .env file: tools/prepare_firmware.ps1 writes LocalConfig.h.
#if __has_include("LocalConfig.h")
#include "LocalConfig.h"
#else
#error "src/LocalConfig.h is missing: copy .env.example to .env, set the values, run tools/prepare_firmware.ps1"
#endif
#define WIFI_AP_PREFIX "MusicPlayer-"   // setup hotspot name, followed by 4 hex digits of the chip ID
#define WIFI_CREDENTIALS_FILE "/config/wifi.txt"
#define WIFI_CONNECT_TIMEOUT_MS 30000   // not connected after this -> also open the setup hotspot
#define WIFI_LOST_TIMEOUT_MS 60000      // connection lost for this long -> also open the setup hotspot
#define WIFI_AP_LINGER_MS 60000         // after connecting, keep the hotspot so the phone sees the result
#define WIFI_RESET_BUTTON_PIN 0         // NodeMCU "FLASH" button (GPIO0, LOW when pressed)
#define WIFI_RESET_HOLD_MS 10000        // hold FLASH this long -> forget all networks, open the setup hotspot

// ---------------------------------------------------------------- sound style defaults
// Shared by all built-in styles (Sound.cpp); the same values as the GT2560's own defaults.
#define DEFAULT_LEVEL 5                 // loudness 1..MAX_LEVEL (push mode: steps per note pulse = level * 2)
#define MAX_LEVEL 10                    // above ~10 high notes stall the motors
#define DEFAULT_STEP_SPACING_US 25      // push mode: time between the steps of one burst
#define DEFAULT_RAMP_PERIODS 8          // push mode: soft start over this many note periods
#define DEFAULT_MAX_FULL_STEPS 1500     // push mode: stall guard, full steps per second (0 = off)
#define DEFAULT_RELEASE_MS 400          // a silent motor's driver is switched off after this long
#define DEFAULT_MICROSTEPS 16           // driver STEP_SIZE jumpers (all three fitted = 1/16)

// ---------------------------------------------------------------- note streaming (Player)
#define GT_MIN_FREE_SLOTS 2             // stop sending notes when the GT2560 buffer has only this many free slots
#define PREFILL_NOTES 64                // start playing after this many notes (~0.5 s), fill the rest while playing
#define BUFFER_FULL_POLL_MS 50          // buffer full: ask the GT2560 for its free space this often
#define SONG_END_POLL_MS 200            // all notes sent: check for the end of the song this often
#define PLAYER_LOG_MS 5000              // progress line on USB this often during a song

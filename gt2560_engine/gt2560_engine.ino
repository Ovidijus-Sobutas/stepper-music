/**
 * Stepper music engine for the GT2560 Rev A (ATmega2560).
 *
 * Real-time side of the Wi-Fi music player: receives commands and note events from the
 * NodeMCU ESP8266 over Serial1 (TX1 = D18, RX1 = D19) and plays them on 5 steppers.
 * USB (Serial, 115200) is the debug log. See docs/PLAN.md.
 *
 * Modules: comm/ (link to the ESP), music/ (song buffer and clock), motors/ (step timing and
 * driver power). All settings are in src/config.h.
 */
#include "src/config.h"
#include "src/motors/MotorManager.h"
#include "src/comm/EspLink.h"
#include "src/music/MusicEngine.h"

void setup() {
  motorsInitSafe();  // first: heaters off, drivers disabled

  Serial.begin(USB_BAUD);
  Serial.println();
  Serial.println(F("=== GT2560 music engine ==="));
  Serial.print(F("Firmware: "));
  Serial.println(F(FW_VERSION));
  Serial.println(F("Motors: disabled, heaters: off"));
  Serial.print(F("Link: Serial1 (D18/D19) at "));
  Serial.println(LINK_BAUD);

  esplink::begin();
}

// Periodic one-line status on USB. Skipped while playing: MusicEngine logs its own progress.
static void logStatusIfDue() {
  static uint32_t lastStatusLogMs = 0;
  if (engine::state() == EngineState::PLAYING || millis() - lastStatusLogMs < STATUS_LOG_MS) return;
  lastStatusLogMs = millis();
  Serial.print(F("[GT] state="));
  Serial.print(engine::stateName());
  Serial.print(F(" link="));
  Serial.print(esplink::connected() ? F("UP") : F("DOWN"));
  Serial.print(F(" timeouts="));
  Serial.print(esplink::timeouts());
  Serial.print(F(" motors="));
  Serial.println(motorEnableMask() ? F("ON") : F("off"));
}

void loop() {
  esplink::poll();
  engine::update();
  logStatusIfDue();
}

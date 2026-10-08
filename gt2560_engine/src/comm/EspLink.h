// GT2560 side of the serial link to the ESP8266 (Serial1, frames from LinkFrame.h).
// The GT2560 only ever answers a frame from the ESP (one reply per frame, same sequence number),
// except for one BOOT frame after reset. It also owns the link safety timeout: no valid frame
// for LINK_TIMEOUT_MS stops playback and releases the motors. See docs/PLAN.md 5.3.
#pragma once
#include <Arduino.h>

namespace esplink {

void begin();       // opens Serial1 and announces BOOT to the ESP
void poll();        // call every loop: reads frames, answers them, checks the safety timeout
bool connected();   // a valid frame arrived within LINK_TIMEOUT_MS
uint32_t timeouts();

}  // namespace esplink

// Music engine: buffers the note events sent by the ESP8266 and plays them on the GT2560's own
// song clock, so Wi-Fi and link delays do not affect the rhythm as long as the buffer is ahead.
//
//   IDLE --LOAD--> READY --PLAY--> PLAYING <--PAUSE/RESUME--> PAUSED
//   PLAYING --(END time reached, buffer empty)--> DONE (motors released)
//   STOP, link timeout, errors --> IDLE (motors released)
#pragma once
#include <Arduino.h>

enum class EngineState : uint8_t { IDLE, READY, PLAYING, PAUSED, DONE };

namespace engine {

void update();  // call every loop

bool load();     // clear buffer and clock -> READY
bool play();     // READY -> PLAYING (idempotent while playing)
bool pause();
bool resume();
void stop();     // any state -> IDLE, motors released

// Note event: starts startMs after song start, on motor, for durationMs. False if not accepted.
bool addNote(uint32_t startMs, uint8_t motor, uint8_t note, uint16_t durationMs);
bool setSongEnd(uint32_t endMs);  // song ends at endMs; DONE once reached and all notes finished

bool playTestTone(uint8_t motor, uint8_t note, uint16_t ms);  // single test tone (IDLE/DONE only)

// A motor that has been silent this long is switched off (0 = right after each note).
void setReleaseMs(uint16_t ms);
uint16_t getReleaseMs();

uint16_t freeSlots();
uint16_t bufferedNotes();
uint32_t songMs();
uint16_t lateNotes();  // notes that arrived after their start time (buffer underrun)
EngineState state();
const char *stateName();

}  // namespace engine

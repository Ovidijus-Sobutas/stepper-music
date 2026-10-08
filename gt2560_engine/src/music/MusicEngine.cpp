// Music engine: note event ring buffer, song clock and playback state machine. See MusicEngine.h.
#include "MusicEngine.h"
#include "../config.h"
#include "../motors/MotorManager.h"
#include "../motors/ToneGenerator.h"

namespace engine {

struct NoteEvent {
  uint32_t startMs;  // song time
  uint8_t motor;
  uint8_t note;      // MIDI note number
  uint16_t durationMs;
};

static_assert((EVENT_BUFFER_SIZE & (EVENT_BUFFER_SIZE - 1)) == 0, "EVENT_BUFFER_SIZE must be a power of 2");
static const uint16_t BUFFER_INDEX_MASK = EVENT_BUFFER_SIZE - 1;
static NoteEvent events[EVENT_BUFFER_SIZE];
static uint16_t writeIndex = 0, readIndex = 0, bufferedCount = 0;

static EngineState engineState = EngineState::IDLE;
static uint32_t songStartMillis = 0;  // millis() at song time 0
static uint32_t pausedSongMs = 0;     // song time when paused
static bool songEndKnown = false;
static uint32_t songEndMs = 0;
static uint16_t lateNoteCount = 0;

static bool notePlaying[NUM_MOTORS];
static uint8_t currentNote[NUM_MOTORS];
static uint32_t noteEndSongMs[NUM_MOTORS];
static uint32_t silentSinceMillis[NUM_MOTORS];  // when the motor's last note ended
static uint16_t releaseMs = DEFAULT_RELEASE_MS;

static bool testToneActive = false;
static uint8_t testToneMotor = 0;
static uint32_t testToneEndMillis = 0;

static uint32_t lastProgressLogMs = 0;

static void clearNotes() {
  for (uint8_t m = 0; m < NUM_MOTORS; m++) notePlaying[m] = false;
}

static void clearBuffer() {
  writeIndex = readIndex = bufferedCount = 0;
  songEndKnown = false;
  lateNoteCount = 0;
}

static void finishTestTone() {
  if (!testToneActive) return;
  testToneActive = false;
  disableAllMotors();
}

static bool acceptsSongData() {
  return engineState == EngineState::READY || engineState == EngineState::PLAYING ||
         engineState == EngineState::PAUSED;
}

uint32_t songMs() {
  switch (engineState) {
    case EngineState::PLAYING: return millis() - songStartMillis;
    case EngineState::PAUSED: return pausedSongMs;
    default: return 0;
  }
}

bool load() {
  finishTestTone();
  disableAllMotors();
  clearBuffer();
  clearNotes();
  engineState = EngineState::READY;
  Serial.println(F("[PLAY] loaded, waiting for notes"));
  return true;
}

bool play() {
  if (engineState == EngineState::PLAYING) return true;
  if (engineState != EngineState::READY) return false;
  songStartMillis = millis();
  lastProgressLogMs = millis();
  engineState = EngineState::PLAYING;  // each motor is switched on when its first note starts
  Serial.print(F("[PLAY] start, "));
  Serial.print(bufferedCount);
  Serial.print(F(" notes buffered, level "));
  Serial.println(toneLevel());
  return true;
}

bool pause() {
  if (engineState == EngineState::PAUSED) return true;
  if (engineState != EngineState::PLAYING) return false;
  pausedSongMs = songMs();
  disableAllMotors();  // no holding current while paused
  engineState = EngineState::PAUSED;
  Serial.println(F("[PLAY] paused, motors released"));
  return true;
}

bool resume() {
  if (engineState == EngineState::PLAYING) return true;
  if (engineState != EngineState::PAUSED) return false;
  songStartMillis = millis() - pausedSongMs;
  for (uint8_t m = 0; m < NUM_MOTORS; m++) {  // restart the notes that were cut by the pause
    if (notePlaying[m] && noteEndSongMs[m] > pausedSongMs) {
      enableMotor(m);
      toneStart(m, currentNote[m]);
    }
  }
  engineState = EngineState::PLAYING;
  Serial.println(F("[PLAY] resumed"));
  return true;
}

void stop() {
  testToneActive = false;
  disableAllMotors();
  clearBuffer();
  clearNotes();
  if (engineState != EngineState::IDLE) Serial.println(F("[PLAY] stopped, motors released"));
  engineState = EngineState::IDLE;
}

bool addNote(uint32_t startMs, uint8_t motor, uint8_t note, uint16_t durationMs) {
  if (!acceptsSongData()) return false;
  if (bufferedCount >= EVENT_BUFFER_SIZE || motor >= NUM_MOTORS || note > 127) return false;
  events[writeIndex] = {startMs, motor, note, durationMs};
  writeIndex = (writeIndex + 1) & BUFFER_INDEX_MASK;
  bufferedCount++;
  return true;
}

bool setSongEnd(uint32_t endMs) {
  if (!acceptsSongData()) return false;
  songEndKnown = true;
  songEndMs = endMs;
  return true;
}

bool playTestTone(uint8_t motor, uint8_t note, uint16_t ms) {
  if (engineState != EngineState::IDLE && engineState != EngineState::DONE) return false;
  if (motor >= NUM_MOTORS || note > 127) return false;
  finishTestTone();
  testToneActive = true;
  testToneMotor = motor;
  testToneEndMillis = millis() + min(ms, (uint16_t)TEST_TONE_MAX_MS);
  enableMotor(motor);
  toneStart(motor, note);
  Serial.print(F("[TEST] motor "));
  Serial.print(motor);
  Serial.print(F(" note "));
  Serial.println(note);
  return true;
}

static void finishTestToneIfDue() {
  if (testToneActive && (int32_t)(millis() - testToneEndMillis) >= 0) {
    finishTestTone();
    Serial.print(F("[TEST] motor "));
    Serial.print(testToneMotor);
    Serial.println(F(" done, released"));
  }
}

// Starts every buffered note whose time has come. Done before ending notes: a note that follows
// straight on from the previous one on the same motor then changes speed without stopping
// (legato), instead of stopping and soft-starting.
static void startDueNotes(uint32_t songTime) {
  while (bufferedCount && events[readIndex].startMs <= songTime) {
    NoteEvent e = events[readIndex];
    readIndex = (readIndex + 1) & BUFFER_INDEX_MASK;
    bufferedCount--;
    uint32_t end = e.startMs + e.durationMs;
    if (songTime > e.startMs + LATE_NOTE_TOLERANCE_MS) lateNoteCount++;
    if (end <= songTime) continue;  // arrived too late to be heard at all
    enableMotor(e.motor);
    toneStart(e.motor, e.note);
    notePlaying[e.motor] = true;
    currentNote[e.motor] = e.note;
    noteEndSongMs[e.motor] = end;
  }
}

static void endFinishedNotesAndReleaseIdleMotors(uint32_t songTime) {
  uint32_t nowMillis = millis();
  for (uint8_t m = 0; m < NUM_MOTORS; m++) {
    if (notePlaying[m] && songTime >= noteEndSongMs[m]) {
      toneStop(m);
      notePlaying[m] = false;
      silentSinceMillis[m] = nowMillis;
    }
    // A motor that stays quiet is switched off: no holding current, no hum, no heat.
    if (!notePlaying[m] && (motorEnableMask() & _BV(m)) && nowMillis - silentSinceMillis[m] >= releaseMs)
      disableMotor(m);
  }
}

static bool songFinished(uint32_t songTime) {
  bool anyNotePlaying = false;
  for (uint8_t m = 0; m < NUM_MOTORS; m++) anyNotePlaying |= notePlaying[m];
  return songEndKnown && bufferedCount == 0 && !anyNotePlaying && songTime >= songEndMs;
}

static void finishSong(uint32_t songTime) {
  disableAllMotors();
  engineState = EngineState::DONE;
  Serial.print(F("[PLAY] song finished at "));
  Serial.print(songTime / 1000.0, 1);
  Serial.print(F(" s, late notes: "));
  Serial.print(lateNoteCount);
  Serial.println(F(", motors released"));
}

static void logProgressIfDue(uint32_t songTime) {
  if (millis() - lastProgressLogMs < PLAY_LOG_MS) return;
  lastProgressLogMs = millis();
  Serial.print(F("[PLAY] t="));
  Serial.print(songTime / 1000.0, 1);
  Serial.print(F("s buffered="));
  Serial.print(bufferedCount);
  Serial.print(F(" late="));
  Serial.println(lateNoteCount);
}

void update() {
  finishTestToneIfDue();
  if (engineState != EngineState::PLAYING) return;

  uint32_t songTime = songMs();
  startDueNotes(songTime);
  endFinishedNotesAndReleaseIdleMotors(songTime);
  if (songFinished(songTime)) {
    finishSong(songTime);
    return;
  }
  logProgressIfDue(songTime);
}

void setReleaseMs(uint16_t ms) { releaseMs = ms; }
uint16_t getReleaseMs() { return releaseMs; }

uint16_t freeSlots() { return EVENT_BUFFER_SIZE - bufferedCount; }
uint16_t bufferedNotes() { return bufferedCount; }
uint16_t lateNotes() { return lateNoteCount; }
EngineState state() { return engineState; }

const char *stateName() {
  switch (engineState) {
    case EngineState::READY: return "READY";
    case EngineState::PLAYING: return "PLAYING";
    case EngineState::PAUSED: return "PAUSED";
    case EngineState::DONE: return "DONE";
    default: return "IDLE";
  }
}

}  // namespace engine

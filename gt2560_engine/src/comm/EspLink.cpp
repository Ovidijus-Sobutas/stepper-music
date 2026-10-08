// GT2560 side of the ESP8266 link: command handlers, duplicate-reply cache and safety timeout.
// Reply formats are part of the protocol (docs/PLAN.md 5.3): field order and number formatting
// must match what the ESP parses in Gt2560Link.cpp.
#include "EspLink.h"
#include <stdarg.h>
#include "LinkFrame.h"
#include "../config.h"
#include "../motors/MotorManager.h"
#include "../motors/ToneGenerator.h"
#include "../music/MusicEngine.h"

namespace esplink {

static link::Parser parser;
static uint32_t lastValidFrameMs = 0;
static bool linkUp = false;
static uint32_t timeoutCount = 0;

// When a reply is lost, the ESP sends the same frame again (same seq). The command must not run
// twice (a note added twice, a test tone played twice), so the last reply is kept and resent.
static bool haveLastRequest = false;
static uint8_t lastRequestSeq = 0;
static char lastRequestType[8] = "";
static uint32_t lastRequestMs = 0;
static char lastReply[link::MAX_FRAME + 1];
static size_t lastReplyLen = 0;  // 0 = nothing to resend
static uint32_t duplicateCount = 0;
static uint32_t maxLoopGapUs = 0;  // longest time between two poll() calls since the last PS

// ---------------------------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------------------------

// Every frame sent goes through here, so lastReply always holds the latest one.
static void sendFrame(const char *body) {
  size_t n = link::build(lastReply, sizeof(lastReply), body);
  if (n) {
    lastReplyLen = n;
    Serial1.write(lastReply, n);
  } else {
    lastReplyLen = 0;
    Serial.print(F("[LINK] reply too long, dropped: "));
    Serial.println(body);
  }
}

// printf-style sendFrame(). A body too long for a frame is dropped by sendFrame(), never cut.
static void sendFormatted(const char *fmt, ...) {
  char body[link::MAX_FRAME];
  va_list args;
  va_start(args, fmt);
  vsnprintf(body, sizeof(body), fmt, args);
  va_end(args);
  sendFrame(body);
}

static void replyOkOrStateError(uint8_t seq, bool ok) {
  if (ok) sendFormatted("OK,%u", seq);
  else sendFormatted("ERR,%u,STATE", seq);
}

static uint32_t fieldAsUint(const link::Frame &f, uint8_t i) { return strtoul(f.field(i), nullptr, 10); }

// ---------------------------------------------------------------------------------------------
// Link state and duplicate detection
// ---------------------------------------------------------------------------------------------

static void noteValidFrame() {
  lastValidFrameMs = millis();
  if (!linkUp) {
    linkUp = true;
    Serial.println(F("[LINK] ESP connected"));
  }
}

static bool isRetryOfLastRequest(const link::Frame &f) {
  return haveLastRequest && f.seq() == lastRequestSeq && !strcmp(f.type(), lastRequestType) &&
         millis() - lastRequestMs < DUPLICATE_WINDOW_MS;
}

static void rememberRequest(const link::Frame &f) {
  haveLastRequest = true;
  lastRequestSeq = f.seq();
  strlcpy(lastRequestType, f.type(), sizeof(lastRequestType));
  lastRequestMs = millis();
}

// ---------------------------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------------------------

static void handleNote(const link::Frame &f, uint8_t seq) {  // N,seq,t,motor,note,dur
  bool ok = engine::addNote(fieldAsUint(f, 2), (uint8_t)fieldAsUint(f, 3), (uint8_t)fieldAsUint(f, 4),
                            (uint16_t)fieldAsUint(f, 5));
  if (ok) sendFormatted("A,%u,%u,%lu", seq, engine::freeSlots(), (unsigned long)engine::songMs());
  else sendFormatted("ERR,%u,%s", seq, engine::freeSlots() ? "STATE" : "FULL");
}

static void handlePing(uint8_t seq) {
  sendFormatted("PONG,%u,%s,%u,%lu", seq, engine::stateName(), engine::freeSlots(),
                (unsigned long)engine::songMs());
}

static void handleStatus(uint8_t seq) {  // link and board health
  const link::ParserStats &s = parser.stats();
  sendFormatted("STAT,%u,%s,%lu,%lu,%lu,%lu,%lu,%lu,%u", seq, engine::stateName(), millis() / 1000UL,
                (unsigned long)s.ok, (unsigned long)s.bad, (unsigned long)s.overflows,
                (unsigned long)s.junk, (unsigned long)timeoutCount, motorEnableMask());
}

static void handlePlaybackStatus(uint8_t seq) {
  // ...,gap = longest main-loop pause since the last PS (ms), dropped = steps cut short (CPU behind)
  sendFormatted("PS,%u,%s,%lu,%u,%u,%u,%u,%lu,%u,%u", seq, engine::stateName(),
                (unsigned long)engine::songMs(), engine::bufferedNotes(), engine::lateNotes(),
                motorEnableMask(), toneLevel(), (unsigned long)duplicateCount,
                (unsigned)(maxLoopGapUs / 1000), toneLateSteps());
  maxLoopGapUs = 0;
}

// --- CFG,seq,key[,value]: motor drive tuning, applies to the next note ---

enum class ConfigResult : uint8_t { SET, SHOW, UNKNOWN_KEY };

static bool isValidMicrostepCount(uint32_t v) { return v == 1 || v == 2 || v == 4 || v == 8 || v == 16; }

// Closest to the stepper_music player: tight bursts, no guards.
static void applyClassicPreset(ToneSettings &s) {
  s.mode = TONE_PUSH; s.spacingUs = 12; s.rampPeriods = 0; s.maxFullSteps = 0; engine::setReleaseMs(60000);
  s.stepBudget = DEFAULT_STEP_BUDGET;  // the CPU guard stays: without it the link fails
}

static void applyDefaultPreset(ToneSettings &s) {
  s.mode = DEFAULT_TONE_MODE; s.spacingUs = DEFAULT_STEP_SPACING_US; s.rampPeriods = DEFAULT_RAMP_PERIODS;
  s.maxFullSteps = DEFAULT_MAX_FULL_STEPS; s.microsteps = DEFAULT_MICROSTEPS;
  s.stepBudget = DEFAULT_STEP_BUDGET; s.group = DEFAULT_GROUP; s.octave = DEFAULT_OCTAVE;
  engine::setReleaseMs(DEFAULT_RELEASE_MS);
}

// Out-of-range values are clamped; invalid group/micro values leave the setting unchanged.
static ConfigResult applyConfig(const char *key, const char *valueText) {
  ToneSettings &s = toneSettings();
  uint32_t v = strtoul(valueText, nullptr, 10);
  long signedValue = strtol(valueText, nullptr, 10);
  if (!strcmp(key, "mode")) s.mode = v ? TONE_PUSH : TONE_EVEN;
  else if (!strcmp(key, "group")) s.group = isValidMicrostepCount(v) ? v : s.group;
  else if (!strcmp(key, "oct")) s.octave = constrain(signedValue, -2L, 2L);
  else if (!strcmp(key, "spacing")) s.spacingUs = constrain(v, 12UL, 2000UL);
  else if (!strcmp(key, "ramp")) s.rampPeriods = constrain(v, 0UL, 50UL);
  else if (!strcmp(key, "maxfs")) s.maxFullSteps = constrain(v, 0UL, 20000UL);
  else if (!strcmp(key, "budget")) s.stepBudget = constrain(v, 0UL, 40000UL);
  else if (!strcmp(key, "micro")) s.microsteps = isValidMicrostepCount(v) ? v : s.microsteps;
  else if (!strcmp(key, "release")) engine::setReleaseMs(constrain(v, 0UL, 60000UL));
  else if (!strcmp(key, "level")) toneSetLevel((uint8_t)v);
  else if (!strcmp(key, "even")) s.mode = TONE_EVEN;
  else if (!strcmp(key, "push")) s.mode = TONE_PUSH;
  else if (!strcmp(key, "classic")) { applyClassicPreset(s); return ConfigResult::SHOW; }
  else if (!strcmp(key, "default")) { applyDefaultPreset(s); return ConfigResult::SHOW; }
  else if (!strcmp(key, "show")) return ConfigResult::SHOW;
  else return ConfigResult::UNKNOWN_KEY;
  return ConfigResult::SET;
}

static void logToneSettings(const ToneSettings &s) {
  Serial.print(F("[CFG] mode=")); Serial.print(s.mode == TONE_EVEN ? F("even") : F("push"));
  Serial.print(F(" group=")); Serial.print(s.group);
  Serial.print(F(" oct=")); Serial.print(s.octave);
  Serial.print(F(" level=")); Serial.print(s.level);
  Serial.print(F(" micro=")); Serial.print(s.microsteps);
  Serial.print(F(" spacing=")); Serial.print(s.spacingUs);
  Serial.print(F("us ramp=")); Serial.print(s.rampPeriods);
  Serial.print(F(" maxfs=")); Serial.print(s.maxFullSteps);
  Serial.print(F(" release=")); Serial.print(engine::getReleaseMs());
  Serial.print(F("ms budget=")); Serial.println(s.stepBudget);
}

static void sendTuneReply(uint8_t seq, const ToneSettings &s) {
  sendFormatted("TUNE,%u,%s,%u,%u,%u,%d,%u,%u,%u,%u,%u", seq, s.mode == TONE_EVEN ? "even" : "push", s.level,
                s.microsteps, s.group, s.octave, s.spacingUs, s.rampPeriods, s.maxFullSteps,
                engine::getReleaseMs(), s.stepBudget);
}

static void handleConfig(const link::Frame &f, uint8_t seq) {
  switch (applyConfig(f.field(2), f.field(3))) {
    case ConfigResult::UNKNOWN_KEY:
      sendFormatted("ERR,%u,KEY", seq);
      break;
    case ConfigResult::SET:
      replyOkOrStateError(seq, true);  // single setting (the ESP sends a whole style as a series of these)
      break;
    case ConfigResult::SHOW:
      logToneSettings(toneSettings());
      sendTuneReply(seq, toneSettings());
      break;
  }
}

// Commands other than N and PING: rare, so each one is logged on USB.
static void handleCommand(const link::Frame &f, uint8_t seq) {
  Serial.print(F("[LINK] rx "));
  Serial.print(f.type());
  Serial.print(F(" seq="));
  Serial.println(seq);

  if (f.is("VER")) sendFormatted("VER,%u,%s", seq, FW_VERSION);
  else if (f.is("STAT")) handleStatus(seq);
  else if (f.is("PS")) handlePlaybackStatus(seq);
  else if (f.is("LOAD")) replyOkOrStateError(seq, engine::load());
  else if (f.is("END")) replyOkOrStateError(seq, engine::setSongEnd(fieldAsUint(f, 2)));
  else if (f.is("PLAY")) replyOkOrStateError(seq, engine::play());
  else if (f.is("PAUSE")) replyOkOrStateError(seq, engine::pause());
  else if (f.is("RESUME")) replyOkOrStateError(seq, engine::resume());
  else if (f.is("STOP")) {
    engine::stop();
    replyOkOrStateError(seq, true);
  } else if (f.is("LEVEL")) {
    toneSetLevel((uint8_t)fieldAsUint(f, 2));
    replyOkOrStateError(seq, true);
  } else if (f.is("CFG")) handleConfig(f, seq);
  else if (f.is("TEST")) {  // TEST,seq,motor,note,ms
    replyOkOrStateError(seq, engine::playTestTone((uint8_t)fieldAsUint(f, 2), (uint8_t)fieldAsUint(f, 3),
                                                  (uint16_t)fieldAsUint(f, 4)));
  } else sendFormatted("ERR,%u,UNKNOWN", seq);
}

static void handleFrame(const link::Frame &f) {
  noteValidFrame();
  uint8_t seq = f.seq();

  if (isRetryOfLastRequest(f)) {
    duplicateCount++;
    if (lastReplyLen) Serial1.write(lastReply, lastReplyLen);
    return;
  }
  rememberRequest(f);

  // High-rate frames first, not logged.
  if (f.is("N")) handleNote(f, seq);
  else if (f.is("PING")) handlePing(seq);
  else handleCommand(f, seq);
}

// ---------------------------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------------------------

void begin() {
  Serial1.begin(LINK_BAUD);
  sendFormatted("BOOT,0,%s", FW_VERSION);
}

static void trackLoopGap() {
  static uint32_t lastPollUs = 0;
  uint32_t nowUs = micros();
  if (lastPollUs && nowUs - lastPollUs > maxLoopGapUs) maxLoopGapUs = nowUs - lastPollUs;
  lastPollUs = nowUs;
}

static void checkLinkTimeout() {
  if (!linkUp || millis() - lastValidFrameMs <= LINK_TIMEOUT_MS) return;
  linkUp = false;
  timeoutCount++;
  Serial.print(F("[LINK] timeout: no valid frame for "));
  Serial.print(LINK_TIMEOUT_MS);
  Serial.println(F(" ms"));
  engine::stop();
  Serial.println(F("[SAFETY] link timeout -> playback stopped, motors disabled, state IDLE"));
}

void poll() {
  trackLoopGap();
  while (Serial1.available()) {
    if (parser.feed((char)Serial1.read())) handleFrame(parser.frame());
  }
  checkLinkTimeout();
}

bool connected() { return linkUp; }
uint32_t timeouts() { return timeoutCount; }

}  // namespace esplink

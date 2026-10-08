#include "Player.h"
#include "../config.h"

void Player::listSongs() const {
  Serial.printf("Songs in flash (%lu of %lu KB used):\n", (unsigned long)(library_.usedBytes() / 1024),
                (unsigned long)(library_.totalBytes() / 1024));
  for (size_t i = 0; i < library_.count(); i++) {
    const SongInfo &song = library_.at(i);
    Serial.printf("  %-24s %5lu notes  %6.1f s  %6lu bytes\n", song.name, (unsigned long)song.notes,
                  song.lengthMs / 1000.0, (unsigned long)song.bytes);
  }
}

Player::NoteEvent Player::readEvent(uint32_t index) {
  NoteEvent event = {};
  uint32_t offset = SongLibrary::HEADER_BYTES + SongLibrary::EVENT_BYTES * index;
  if (songFile_.position() != offset) songFile_.seek(offset);  // notes are read in order: seek only when needed
  songFile_.read((uint8_t *)&event, SongLibrary::EVENT_BYTES);
  return event;
}

// ---------------------------------------------------------------- commands

bool Player::play(const char *name) {
  const SongInfo *info = library_.find(name);
  if (!info) {
    Serial.printf("[PLAYER] no song called '%s' (type 'songs')\n", name);
    return false;
  }
  if (mode_ != Mode::IDLE) {
    strlcpy(queuedSong_, info->name, sizeof(queuedSong_));
    queuedTest_ = false;
    stopCurrent();
    Serial.printf("[PLAYER] '%s' queued, stopping what is running\n", info->name);
    return true;
  }
  songFile_ = library_.open(info->name);
  if (!songFile_) {
    Serial.printf("[PLAYER] cannot open '%s'\n", info->name);
    return false;
  }
  strlcpy(songName_, info->name, sizeof(songName_));
  noteCount_ = info->notes;
  lengthMs_ = info->lengthMs;
  nextNote_ = 0;
  songFile_.seek(SongLibrary::HEADER_BYTES);
  gtFreeKnown_ = playAcknowledged_ = endAcknowledged_ = polledAfterEnd_ = paused_ = false;
  linkFailures_ = 0;
  control_ = Control::NONE;
  requestSeq_ = -1;
  requestKind_ = RequestKind::NONE;
  idleSoundSeq_ = -1;  // a sound setting still on its way is simply sent again later
  if (gt_.takeRebooted()) sound_.resendAll();
  mode_ = Mode::SONG;
  songStep_ = SongStep::LOAD;
  startedMs_ = lastLogMs_ = millis();
  Serial.printf("[PLAYER] playing '%s': %lu notes, %.1f s, style %s\n", songName_, (unsigned long)noteCount_,
                lengthMs_ / 1000.0, sound_.styleName());
  return true;
}

void Player::stop() {
  queuedSong_[0] = 0;
  queuedTest_ = false;
  if (mode_ == Mode::IDLE) {
    gt_.send("STOP");  // make sure the motors are released anyway
    return;
  }
  stopCurrent();
}

void Player::stopCurrent() {
  if (mode_ == Mode::SONG) control_ = Control::STOP;
  else abortTest_ = true;
}

void Player::pause() {
  if (mode_ == Mode::SONG && !paused_) control_ = Control::PAUSE;
}

void Player::resume() {
  if (mode_ == Mode::SONG && paused_) control_ = Control::RESUME;
}

void Player::setLevel(uint8_t level) {
  String error;
  sound_.set("level", constrain(level, 1, MAX_LEVEL), error);  // reaches the GT2560 through the sound sync
  Serial.printf("[PLAYER] level %u\n", this->level());
}

bool Player::startMotorTest(int motor, uint8_t note, uint16_t ms) {
  if (mode_ != Mode::IDLE) {
    queuedTest_ = true;
    queuedSong_[0] = 0;
    queuedTestMotor_ = motor;
    queuedTestNote_ = note;
    queuedTestMs_ = ms;
    stopCurrent();
    Serial.println(F("[PLAYER] motor test queued, stopping what is running"));
    return true;
  }
  abortTest_ = false;
  idleSoundSeq_ = -1;
  testMotor_ = motor < 0 ? 0 : motor;
  testLastMotor_ = motor < 0 ? MOTOR_COUNT - 1 : motor;
  testNote_ = note;
  testMs_ = ms;
  testNextMs_ = millis();
  requestSeq_ = -1;
  mode_ = Mode::TEST;
  return true;
}

// ---------------------------------------------------------------- main loop

void Player::poll() {
  if (mode_ == Mode::SONG) pollSong();
  else if (mode_ == Mode::TEST) pollTest();
  else syncSoundWhileIdle();
  if (mode_ == Mode::IDLE) startQueuedWork();
}

void Player::startQueuedWork() {
  if (queuedSong_[0]) {
    char name[sizeof(queuedSong_)];
    strlcpy(name, queuedSong_, sizeof(name));
    queuedSong_[0] = 0;
    play(name);
  } else if (queuedTest_) {
    queuedTest_ = false;
    startMotorTest(queuedTestMotor_, queuedTestNote_, queuedTestMs_);
  }
}

// Keeps the GT2560's drive settings equal to the chosen sound style while nothing is playing.
// (During a song the same sync runs between the notes, see sendSoundSetting.)
void Player::syncSoundWhileIdle() {
  if (idleSoundSeq_ >= 0) {
    if (!gt_.finished(idleSoundSeq_)) return;
    if (gt_.succeeded(idleSoundSeq_)) sound_.markSent(sentSoundKey_, sentSoundValue_);
    idleSoundSeq_ = -1;
    return;
  }
  if (gt_.takeRebooted()) sound_.resendAll();
  if (!gt_.connected() || !sound_.hasUnsentSettings()) return;
  char args[24];
  if (sound_.nextUnsentSetting(args, sizeof(args), sentSoundKey_, sentSoundValue_))
    idleSoundSeq_ = gt_.send("CFG", args);
}

bool Player::sendRequest(RequestKind kind, const char *type, const char *args) {
  int seq = gt_.send(type, args);
  if (seq < 0) return false;  // another request (heartbeat, console) is in flight; try again next poll
  requestSeq_ = seq;
  requestKind_ = kind;
  return true;
}

void Player::finish(const char *message, EndReason reason) {
  lastEnd_ = reason;
  endCount_++;
  Serial.printf("[PLAYER] %s\n", message);
  strlcpy(lastMessage_, message, sizeof(lastMessage_));
  if (songFile_) songFile_.close();
  mode_ = Mode::IDLE;
  requestSeq_ = -1;
  requestKind_ = RequestKind::NONE;
  control_ = Control::NONE;
}

// ---------------------------------------------------------------- motor test

void Player::pollTest() {
  if (abortTest_ && requestSeq_ < 0) {
    abortTest_ = false;
    // Ends a tone that is still sounding. Skipped if the link is busy: the tone then simply ends
    // after its length (at most 3 s).
    gt_.send("STOP");
    finish("motor test stopped");
    return;
  }
  if (requestSeq_ >= 0) {
    if (!gt_.finished(requestSeq_)) return;
    if (!gt_.succeeded(requestSeq_))
      Serial.printf("[TEST] motor %d (%s): GT2560 refused the test tone\n", testMotor_, MOTOR_NAMES[testMotor_]);
    requestSeq_ = -1;
    testNextMs_ = millis() + testMs_ + 400;  // tone length + a gap
    testMotor_++;
    return;
  }
  if (testMotor_ > testLastMotor_) {
    if (millis() >= testNextMs_) finish("motor test done, all motors released");
    return;
  }
  if (millis() < testNextMs_) return;
  char args[24];
  snprintf(args, sizeof(args), "%d,%u,%u", testMotor_, testNote_, testMs_);
  if (sendRequest(RequestKind::OTHER, "TEST", args))
    Serial.printf("[TEST] motor %d (%s): note %u for %u ms\n", testMotor_, MOTOR_NAMES[testMotor_], testNote_, testMs_);
}

// ---------------------------------------------------------------- song

// Each poll sends at most one request, in this order of priority.
void Player::pollSong() {
  uint32_t now = millis();

  if (gt_.takeRebooted()) {
    sound_.resendAll();
    finish("GT2560 restarted, playback aborted", EndReason::ERROR);
    return;
  }
  if (requestSeq_ >= 0) {
    if (!gt_.finished(requestSeq_)) return;
    if (!handleSongReply()) return;
  }
  if (sendSongStartStep()) return;
  if (control_ != Control::NONE) {  // user controls go first
    sendControl();
    return;
  }
  if (sendSoundSetting()) return;

  // The GT2560 gave up on the song (link timeout, error, stop from elsewhere).
  if (playAcknowledged_ && gtFreeKnown_ && !strcmp(gt_.gtState(), "IDLE")) {
    finish("GT2560 stopped the song (state IDLE)", EndReason::ERROR);
    return;
  }

  logProgress(now);

  if (!gtFreeKnown_) {
    sendRequest(RequestKind::POLL, "PING");
    return;
  }
  bool prefilled = nextNote_ >= noteCount_ || nextNote_ >= PREFILL_NOTES || gt_.gtFreeSlots() <= GT_MIN_FREE_SLOTS;
  if (!playAcknowledged_ && prefilled) {
    sendRequest(RequestKind::PLAY, "PLAY");
    return;
  }
  if (nextNote_ < noteCount_) {
    sendNextNoteOrPoll(now);
    return;
  }
  if (!endAcknowledged_) {
    sendEnd();
    return;
  }
  if (polledAfterEnd_ && !strcmp(gt_.gtState(), "DONE")) {
    char message[64];
    snprintf(message, sizeof(message), "'%s' finished after %.1f s, motors released", songName_,
             (now - startedMs_) / 1000.0);
    finish(message, EndReason::FINISHED);
    return;
  }
  pollGtEvery(now, SONG_END_POLL_MS);
}

// The reply to the request in flight has arrived (or the link gave up on it).
bool Player::handleSongReply() {
  bool ok = gt_.succeeded(requestSeq_);
  requestSeq_ = -1;
  RequestKind kind = requestKind_;
  requestKind_ = RequestKind::NONE;
  if (kind == RequestKind::SOUND_SETTING) {  // not fatal if it failed: it is sent again
    if (ok) sound_.markSent(sentSoundKey_, sentSoundValue_);
    return false;
  }
  if (!ok) {
    // No reply after all tries: usually garbled bytes on the software serial link while Wi-Fi
    // is busy (a big upload). The GT2560 keeps playing from its buffer and only gives up after
    // its LINK_TIMEOUT_MS (6 s), so try again a few times before stopping the song.
    bool noReply = !strcmp(gt_.lastError(), "NO REPLY");
    if (noReply && ++linkFailures_ < MAX_LINK_FAILURES) {
      Serial.printf("[PLAYER] link hiccup %u, trying again\n", linkFailures_);
      return false;  // the same step is sent again on the next poll
    }
    char message[64];
    snprintf(message, sizeof(message), "stopped: GT2560 link error (%s)", gt_.lastError());
    gt_.send("STOP");
    finish(message, EndReason::ERROR);
    return false;
  }
  linkFailures_ = 0;
  switch (kind) {
    case RequestKind::NOTE:
      nextNote_++;
      gtFreeKnown_ = true;
      break;
    case RequestKind::POLL:
      gtFreeKnown_ = true;
      if (endAcknowledged_) polledAfterEnd_ = true;
      break;
    case RequestKind::PLAY:
      playAcknowledged_ = true;
      Serial.printf("[PLAYER] PLAY (%lu notes buffered)\n", (unsigned long)nextNote_);
      break;
    case RequestKind::END:
      endAcknowledged_ = true;
      break;
    default:
      break;
  }
  if (songStep_ == SongStep::LOAD) songStep_ = SongStep::LEVEL;
  else if (songStep_ == SongStep::LEVEL) songStep_ = SongStep::STREAM;
  return true;
}

// LOAD (the GT2560 empties its note buffer) and LEVEL, before the first note. True while not done.
bool Player::sendSongStartStep() {
  if (songStep_ == SongStep::LOAD) {
    sendRequest(RequestKind::OTHER, "LOAD");
    return true;
  }
  if (songStep_ == SongStep::LEVEL) {
    char args[4];
    snprintf(args, sizeof(args), "%u", level());
    sendRequest(RequestKind::OTHER, "LEVEL", args);
    return true;
  }
  return false;
}

void Player::sendControl() {
  Control control = control_;
  bool sent = false;
  switch (control) {
    case Control::STOP: sent = sendRequest(RequestKind::CONTROL, "STOP"); break;
    case Control::PAUSE: sent = sendRequest(RequestKind::CONTROL, "PAUSE"); break;
    case Control::RESUME: sent = sendRequest(RequestKind::CONTROL, "RESUME"); break;
    default: break;
  }
  if (!sent) return;
  control_ = Control::NONE;
  if (control == Control::STOP) {
    finish("stopped, motors released");
  } else if (control == Control::PAUSE) {
    paused_ = true;
    Serial.println(F("[PLAYER] paused"));
  } else if (control == Control::RESUME) {
    paused_ = false;
    Serial.println(F("[PLAYER] resumed"));
  }
}

// Sound style changed during the song: sent between the notes, applies from the next note.
bool Player::sendSoundSetting() {
  if (!sound_.hasUnsentSettings()) return false;
  char args[24];
  if (!sound_.nextUnsentSetting(args, sizeof(args), sentSoundKey_, sentSoundValue_)) return false;
  sendRequest(RequestKind::SOUND_SETTING, "CFG", args);
  return true;
}

void Player::logProgress(uint32_t now) {
  if (now - lastLogMs_ < PLAYER_LOG_MS) return;
  lastLogMs_ = now;
  uint32_t lastSentTimeMs = nextNote_ ? readEvent(nextNote_ - 1).timeMs : 0;
  Serial.printf("[PLAYER] %s: sent %lu/%lu notes (up to %.1f s of %.1f s), GT2560 free=%u state=%s\n", songName_,
                (unsigned long)nextNote_, (unsigned long)noteCount_, lastSentTimeMs / 1000.0, lengthMs_ / 1000.0,
                gt_.gtFreeSlots(), gt_.gtState());
}

// Each note ack reports the free space; when the buffer is full, ask with a PING now and then.
void Player::sendNextNoteOrPoll(uint32_t now) {
  if (gt_.gtFreeSlots() <= GT_MIN_FREE_SLOTS) {
    pollGtEvery(now, BUFFER_FULL_POLL_MS);
    return;
  }
  NoteEvent event = readEvent(nextNote_);
  char args[32];
  snprintf(args, sizeof(args), "%lu,%u,%u,%u", (unsigned long)event.timeMs, event.motor, event.note,
           event.durationMs);
  sendRequest(RequestKind::NOTE, "N", args);
}

// END tells the GT2560 the song length: it reports DONE once that time is reached.
void Player::sendEnd() {
  char args[12];
  snprintf(args, sizeof(args), "%lu", (unsigned long)lengthMs_);
  sendRequest(RequestKind::END, "END", args);
  Serial.println(F("[PLAYER] all notes sent, waiting for the song to finish"));
}

void Player::pollGtEvery(uint32_t now, uint32_t intervalMs) {
  if (now - lastPollMs_ < intervalMs) return;
  lastPollMs_ = now;
  sendRequest(RequestKind::POLL, "PING");
}

void Player::printStatus() const {
  if (mode_ == Mode::IDLE) Serial.println(F("[PLAYER] idle"));
  else if (mode_ == Mode::TEST) Serial.println(F("[PLAYER] motor test running"));
  else
    Serial.printf("[PLAYER] %s%s: sent %lu/%lu notes, GT2560 free=%u state=%s\n", songName_,
                  paused_ ? " (paused)" : "", (unsigned long)nextNote_, (unsigned long)noteCount_, gt_.gtFreeSlots(),
                  gt_.gtState());
}

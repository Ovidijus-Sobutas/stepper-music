// Plays one song (or a motor test) by streaming its note events to the GT2560.
//
// The GT2560 keeps the time; this side only keeps its note buffer filled, one link request in
// flight at a time:
//   LOAD -> LEVEL -> notes until PREFILL_NOTES are buffered -> PLAY -> keep the buffer topped up
//   -> END -> poll until the GT2560 reports DONE (it releases the motors itself).
// Pause/resume/stop and sound-style changes are sent between the notes. Queue decides what plays
// next; the web page and the console call play()/stop()/... and poll() does the work later.
#pragma once
#include <Arduino.h>
#include <FS.h>
#include "SongLibrary.h"
#include "Sound.h"
#include "../config.h"
#include "../gt2560/Gt2560Link.h"

class Player {
public:
  Player(Gt2560Link &gt, SongLibrary &library, Sound &sound) : gt_(gt), library_(library), sound_(sound) {}

  void listSongs() const;
  bool play(const char *name);  // while something runs: stops it first, then starts this song
  void stop();
  void pause();
  void resume();
  void setLevel(uint8_t level);
  // Plays a test tone on one motor (index 0..4 = X, Y, Z, E0, E1), or on each in turn (motor = -1).
  bool startMotorTest(int motor, uint8_t note, uint16_t ms);

  void poll();  // call every loop
  void printStatus() const;

  bool playingSong() const { return mode_ == Mode::SONG; }
  bool testing() const { return mode_ == Mode::TEST; }
  bool paused() const { return paused_; }
  const char *songName() const { return songName_; }
  bool isCurrent(const char *name) const { return mode_ == Mode::SONG && !strcasecmp(songName_, name); }
  const SongLibrary &library() const { return library_; }
  uint32_t lengthMs() const { return lengthMs_; }
  uint8_t level() const { return sound_.settings().level; }
  const Sound &sound() const { return sound_; }
  int testMotor() const { return testMotor_; }
  const char *lastMessage() const { return lastMessage_; }  // how the last song/test ended, for people

  // How the last song or test ended, and a counter that changes every time one ends (Queue).
  enum class EndReason : uint8_t { NONE, FINISHED, STOPPED, ERROR };
  EndReason lastEnd() const { return lastEnd_; }
  uint32_t endCount() const { return endCount_; }

private:
  enum class Mode : uint8_t { IDLE, SONG, TEST };
  enum class SongStep : uint8_t { LOAD, LEVEL, STREAM };
  // What the request in flight was for, so its reply can be handled.
  enum class RequestKind : uint8_t { NONE, NOTE, POLL, PLAY, END, CONTROL, SOUND_SETTING, OTHER };
  enum class Control : uint8_t { NONE, STOP, PAUSE, RESUME };

  // One note event as stored in a .stepper file (read straight from the file, little endian).
  struct NoteEvent {
    uint32_t timeMs;
    uint8_t motor;
    uint8_t note;
    uint16_t durationMs;
  };
  static_assert(sizeof(NoteEvent) == SongLibrary::EVENT_BYTES, "NoteEvent must match the file format");

  bool sendRequest(RequestKind kind, const char *type, const char *args = nullptr);
  NoteEvent readEvent(uint32_t index);
  void startQueuedWork();
  void syncSoundWhileIdle();
  void finish(const char *message, EndReason reason = EndReason::STOPPED);
  void stopCurrent();  // ask the running song or test to end (the queued work starts after it)

  void pollSong();
  bool handleSongReply();  // false: nothing more to do in this poll
  bool sendSongStartStep();
  void sendControl();
  bool sendSoundSetting();
  void logProgress(uint32_t now);
  void sendNextNoteOrPoll(uint32_t now);
  void sendEnd();
  void pollGtEvery(uint32_t now, uint32_t intervalMs);

  void pollTest();

  Gt2560Link &gt_;
  SongLibrary &library_;
  Sound &sound_;
  Mode mode_ = Mode::IDLE;
  SongStep songStep_ = SongStep::LOAD;
  int requestSeq_ = -1;  // -1: no request of the player in flight
  RequestKind requestKind_ = RequestKind::NONE;
  Control control_ = Control::NONE;  // user control waiting to be sent
  int idleSoundSeq_ = -1;            // sound setting sent while idle
  uint8_t sentSoundKey_ = 0;         // the sound setting in flight (Sound::markSent once acknowledged)
  long sentSoundValue_ = 0;
  uint8_t linkFailures_ = 0;  // requests in a row that got no reply
  // 4 x LINK_TRIES x 100 ms ~ 3.2 s of retries, well under the GT2560's 6 s link timeout.
  static const uint8_t MAX_LINK_FAILURES = 4;

  // Started when the current song/test has ended.
  char queuedSong_[24] = "";
  bool queuedTest_ = false;
  int queuedTestMotor_ = 0;
  uint8_t queuedTestNote_ = 60;
  uint16_t queuedTestMs_ = 1000;

  File songFile_;
  char songName_[24] = "";
  uint32_t noteCount_ = 0;
  uint32_t nextNote_ = 0;  // index of the next note to send
  uint32_t lengthMs_ = 0;
  bool gtFreeKnown_ = false;       // a PONG or note ack has told us the GT2560's free space
  bool playAcknowledged_ = false;
  bool endAcknowledged_ = false;
  bool polledAfterEnd_ = false;    // the GT2560 state is fresh, not from before END
  bool paused_ = false;
  uint32_t lastPollMs_ = 0;
  uint32_t lastLogMs_ = 0;
  uint32_t startedMs_ = 0;

  bool abortTest_ = false;
  int testMotor_ = 0, testLastMotor_ = 0;
  uint8_t testNote_ = 60;
  uint16_t testMs_ = 1000;
  uint32_t testNextMs_ = 0;

  char lastMessage_[40] = "";
  EndReason lastEnd_ = EndReason::NONE;
  uint32_t endCount_ = 0;
};

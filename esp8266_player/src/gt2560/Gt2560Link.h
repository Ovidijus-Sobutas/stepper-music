// ESP8266 side of the serial link to the GT2560 music engine (frame format: LinkFrame.h).
//
// One request in flight at a time: send() transmits "TYPE,seq[,args]" and the GT2560 answers
// with the same seq. No reply within REQUEST_TIMEOUT_MS -> the same frame is sent again (the
// GT2560 recognises the repeat and answers from its cache), up to LINK_TRIES times. This
// half-duplex rule also suits SoftwareSerial, which cannot receive while it transmits.
// The Player and the console are the only senders; when the link is idle a PING heartbeat keeps
// the GT2560's link timeout away and refreshes its state, free buffer space and song clock.
#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>
#include "LinkFrame.h"

class Gt2560Link {
public:
  void begin();
  void poll();  // call every loop; never blocks

  bool connected() const;
  const char *gtVersion() const { return gtVersion_; }  // "" until VER or BOOT has been received
  const char *gtState() const { return gtState_; }      // IDLE, READY, PLAYING, PAUSED, DONE
  uint16_t gtFreeSlots() const { return gtFreeSlots_; }  // free note slots in the GT2560 buffer
  uint32_t gtSongMs() const { return gtSongMs_; }        // GT2560 song clock, from the last PONG or note ack

  bool busy() const { return pending_.active; }
  // Returns the sequence number, or -1 if a request is still waiting for its reply.
  int send(const char *type, const char *args = nullptr);
  // True once request <seq> got its reply or gave up, even if other requests (e.g. a heartbeat)
  // have finished since: the last FINISHED_HISTORY results are kept.
  bool finished(int seq) const;
  bool succeeded(int seq) const;
  const char *lastError() const { return lastError_; }  // GT2560 error code, or "NO REPLY"

  // Set when a BOOT frame arrives (GT2560 was reset); cleared by the caller.
  bool takeRebooted() { bool rebooted = rebooted_; rebooted_ = false; return rebooted; }

  void setHeartbeat(bool on);

  // Link tests (USB console).
  void startPingBurst(uint16_t count);
  void startCorruptionTest();
  void sendRaw(const char *text);
  void printStats() const;

private:
  void receiveFrames();
  void handleFrame(const link::Frame &frame);
  void handleReply(const link::Frame &frame);
  void retryOrGiveUp();
  void transmitPending();
  void completePending(bool ok);
  void reportConnectionChange();

  void printStatReply(const link::Frame &frame);
  void printPlaybackReply(const link::Frame &frame);
  void printTuneReply(const link::Frame &frame);

  void pollPingBurst();
  void finishPingBurst();
  void pollCorruptionTest();
  void sendCorruptFramesPart1();
  void sendCorruptFramesPart2();

  SoftwareSerial serial_;
  link::Parser parser_;
  uint8_t nextSeq_ = 1;

  struct PendingRequest {
    bool active = false;
    uint8_t seq = 0;
    char type[8] = "";
    char frame[link::MAX_FRAME + 1];
    size_t length = 0;
    uint32_t sentMs = 0, sentUs = 0;
    uint8_t tries = 0;
  } pending_;
  bool finishedOk_ = false;
  // Results of the most recent requests, newest at finishedHistoryNext_ - 1. Without it, a
  // heartbeat sent right after a late reply (e.g. after a ~1 s loop pause while an upload is
  // saved) overwrote that reply's result, and the Player waited for it until the 8-bit seq
  // wrapped around (~2 minutes, songs ran on silently and Stop was ignored).
  struct FinishedRequest {
    int seq = -1;
    bool ok = false;
  };
  static const uint8_t FINISHED_HISTORY = 8;
  FinishedRequest finishedHistory_[FINISHED_HISTORY];
  uint8_t finishedHistoryNext_ = 0;
  const FinishedRequest *findFinished(int seq) const;
  uint32_t finishedRoundTripUs_ = 0;
  char lastError_[12] = "";

  uint32_t requestCount_ = 0, retryCount_ = 0, failureCount_ = 0;

  bool heartbeat_ = true;
  uint32_t lastSendMs_ = 0;
  uint32_t lastReceiveMs_ = 0;
  bool wasConnected_ = false;
  bool everConnected_ = false;
  bool rebooted_ = false;

  char gtVersion_[12] = "";
  char gtState_[10] = "?";
  uint16_t gtFreeSlots_ = 0;
  uint32_t gtSongMs_ = 0;

  struct PingBurst {
    bool active = false;
    uint16_t total = 0, done = 0, ok = 0, failed = 0;
    int seq = -1;
    uint32_t startMs = 0, retriesAtStart = 0;
    uint32_t minRoundTripUs = 0, maxRoundTripUs = 0;
    uint64_t sumRoundTripUs = 0;
  } burst_;

  enum class CorruptionPhase : uint8_t { OFF, STAT_BEFORE, BATCH_1, BATCH_2, STAT_AFTER, WAIT_STAT_AFTER };
  struct CorruptionTest {
    CorruptionPhase phase = CorruptionPhase::OFF;
    int seq = -1;
    uint32_t batchSentMs = 0;
  } corruption_;
};

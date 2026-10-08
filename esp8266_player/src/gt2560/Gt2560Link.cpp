#include "Gt2560Link.h"
#include "../config.h"

void Gt2560Link::begin() {
  serial_.begin(LINK_BAUD, SWSERIAL_8N1, LINK_RX_PIN, LINK_TX_PIN, false, 256);
  // Block interrupts while each byte is bit-banged (~170 us at 57600). With interrupts allowed,
  // about 1 in 3000 frames to the GT2560 arrived with a stretched bit (caught by the CRC).
  serial_.enableIntTx(false);
}

bool Gt2560Link::connected() const {
  return everConnected_ && millis() - lastReceiveMs_ < GT_CONNECTED_TIMEOUT_MS;
}

void Gt2560Link::setHeartbeat(bool on) {
  heartbeat_ = on;
  Serial.printf("[LINK] heartbeat %s\n", on ? "ON" : "OFF (GT2560 should time out after 6 s)");
}

void Gt2560Link::poll() {
  receiveFrames();
  uint32_t now = millis();
  if (pending_.active && now - pending_.sentMs > REQUEST_TIMEOUT_MS) retryOrGiveUp();

  if (burst_.active) pollPingBurst();
  else if (corruption_.phase != CorruptionPhase::OFF) pollCorruptionTest();
  else if (heartbeat_ && !pending_.active && now - lastSendMs_ >= HEARTBEAT_INTERVAL_MS) send("PING");

  reportConnectionChange();
}

// ---------------------------------------------------------------- sending

int Gt2560Link::send(const char *type, const char *args) {
  if (pending_.active) return -1;
  char body[link::MAX_FRAME];
  uint8_t seq = nextSeq_++;
  if (args && *args) snprintf(body, sizeof(body), "%s,%u,%s", type, seq, args);
  else snprintf(body, sizeof(body), "%s,%u", type, seq);
  pending_.length = link::build(pending_.frame, sizeof(pending_.frame), body);
  if (!pending_.length) {
    Serial.printf("[LINK] frame too long, not sent: %s\n", body);
    return -1;
  }
  pending_.active = true;
  pending_.seq = seq;
  strlcpy(pending_.type, type, sizeof(pending_.type));
  pending_.tries = 0;
  requestCount_++;
  transmitPending();
  return seq;
}

void Gt2560Link::transmitPending() {
  pending_.tries++;
  pending_.sentMs = millis();
  pending_.sentUs = micros();
  lastSendMs_ = pending_.sentMs;
  serial_.write(pending_.frame, pending_.length);
}

void Gt2560Link::retryOrGiveUp() {
  if (pending_.tries < LINK_TRIES) {
    retryCount_++;
    transmitPending();
    return;
  }
  failureCount_++;
  strlcpy(lastError_, "NO REPLY", sizeof(lastError_));
  Serial.printf("[LINK] no reply to %s (seq %u) after %u tries\n", pending_.type, pending_.seq, pending_.tries);
  completePending(false);
}

void Gt2560Link::completePending(bool ok) {
  finishedRoundTripUs_ = micros() - pending_.sentUs;
  pending_.active = false;
  finishedOk_ = ok;
  finishedHistory_[finishedHistoryNext_] = {pending_.seq, ok};
  finishedHistoryNext_ = (finishedHistoryNext_ + 1) % FINISHED_HISTORY;
}

// Newest first: a seq number comes round again after 256 requests, and the newest result wins.
const Gt2560Link::FinishedRequest *Gt2560Link::findFinished(int seq) const {
  for (uint8_t age = 1; age <= FINISHED_HISTORY; age++) {
    const FinishedRequest &entry = finishedHistory_[(finishedHistoryNext_ + FINISHED_HISTORY - age) % FINISHED_HISTORY];
    if (entry.seq == seq) return &entry;
  }
  return nullptr;
}

bool Gt2560Link::finished(int seq) const {
  if (seq < 0 || (pending_.active && pending_.seq == seq)) return false;  // still waiting for it
  return findFinished(seq) != nullptr;
}

bool Gt2560Link::succeeded(int seq) const {
  const FinishedRequest *entry = finished(seq) ? findFinished(seq) : nullptr;
  return entry && entry->ok;
}

void Gt2560Link::sendRaw(const char *text) {
  serial_.print(text);
  serial_.print('\n');
  Serial.printf("[LINK] raw sent: %s\n", text);
}

void Gt2560Link::printStats() const {
  const link::ParserStats &s = parser_.stats();
  Serial.printf("[LINK] ESP: requests=%lu retries=%lu failures=%lu | received ok=%lu bad=%lu overflow=%lu junk=%lu\n",
                (unsigned long)requestCount_, (unsigned long)retryCount_, (unsigned long)failureCount_,
                (unsigned long)s.ok, (unsigned long)s.bad, (unsigned long)s.overflows, (unsigned long)s.junk);
}

void Gt2560Link::reportConnectionChange() {
  bool isConnected = connected();
  if (isConnected == wasConnected_) return;
  wasConnected_ = isConnected;
  if (isConnected) Serial.println(F("[LINK] GT2560: CONNECTED"));
  else Serial.printf("[LINK] GT2560: NOT CONNECTED (no valid frame for %u ms)\n", GT_CONNECTED_TIMEOUT_MS);
}

// ---------------------------------------------------------------- receiving

void Gt2560Link::receiveFrames() {
  while (serial_.available()) {
    if (parser_.feed((char)serial_.read())) handleFrame(parser_.frame());
  }
}

void Gt2560Link::handleFrame(const link::Frame &frame) {
  lastReceiveMs_ = millis();
  everConnected_ = true;

  if (frame.is("BOOT")) {  // the only frame the GT2560 sends without being asked
    strlcpy(gtVersion_, frame.field(2), sizeof(gtVersion_));
    rebooted_ = true;
    Serial.printf("[LINK] GT2560 booted, firmware %s\n", gtVersion_);
    return;
  }

  uint8_t seq = frame.seq();
  if (!pending_.active || seq != pending_.seq) {
    // A late reply to a request that was already retried or given up. Seq 79 is the good frame
    // hidden between the bad ones of the corruption test.
    if (seq == 79 && frame.is("PONG")) Serial.println(F("[TEST] got PONG for seq 79 (the good frame hidden between bad ones)"));
    return;
  }
  handleReply(frame);
}

void Gt2560Link::handleReply(const link::Frame &frame) {
  if (frame.is("PONG")) {  // PONG,seq,state,freeSlots,songMs
    strlcpy(gtState_, frame.field(2), sizeof(gtState_));
    gtFreeSlots_ = (uint16_t)atoi(frame.field(3));
    gtSongMs_ = strtoul(frame.field(4), nullptr, 10);
  } else if (frame.is("A")) {  // note ack: A,seq,freeSlots[,songMs]
    gtFreeSlots_ = (uint16_t)atoi(frame.field(2));
    if (frame.count > 3) gtSongMs_ = strtoul(frame.field(3), nullptr, 10);
  } else if (frame.is("ERR")) {
    strlcpy(lastError_, frame.field(2), sizeof(lastError_));
    Serial.printf("[LINK] GT2560 refused %s (seq %u): %s\n", pending_.type, frame.seq(), lastError_);
    completePending(false);
    return;
  } else if (frame.is("VER")) {
    strlcpy(gtVersion_, frame.field(2), sizeof(gtVersion_));
    Serial.printf("[LINK] GT2560 firmware %s\n", gtVersion_);
  } else if (frame.is("STAT")) {
    printStatReply(frame);
  } else if (frame.is("PS")) {
    printPlaybackReply(frame);
  } else if (frame.is("TUNE")) {
    printTuneReply(frame);
  } else if (frame.is("OK")) {
    Serial.printf("[LINK] %s OK\n", pending_.type);
  }
  completePending(true);
}

// STAT,seq,state,uptime,framesOk,framesBad,overflows,junk,timeouts,motors
void Gt2560Link::printStatReply(const link::Frame &frame) {
  strlcpy(gtState_, frame.field(2), sizeof(gtState_));
  Serial.printf("[LINK] GT2560 STAT: state=%s uptime=%ss frames ok=%s bad=%s overflow=%s junk=%s "
                "timeouts=%s motors=%s\n",
                frame.field(2), frame.field(3), frame.field(4), frame.field(5), frame.field(6), frame.field(7),
                frame.field(8), frame.field(9));
  printStats();
}

// PS,seq,state,songMs,buffered,late,motors,level,repeats,loopGapMs,droppedSteps
void Gt2560Link::printPlaybackReply(const link::Frame &frame) {
  strlcpy(gtState_, frame.field(2), sizeof(gtState_));
  Serial.printf("[LINK] GT2560 playback: state=%s song=%.1fs buffered=%s late=%s motors=%s level=%s repeats=%s "
                "loop-gap=%sms dropped-steps=%s\n",
                frame.field(2), atol(frame.field(3)) / 1000.0, frame.field(4), frame.field(5), frame.field(6),
                frame.field(7), frame.field(8), frame.field(9), frame.field(10));
}

// TUNE,seq,mode,level,micro,group,oct,spacing,ramp,maxfs,release,budget
void Gt2560Link::printTuneReply(const link::Frame &frame) {
  Serial.printf("[CFG] mode=%s group=%s oct=%s level=%s micro=1/%s spacing=%sus ramp=%s maxfs=%s release=%sms "
                "budget=%s\n",
                frame.field(2), frame.field(5), frame.field(6), frame.field(3), frame.field(4), frame.field(7),
                frame.field(8), frame.field(9), frame.field(10), frame.field(11));
}

// ---------------------------------------------------------------- link tests

void Gt2560Link::startPingBurst(uint16_t count) {
  burst_ = PingBurst();
  burst_.active = true;
  burst_.total = count;
  burst_.startMs = millis();
  burst_.retriesAtStart = retryCount_;
  Serial.printf("[TEST] ping burst: %u pings, one at a time, %u tries each\n", count, LINK_TRIES);
}

void Gt2560Link::pollPingBurst() {
  if (burst_.seq >= 0) {
    if (!finished(burst_.seq)) return;
    burst_.done++;
    if (finishedOk_) {
      uint32_t roundTripUs = finishedRoundTripUs_;
      if (burst_.ok == 0 || roundTripUs < burst_.minRoundTripUs) burst_.minRoundTripUs = roundTripUs;
      if (roundTripUs > burst_.maxRoundTripUs) burst_.maxRoundTripUs = roundTripUs;
      burst_.sumRoundTripUs += roundTripUs;
      burst_.ok++;
    } else {
      burst_.failed++;
    }
    burst_.seq = -1;
  }
  if (busy()) return;
  if (burst_.done < burst_.total) burst_.seq = send("PING");
  else finishPingBurst();
}

void Gt2560Link::finishPingBurst() {
  burst_.active = false;
  Serial.printf("[TEST] ping burst done in %lu ms: ok=%u failed=%u retries=%lu\n",
                (unsigned long)(millis() - burst_.startMs), burst_.ok, burst_.failed,
                (unsigned long)(retryCount_ - burst_.retriesAtStart));
  if (burst_.ok)
    Serial.printf("[TEST] round trip: min=%.1f ms avg=%.1f ms max=%.1f ms\n", burst_.minRoundTripUs / 1000.0,
                  (double)burst_.sumRoundTripUs / burst_.ok / 1000.0, burst_.maxRoundTripUs / 1000.0);
  printStats();
  Serial.printf("[TEST] result: %s\n", burst_.failed == 0 ? "PASS" : "FAIL");
}

void Gt2560Link::startCorruptionTest() {
  Serial.println(F("[TEST] corruption test: STAT, 5 bad inputs + 1 good PING, STAT"));
  Serial.println(F("[TEST] expect: 1 PONG (seq 79); GT bad +3, overflow +1, junk grows; link stays up"));
  corruption_ = CorruptionTest();
  corruption_.phase = CorruptionPhase::STAT_BEFORE;
}

// STAT (baseline counters) -> two batches of bad input, 100 ms apart -> STAT again.
void Gt2560Link::pollCorruptionTest() {
  switch (corruption_.phase) {
    case CorruptionPhase::STAT_BEFORE:
      if (!busy()) { corruption_.seq = send("STAT"); corruption_.phase = CorruptionPhase::BATCH_1; }
      break;
    case CorruptionPhase::BATCH_1:
      if (!finished(corruption_.seq)) break;
      sendCorruptFramesPart1();
      corruption_.batchSentMs = millis();
      corruption_.phase = CorruptionPhase::BATCH_2;
      break;
    case CorruptionPhase::BATCH_2:  // waits for the PONG to seq 79 before sending more (half duplex)
      if (millis() - corruption_.batchSentMs < 100) break;
      sendCorruptFramesPart2();
      corruption_.batchSentMs = millis();
      corruption_.phase = CorruptionPhase::STAT_AFTER;
      break;
    case CorruptionPhase::STAT_AFTER:
      if (millis() - corruption_.batchSentMs < 100 || busy()) break;
      corruption_.seq = send("STAT");
      corruption_.phase = CorruptionPhase::WAIT_STAT_AFTER;
      break;
    case CorruptionPhase::WAIT_STAT_AFTER:
      if (finished(corruption_.seq)) {
        corruption_.phase = CorruptionPhase::OFF;
        Serial.println(F("[TEST] corruption test done: compare the two STAT lines"));
      }
      break;
    default:
      break;
  }
}

void Gt2560Link::sendCorruptFramesPart1() {
  char frame[link::MAX_FRAME + 1];
  // 1. valid body, wrong CRC
  link::build(frame, sizeof(frame), "PING,77");
  frame[strlen(frame) - 2] ^= 0x01;
  serial_.print(frame);
  // 2. plain garbage, including non-printable bytes
  serial_.print("hello garbage \xff\x01\x7f\n");
  // 3. frame cut off halfway, directly followed by a good frame (must still be answered)
  serial_.print("$PING,78");
  link::build(frame, sizeof(frame), "PING,79");
  serial_.print(frame);
}

void Gt2560Link::sendCorruptFramesPart2() {
  char frame[link::MAX_FRAME + 1];
  // 4. over-long frame
  serial_.print('$');
  for (int i = 0; i < 90; i++) serial_.print('A');
  serial_.print('\n');
  // 5. body changed after the CRC was computed
  link::build(frame, sizeof(frame), "PING,80");
  frame[4] = 'H';  // "PING" -> "PINH"
  serial_.print(frame);
  // 6. missing CRC
  serial_.print("$PING,81\n");
}

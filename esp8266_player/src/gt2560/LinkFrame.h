// Link frame codec, shared by the GT2560 and ESP8266 firmware (keep both copies identical:
// gt2560_engine/src/comm/ and esp8266_player/src/gt2560/).
//
//   $BODY*CC\n     BODY = TYPE,seq[,field...]
//                  CC   = CRC-8 (poly 0x07, init 0) of BODY, two uppercase hex digits
//
// Anything outside a frame, or any frame with a bad CRC, is dropped and counted. A '$' always
// starts a new frame, so the parser resynchronises by itself after noise or a cut-off frame.
#pragma once
#include <Arduino.h>

namespace link {

const uint8_t MAX_FRAME = 64;   // whole frame including '$', "*CC" and '\n'
const uint8_t MAX_FIELDS = 14;  // fields beyond this are dropped

uint8_t crc8(const char *data, size_t len);

// Writes "$body*CC\n" (NUL-terminated) into out. Returns its length, or 0 if it does not fit.
size_t build(char *out, size_t outSize, const char *body);

// A parsed frame. The field pointers point into the parser's buffer: valid until the next feed().
struct Frame {
  char *fields[MAX_FIELDS];  // fields[0] = TYPE, fields[1] = seq, then the payload
  uint8_t count = 0;

  const char *type() const { return count ? fields[0] : ""; }
  uint8_t seq() const { return count > 1 ? (uint8_t)atoi(fields[1]) : 0; }
  bool is(const char *t) const { return count && strcmp(fields[0], t) == 0; }
  const char *field(uint8_t i) const { return i < count ? fields[i] : ""; }  // "" if missing
};

struct ParserStats {
  uint32_t ok;         // valid frames
  uint32_t bad;        // CRC mismatch or malformed
  uint32_t overflows;  // frame longer than MAX_FRAME
  uint32_t junk;       // bytes outside any frame (noise, boot messages, cut-off frames)
};

class Parser {
public:
  // Feed one received byte. Returns true when frame() holds a new valid frame.
  bool feed(char c);
  const Frame &frame() const { return frame_; }
  const ParserStats &stats() const { return stats_; }

private:
  bool parseBody();  // checks the CRC and splits body_ into frame_; true if valid

  char body_[MAX_FRAME + 1];  // text between '$' and '\n' ("BODY*CC"), NUL-terminated
  uint8_t bodyLen_ = 0;
  bool inFrame_ = false;      // a '$' was seen and the closing '\n' not yet
  Frame frame_;
  ParserStats stats_ = {0, 0, 0, 0};
};

}  // namespace link

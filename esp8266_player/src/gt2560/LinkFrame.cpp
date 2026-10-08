// Link frame codec: CRC, frame building and the byte-by-byte frame parser. See LinkFrame.h.
#include "LinkFrame.h"

namespace link {

uint8_t crc8(const char *data, size_t len) {
  const uint8_t POLYNOMIAL = 0x07;
  uint8_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint8_t)data[i];
    for (uint8_t bit = 0; bit < 8; bit++)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ POLYNOMIAL) : (uint8_t)(crc << 1);
  }
  return crc;
}

size_t build(char *out, size_t outSize, const char *body) {
  static const char HEX_DIGITS[] = "0123456789ABCDEF";
  size_t bodyLen = strlen(body);
  size_t frameLen = bodyLen + 5;  // '$' + body + '*' + 2 hex + '\n'
  if (frameLen > MAX_FRAME || frameLen + 1 > outSize) return 0;
  uint8_t crc = crc8(body, bodyLen);
  out[0] = '$';
  memcpy(out + 1, body, bodyLen);
  out[bodyLen + 1] = '*';
  out[bodyLen + 2] = HEX_DIGITS[crc >> 4];
  out[bodyLen + 3] = HEX_DIGITS[crc & 0x0F];
  out[bodyLen + 4] = '\n';
  out[bodyLen + 5] = 0;
  return frameLen;
}

bool Parser::feed(char c) {
  if (c == '$') {  // start of a frame; also resynchronises after garbage
    if (inFrame_) stats_.junk += bodyLen_ + 1;  // previous frame was cut off
    inFrame_ = true;
    bodyLen_ = 0;
    return false;
  }
  if (!inFrame_) {
    if (c != '\r' && c != '\n') stats_.junk++;
    return false;
  }
  if (c == '\r') return false;
  if (c == '\n') {
    inFrame_ = false;
    return parseBody();
  }
  if (bodyLen_ >= MAX_FRAME - 2) {  // '$' + body + '\n' would exceed MAX_FRAME
    inFrame_ = false;
    stats_.overflows++;
    return false;
  }
  body_[bodyLen_++] = c;
  return false;
}

bool Parser::parseBody() {
  body_[bodyLen_] = 0;
  char *star = strrchr(body_, '*');
  if (!star || !isxdigit(star[1]) || !isxdigit(star[2]) || star[3] != 0) {
    stats_.bad++;
    return false;
  }
  uint8_t expectedCrc = (uint8_t)strtoul(star + 1, nullptr, 16);
  if (crc8(body_, star - body_) != expectedCrc) {
    stats_.bad++;
    return false;
  }
  *star = 0;  // cut off "*CC": body_ is now just the comma-separated fields

  // Split in place: each ',' becomes a NUL and each field pointer points into body_.
  frame_.count = 0;
  char *fieldStart = body_;
  while (fieldStart && frame_.count < MAX_FIELDS) {
    frame_.fields[frame_.count++] = fieldStart;
    fieldStart = strchr(fieldStart, ',');
    if (fieldStart) *fieldStart++ = 0;
  }
  if (frame_.count < 2) {  // every frame needs TYPE and seq
    stats_.bad++;
    return false;
  }
  stats_.ok++;
  return true;
}

}  // namespace link

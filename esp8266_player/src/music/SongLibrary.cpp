#include "SongLibrary.h"
#include <LittleFS.h>
#include <algorithm>
#include "EmbeddedSongs.h"

// No yield() or delay() anywhere in this class: web requests (async server) are handled when the
// main loop yields, and must never find the song list half rebuilt.

static const char SONG_DIR[] = "/songs";
static const char SONG_EXTENSION[] = ".stepper";
static const char UPLOAD_TEMP_PATH[] = "/songs/.upload";
static const char DEMOS_MARKER_PATH[] = "/config/demos-installed";
static const uint32_t UPLOAD_SPACE_RESERVE = 16384;  // never fill the file system completely

const char SongLibrary::INVALID_NAME_MESSAGE[] = "Name: 1-23 letters, digits, spaces, - _ ( ) .";

String SongLibrary::pathFor(const char *name) { return String(SONG_DIR) + "/" + name + SONG_EXTENSION; }

bool SongLibrary::validName(const char *name) {
  size_t length = strlen(name);
  if (length == 0 || length > MAX_NAME || name[0] == '.' || name[0] == ' ') return false;
  for (size_t i = 0; i < length; i++) {
    char c = name[i];
    if (!(isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_' || c == '(' || c == ')' || c == '.'))
      return false;
  }
  return true;
}

// Keeps the file name without folders and extension, replaces anything unusual with '_'.
void SongLibrary::cleanName(const char *in, char *out, size_t size) {
  const char *base = in;
  for (const char *p = in; *p; p++)
    if (*p == '/' || *p == '\\') base = p + 1;
  size_t length = strlen(base);
  const char *dot = strrchr(base, '.');
  if (dot && dot != base) length = dot - base;
  size_t outLength = 0;
  for (size_t i = 0; i < length && outLength + 1 < size && outLength < MAX_NAME; i++) {
    char c = base[i];
    bool allowed = isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_' || c == '(' || c == ')';
    out[outLength++] = allowed ? c : '_';
  }
  while (outLength && out[outLength - 1] == ' ') outLength--;
  out[outLength] = 0;
  if (!outLength) strlcpy(out, "song", size);
}

// Checks the header and size, and finds the song length (latest note end).
bool SongLibrary::readInfo(File &file, SongInfo &info, String &error) {
  uint8_t header[HEADER_BYTES];
  if (file.read(header, HEADER_BYTES) != HEADER_BYTES || memcmp(header, "STPM", 4)) {
    error = "Not a .stepper file";
    return false;
  }
  uint32_t eventCount;
  memcpy(&eventCount, header + 8, 4);
  if (file.size() != HEADER_BYTES + (size_t)eventCount * EVENT_BYTES) {
    error = "File is incomplete or damaged";
    return false;
  }
  // Read in blocks: one 8-byte event at a time is slow on LittleFS.
  uint32_t lengthMs = 0;
  uint8_t block[256];
  uint32_t eventsLeft = eventCount;
  while (eventsLeft) {
    uint32_t take = min<uint32_t>(eventsLeft, sizeof(block) / EVENT_BYTES);
    if (file.read(block, take * EVENT_BYTES) != take * EVENT_BYTES) {
      error = "Read error";
      return false;
    }
    for (uint32_t i = 0; i < take; i++) {
      uint32_t timeMs;
      uint16_t durationMs;
      memcpy(&timeMs, block + EVENT_BYTES * i, 4);
      memcpy(&durationMs, block + EVENT_BYTES * i + 6, 2);
      lengthMs = max(lengthMs, timeMs + durationMs);
    }
    eventsLeft -= take;
  }
  info.notes = eventCount;
  info.lengthMs = lengthMs;
  info.bytes = file.size();
  return true;
}

// Rebuilds the song list from the files (after every change).
void SongLibrary::scan() {
  songs_.clear();
  Dir dir = LittleFS.openDir(SONG_DIR);
  while (dir.next()) {
    String fileName = dir.fileName();
    if (!fileName.endsWith(SONG_EXTENSION) || fileName.startsWith(".")) continue;
    SongInfo info = {};
    strlcpy(info.name, fileName.substring(0, fileName.length() - strlen(SONG_EXTENSION)).c_str(), sizeof(info.name));
    File file = dir.openFile("r");
    String error;
    if (readInfo(file, info, error)) songs_.push_back(info);
    else Serial.printf("[SONGS] skipping %s: %s\n", fileName.c_str(), error.c_str());
    file.close();
  }
  std::sort(songs_.begin(), songs_.end(),
            [](const SongInfo &a, const SongInfo &b) { return strcasecmp(a.name, b.name) < 0; });
}

bool SongLibrary::begin() {
  if (!LittleFS.begin()) {
    Serial.println(F("[SONGS] file system not available"));
    return false;
  }
  LittleFS.mkdir(SONG_DIR);
  LittleFS.mkdir("/config");
  LittleFS.remove(UPLOAD_TEMP_PATH);  // left over from an interrupted upload
  installDemosOnFirstStart();
  scan();
  Serial.printf("[SONGS] %u songs, %lu of %lu KB used\n", (unsigned)songs_.size(),
                (unsigned long)(usedBytes() / 1024), (unsigned long)(totalBytes() / 1024));
  return true;
}

// Only once: demo songs the user deleted stay deleted after a restart.
void SongLibrary::installDemosOnFirstStart() {
  if (LittleFS.exists(DEMOS_MARKER_PATH)) return;
  int installed = installDemos(false);
  File marker = LittleFS.open(DEMOS_MARKER_PATH, "w");
  marker.print("1");
  marker.close();
  Serial.printf("[SONGS] first start: installed %d demo songs\n", installed);
}

int SongLibrary::installDemos(bool overwrite) {
  int written = 0;
  for (size_t i = 0; i < EMBEDDED_SONG_COUNT; i++) {
    const EmbeddedSong &song = EMBEDDED_SONGS[i];
    String path = pathFor(song.name);
    if (!overwrite && LittleFS.exists(path)) continue;
    if (writeEmbeddedSong(path.c_str(), song.data, song.size)) written++;
  }
  scan();
  return written;
}

// Copies a song from PROGMEM to a file, through a RAM block (flash data must be read with memcpy_P).
bool SongLibrary::writeEmbeddedSong(const char *path, const uint8_t *data, size_t size) {
  File file = LittleFS.open(path, "w");
  if (!file) return false;
  uint8_t block[256];
  for (size_t offset = 0; offset < size; offset += sizeof(block)) {
    size_t take = min(sizeof(block), size - offset);
    memcpy_P(block, data + offset, take);
    file.write(block, take);
  }
  file.close();
  return true;
}

const SongInfo *SongLibrary::find(const char *name) const {
  for (const SongInfo &song : songs_)
    if (!strcasecmp(song.name, name)) return &song;
  return nullptr;
}

File SongLibrary::open(const char *name) const {
  const SongInfo *song = find(name);
  return song ? LittleFS.open(pathFor(song->name), "r") : File();
}

bool SongLibrary::remove(const char *name, String &error) {
  const SongInfo *song = find(name);
  if (!song) { error = "No such song"; return false; }
  if (!LittleFS.remove(pathFor(song->name))) { error = "Could not delete"; return false; }
  scan();
  return true;
}

bool SongLibrary::rename(const char *from, const char *to, String &error) {
  const SongInfo *song = find(from);
  if (!song) { error = "No such song"; return false; }
  if (!validName(to)) { error = INVALID_NAME_MESSAGE; return false; }
  const SongInfo *other = find(to);
  if (other && other != song) { error = "A song with that name already exists"; return false; }
  if (!LittleFS.rename(pathFor(song->name), pathFor(to))) { error = "Could not rename"; return false; }
  scan();
  return true;
}

// ---------------------------------------------------------------- upload

bool SongLibrary::beginUpload(const char *name, String &error) {
  abortUpload();
  if (!validName(name)) { error = INVALID_NAME_MESSAGE; return false; }
  upload_ = LittleFS.open(UPLOAD_TEMP_PATH, "w");
  if (!upload_) { error = "Could not create the file"; return false; }
  strlcpy(uploadName_, name, sizeof(uploadName_));
  uploadFailed_ = false;
  uploadBytes_ = 0;
  // Space check once per upload: LittleFS.info() walks the whole file system (tens of ms), and
  // doing it for every ~1 KB chunk stalled the main loop for ~10 s per song (tested 2026-10-02).
  uint32_t used = usedBytes(), total = totalBytes();
  uploadLimit_ = total > used + UPLOAD_SPACE_RESERVE ? total - used - UPLOAD_SPACE_RESERVE : 0;
  return true;
}

bool SongLibrary::writeUpload(const uint8_t *data, size_t length) {
  if (!upload_ || uploadFailed_) return false;
  if (uploadBytes_ + length > uploadLimit_ || upload_.write(data, length) != length) {
    uploadFailed_ = true;  // reported by endUpload
    return false;
  }
  uploadBytes_ += length;
  return true;
}

bool SongLibrary::endUpload(String &error) {
  if (!upload_) { error = "No upload in progress"; return false; }
  upload_.close();
  if (uploadFailed_) {
    LittleFS.remove(UPLOAD_TEMP_PATH);
    error = "Not enough space for this song";
    return false;
  }
  File file = LittleFS.open(UPLOAD_TEMP_PATH, "r");
  SongInfo info = {};
  bool ok = readInfo(file, info, error);
  file.close();
  if (!ok) {
    LittleFS.remove(UPLOAD_TEMP_PATH);
    return false;
  }
  String path = pathFor(uploadName_);
  LittleFS.remove(path);  // uploading again under the same name replaces the song
  if (!LittleFS.rename(UPLOAD_TEMP_PATH, path)) {
    LittleFS.remove(UPLOAD_TEMP_PATH);
    error = "Could not save";
    return false;
  }
  Serial.printf("[SONGS] saved '%s': %lu notes, %.1f s\n", uploadName_, (unsigned long)info.notes, info.lengthMs / 1000.0);
  scan();
  return true;
}

void SongLibrary::abortUpload() {
  if (upload_) {
    upload_.close();
    LittleFS.remove(UPLOAD_TEMP_PATH);
  }
  uploadFailed_ = false;
}

uint32_t SongLibrary::usedBytes() const {
  FSInfo info;
  return LittleFS.info(info) ? info.usedBytes : 0;
}

uint32_t SongLibrary::totalBytes() const {
  FSInfo info;
  return LittleFS.info(info) ? info.totalBytes : 0;
}

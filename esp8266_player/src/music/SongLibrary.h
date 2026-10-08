// The songs stored in the ESP's flash (LittleFS) as /songs/<name>.stepper.
//
// On the very first start (no /config/demos-installed yet) the demo songs built into the
// firmware (EmbeddedSongs.cpp) are copied there; "Restore demo songs" copies them again.
// Nothing else touches the song files: the player and the web page go through this class,
// which keeps a sorted list of the valid songs in RAM.
//
// .stepper format: 12-byte header ("STPM", version, 3 reserved bytes, uint32 event count),
// then 8-byte note events (uint32 time_ms, uint8 motor, uint8 note, uint16 duration_ms), all
// little endian.
#pragma once
#include <Arduino.h>
#include <FS.h>
#include <vector>

struct SongInfo {
  char name[24];      // file name without ".stepper"
  uint32_t notes;
  uint32_t lengthMs;  // end of the latest note
  uint32_t bytes;
};

class SongLibrary {
public:
  static const size_t MAX_NAME = 23;  // LittleFS limits a file name to 31 characters incl. ".stepper"
  static constexpr uint32_t HEADER_BYTES = 12;
  static constexpr uint32_t EVENT_BYTES = 8;
  static const char INVALID_NAME_MESSAGE[];  // for song and playlist names

  bool begin();  // mount the file system, install demos on first start, read the song list

  size_t count() const { return songs_.size(); }
  const SongInfo &at(size_t i) const { return songs_[i]; }
  const SongInfo *find(const char *name) const;  // case-insensitive
  File open(const char *name) const;

  bool remove(const char *name, String &error);
  bool rename(const char *from, const char *to, String &error);
  int installDemos(bool overwrite);  // returns how many were written

  // Upload, in chunks: the file is written under a temporary name and only appears in the
  // list after it has been checked.
  bool beginUpload(const char *name, String &error);
  bool writeUpload(const uint8_t *data, size_t length);
  bool endUpload(String &error);
  void abortUpload();

  // Each call walks the whole file system (tens of ms): not for every loop or upload chunk.
  uint32_t usedBytes() const;
  uint32_t totalBytes() const;

  static bool validName(const char *name);
  static void cleanName(const char *in, char *out, size_t size);  // uploaded file name -> song name

private:
  void scan();
  void installDemosOnFirstStart();
  static bool writeEmbeddedSong(const char *path, const uint8_t *data, size_t size);
  static String pathFor(const char *name);
  static bool readInfo(File &file, SongInfo &info, String &error);

  std::vector<SongInfo> songs_;
  File upload_;
  char uploadName_[24] = "";
  bool uploadFailed_ = false;
  size_t uploadBytes_ = 0;
  size_t uploadLimit_ = 0;  // free space at the start of the upload, minus a reserve
};

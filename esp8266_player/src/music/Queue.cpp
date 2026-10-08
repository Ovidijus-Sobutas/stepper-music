#include "Queue.h"
#include <LittleFS.h>
#include <algorithm>

static const char PLAYLIST_DIR[] = "/playlists";
static const char SETTINGS_FILE[] = "/config/queue.txt";
static const uint8_t MAX_REST_SECONDS = 60;
static const size_t MAX_PLAYLIST_SONGS = 200;

static String playlistPath(const char *name) { return String(PLAYLIST_DIR) + "/" + name + ".txt"; }

// ---------------------------------------------------------------- settings

void Queue::begin() {
  LittleFS.mkdir(PLAYLIST_DIR);
  File file = LittleFS.open(SETTINGS_FILE, "r");
  if (!file) return;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.startsWith("repeat=")) repeat_ = (Repeat)constrain(line.substring(7).toInt(), 0, 2);
    else if (line.startsWith("rest=")) restSeconds_ = constrain(line.substring(5).toInt(), 0, MAX_REST_SECONDS);
  }
  file.close();
}

void Queue::saveSettings() {
  File file = LittleFS.open(SETTINGS_FILE, "w");
  if (!file) return;
  file.printf("repeat=%d\nrest=%u\n", (int)repeat_, restSeconds_);
  file.close();
}

const char *Queue::repeatName(Repeat repeat) {
  return repeat == Repeat::ALL ? "all" : repeat == Repeat::ONE ? "one" : "off";
}

void Queue::setRepeat(Repeat repeat) {
  repeat_ = repeat;
  saveSettings();
}

void Queue::setRestSeconds(uint8_t seconds) {
  restSeconds_ = seconds > MAX_REST_SECONDS ? MAX_REST_SECONDS : seconds;
  saveSettings();
}

// ---------------------------------------------------------------- starting and moving

bool Queue::start(std::vector<String> songs, bool shuffle, const char *source) {
  songNames_.clear();
  for (String &song : songs)
    if (library_.find(song.c_str())) songNames_.push_back(song);  // songs deleted since are skipped
  if (songNames_.empty()) return false;
  playOrder_.resize(songNames_.size());
  for (size_t i = 0; i < playOrder_.size(); i++) playOrder_[i] = i;
  shuffle_ = shuffle;
  if (shuffle) shuffleOrder(-1);
  strlcpy(sourceName_, source, sizeof(sourceName_));
  active_ = true;
  nextSongAtMs_ = 0;
  Serial.printf("[QUEUE] %s: %u songs%s, repeat %s\n", sourceName_, (unsigned)playOrder_.size(),
                shuffle ? " shuffled" : "", repeatName(repeat_));
  return playAt(0);
}

// Random order; the first song of a new round is never the one that just played.
void Queue::shuffleOrder(int avoidFirst) {
  for (size_t i = playOrder_.size(); i > 1; i--) {
    size_t j = RANDOM_REG32 % i;  // hardware random number generator
    std::swap(playOrder_[i - 1], playOrder_[j]);
  }
  if (avoidFirst >= 0 && playOrder_.size() > 1 && playOrder_[0] == avoidFirst) std::swap(playOrder_[0], playOrder_[1]);
}

bool Queue::playAt(size_t position) {
  position_ = position;
  nextSongAtMs_ = 0;
  return player_.play(songNames_[playOrder_[position_]].c_str());
}

bool Queue::playSong(const char *name) {
  if (!library_.find(name)) return false;
  return start({String(name)}, false, "Single song");
}

bool Queue::playAll(bool shuffle) {
  std::vector<String> songs;
  for (size_t i = 0; i < library_.count(); i++) songs.push_back(library_.at(i).name);
  return start(songs, shuffle, "All songs");
}

bool Queue::playPlaylist(const char *name, bool shuffle, String &error) {
  for (const Playlist &playlist : playlists()) {
    if (!playlist.name.equalsIgnoreCase(name)) continue;
    if (!start(playlist.songs, shuffle, playlist.name.c_str())) {
      error = "None of its songs are on the player any more";
      return false;
    }
    return true;
  }
  error = "No such playlist";
  return false;
}

// step = +1 (next) or -1 (previous). Wraps around with repeat all (reshuffling each round).
bool Queue::advance(int step) {
  if (!active_ || playOrder_.empty()) return false;
  long position = (long)position_ + step;
  if (position >= (long)playOrder_.size()) {
    if (repeat_ != Repeat::ALL) return false;
    if (shuffle_) shuffleOrder(playOrder_[position_]);
    position = 0;
  } else if (position < 0) {
    position = repeat_ == Repeat::ALL ? (long)playOrder_.size() - 1 : 0;
  }
  return playAt((size_t)position);
}

void Queue::next() {
  if (!active_) return;
  if (!advance(+1)) stop();
}

void Queue::prev() {
  if (!active_) return;
  advance(-1);
}

void Queue::stop() {
  active_ = false;
  nextSongAtMs_ = 0;
  player_.stop();
}

// ---------------------------------------------------------------- main loop

void Queue::poll() {
  if (!active_) return;
  if (player_.playingSong() || player_.testing()) {
    seenEndCount_ = player_.endCount();  // ends caused by switching songs are not "song over"
    nextSongAtMs_ = 0;
    return;
  }
  if (nextSongAtMs_) startNextAfterRest();
  else if (player_.endCount() != seenEndCount_) handleSongEnded();
}

void Queue::startNextAfterRest() {
  if ((int32_t)(millis() - nextSongAtMs_) < 0) return;
  nextSongAtMs_ = 0;
  bool more = repeat_ == Repeat::ONE ? playAt(position_) : advance(+1);
  if (!more) {
    active_ = false;
    Serial.println(F("[QUEUE] finished"));
  }
}

void Queue::handleSongEnded() {
  seenEndCount_ = player_.endCount();
  if (player_.lastEnd() != Player::EndReason::FINISHED) {  // stopped by the user, or an error
    active_ = false;
    Serial.println(F("[QUEUE] stopped"));
    return;
  }
  nextSongAtMs_ = millis() + (uint32_t)restSeconds_ * 1000 + 1;  // +1: never 0
}

String Queue::upcoming() const {
  if (!active_ || playOrder_.empty()) return "";
  if (repeat_ == Repeat::ONE) return songNames_[playOrder_[position_]];
  if (position_ + 1 < playOrder_.size()) return songNames_[playOrder_[position_ + 1]];
  return repeat_ == Repeat::ALL ? "(next round)" : "";
}

uint32_t Queue::restLeftMs() const {
  if (!nextSongAtMs_) return 0;
  int32_t left = (int32_t)(nextSongAtMs_ - millis());
  return left > 0 ? left : 0;
}

// ---------------------------------------------------------------- playlists

Queue::Playlist Queue::readPlaylist(const String &name, File &file) {
  Playlist playlist;
  playlist.name = name;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length()) playlist.songs.push_back(line);
  }
  return playlist;
}

std::vector<Queue::Playlist> Queue::playlists() const {
  std::vector<Playlist> result;
  Dir dir = LittleFS.openDir(PLAYLIST_DIR);
  while (dir.next()) {
    String fileName = dir.fileName();
    if (!fileName.endsWith(".txt")) continue;
    File file = dir.openFile("r");
    result.push_back(readPlaylist(fileName.substring(0, fileName.length() - 4), file));
    file.close();
  }
  std::sort(result.begin(), result.end(),
            [](const Playlist &a, const Playlist &b) { return a.name.compareTo(b.name) < 0; });
  return result;
}

bool Queue::savePlaylist(const char *name, const std::vector<String> &songs, String &error) {
  if (!SongLibrary::validName(name)) { error = SongLibrary::INVALID_NAME_MESSAGE; return false; }
  if (songs.empty()) { error = "Add at least one song"; return false; }
  if (songs.size() > MAX_PLAYLIST_SONGS) { error = "At most 200 songs"; return false; }
  File file = LittleFS.open(playlistPath(name), "w");
  if (!file) { error = "Could not save"; return false; }
  for (const String &song : songs) file.println(song);
  file.close();
  Serial.printf("[QUEUE] playlist '%s' saved: %u songs\n", name, (unsigned)songs.size());
  return true;
}

bool Queue::deletePlaylist(const char *name, String &error) {
  if (!SongLibrary::validName(name) || !LittleFS.remove(playlistPath(name))) {
    error = "No such playlist";
    return false;
  }
  return true;
}

// Keeps playlists pointing at a song after it was renamed.
void Queue::renameSongInPlaylists(const char *from, const char *to) {
  for (Playlist &playlist : playlists()) {
    bool changed = false;
    for (String &song : playlist.songs) {
      if (!song.equalsIgnoreCase(from)) continue;
      song = to;
      changed = true;
    }
    if (changed) {
      String error;
      savePlaylist(playlist.name.c_str(), playlist.songs, error);
    }
  }
}

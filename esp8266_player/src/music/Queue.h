// What plays after the current song: one song, all songs, or a playlist, in order or shuffled,
// with repeat (off / all / one) and an optional rest between songs (motor cooldown).
// Watches the Player's end counter: a song that FINISHED moves the queue on, a stop or an error
// ends it. Settings are saved in /config/queue.txt; playlists are text files
// /playlists/<name>.txt with one song name per line.
#pragma once
#include <Arduino.h>
#include <vector>
#include "Player.h"
#include "SongLibrary.h"

class Queue {
public:
  enum class Repeat : uint8_t { OFF, ALL, ONE };  // saved as 0/1/2

  Queue(Player &player, SongLibrary &library) : player_(player), library_(library) {}
  void begin();  // loads repeat / rest settings
  void poll();   // call every loop: starts the next song when one finishes

  bool playSong(const char *name);  // just this song (repeat still applies)
  bool playAll(bool shuffle);       // the whole library
  bool playPlaylist(const char *name, bool shuffle, String &error);
  void next();
  void prev();
  void stop();  // ends the queue and stops the music

  void setRepeat(Repeat repeat);
  Repeat repeat() const { return repeat_; }
  static const char *repeatName(Repeat repeat);
  void setRestSeconds(uint8_t seconds);
  uint8_t restSeconds() const { return restSeconds_; }

  bool active() const { return active_; }
  bool shuffled() const { return shuffle_; }
  size_t position() const { return position_ + 1; }  // 1-based, for display
  size_t size() const { return playOrder_.size(); }
  const char *source() const { return sourceName_; }  // "All songs", "Single song" or the playlist name
  String upcoming() const;      // name of the song after the current one ("" if none)
  uint32_t restLeftMs() const;  // > 0 while waiting between songs

  // Playlists
  struct Playlist { String name; std::vector<String> songs; };
  std::vector<Playlist> playlists() const;  // sorted by name
  bool savePlaylist(const char *name, const std::vector<String> &songs, String &error);
  bool deletePlaylist(const char *name, String &error);
  void renameSongInPlaylists(const char *from, const char *to);

private:
  bool start(std::vector<String> songs, bool shuffle, const char *source);
  void shuffleOrder(int avoidFirst);
  bool playAt(size_t position);
  bool advance(int step);  // move position_ by step and play; false when the queue is over
  void startNextAfterRest();
  void handleSongEnded();
  void saveSettings();
  static Playlist readPlaylist(const String &name, File &file);

  Player &player_;
  SongLibrary &library_;
  std::vector<String> songNames_;
  std::vector<uint16_t> playOrder_;  // indices into songNames_
  size_t position_ = 0;              // index into playOrder_
  bool active_ = false;
  bool shuffle_ = false;
  char sourceName_[28] = "";
  Repeat repeat_ = Repeat::OFF;
  uint8_t restSeconds_ = 2;
  uint32_t seenEndCount_ = 0;  // the Player's endCount when we last looked
  uint32_t nextSongAtMs_ = 0;  // > 0: resting, start the next song at this time
};

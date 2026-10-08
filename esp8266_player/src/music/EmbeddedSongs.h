// Demo songs compiled into flash (PROGMEM), in the .stepper format described in SongLibrary.h.
// Only SongLibrary reads them, to install them into LittleFS.
#pragma once
#include <Arduino.h>

struct EmbeddedSong {
  const char *name;
  const uint8_t *data;  // PROGMEM: read with memcpy_P
  size_t size;
};

extern const EmbeddedSong EMBEDDED_SONGS[];
extern const size_t EMBEDDED_SONG_COUNT;

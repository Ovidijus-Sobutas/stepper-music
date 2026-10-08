// How the motors play ("sound style"): the built-in styles and the current motor drive settings.
//
// The GT2560 forgets its settings when it restarts, so the ESP keeps them (saved in
// /config/sound.txt) and sends them again: at start, after a GT2560 restart and whenever a
// setting changes. Each setting goes as one "CFG,seq,key,value" frame, sent by the Player
// between the notes, so a change applies from the next note, also during a song.
#pragma once
#include <Arduino.h>

struct SoundSettings {
  uint8_t mode;            // 0 = even (continuous rotation), 1 = push (one burst per note)
  uint8_t group;           // even: microsteps sent together, 1 (smooth) .. 16 (full step)
  int8_t octave;           // -2..+2
  uint8_t level;           // push: burst size = level * 2 (loudness)
  uint16_t stepSpacingUs;  // push: time between the steps of a burst
  uint8_t rampPeriods;     // push: soft start, note periods
  uint16_t maxFullSteps;   // push: stall guard, full steps per second (0 = off)
  uint16_t releaseMs;      // a silent motor is switched off after this long
  uint8_t microsteps;      // driver microstepping (STEP_SIZE jumpers)
};

struct SoundStyle {
  const char *id;
  const char *name;
  const char *description;
  SoundSettings settings;
};

class Sound {
public:
  void begin();  // load the saved style

  const SoundSettings &settings() const { return settings_; }
  const char *styleId() const { return styleId_; }  // a style id, or "custom"
  const char *styleName() const;

  static const SoundStyle *styles(size_t &count);
  bool applyStyle(const char *id);
  // Change one setting; the style becomes "custom" unless the result equals a built-in style.
  // Keys (as in the link protocol and the web API): mode group oct level spacing ramp maxfs
  // release micro.
  bool set(const char *key, long value, String &error);

  // Link sync: the next "key,value" for a CFG frame, or false when the GT2560 is up to date.
  bool nextUnsentSetting(char *args, size_t size, uint8_t &keyIndex, long &value);
  void markSent(uint8_t keyIndex, long value);  // call when the GT2560 acknowledged that frame
  bool hasUnsentSettings() const { return unsentMask_ != 0; }
  void resendAll() { unsentMask_ = ALL_SETTINGS; }

private:
  static const uint8_t SETTING_COUNT = 9;
  static const uint16_t ALL_SETTINGS = (1u << SETTING_COUNT) - 1;
  void save();
  void load();
  void markChanged(const SoundSettings &before);
  void updateStyleId();

  SoundSettings settings_;
  char styleId_[16] = "smooth";
  uint16_t unsentMask_ = ALL_SETTINGS;  // bit i = setting i still has to be sent to the GT2560
  bool loading_ = false;
};

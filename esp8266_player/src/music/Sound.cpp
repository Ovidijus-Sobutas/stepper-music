#include "Sound.h"
#include <LittleFS.h>
#include "../config.h"

static const char SOUND_FILE[] = "/config/sound.txt";

// A continuous-rotation style; the push settings only matter in push mode.
#define EVEN_STYLE(group, octave)                                                                        \
  {0, group, octave, DEFAULT_LEVEL, DEFAULT_STEP_SPACING_US, DEFAULT_RAMP_PERIODS, DEFAULT_MAX_FULL_STEPS, \
   DEFAULT_RELEASE_MS, DEFAULT_MICROSTEPS}

// Only continuous-rotation styles with groups of 1-4: bursts (push, classic) and groups of 8/16
// move the rotor in jerks and sounded like grinding gears (user test 2026-10-02). In continuous
// rotation the speed is tied to the pitch, so "fast" means one octave higher.
// The first style is the default.
static const SoundStyle STYLES[] = {
    {"smooth", "Smooth", "Continuous rotation, one microstep at a time. Smoothest and quietest.", EVEN_STYLE(1, 0)},
    {"soft", "Soft", "Microsteps in pairs: still smooth, a little more body.", EVEN_STYLE(2, 0)},
    {"balanced", "Balanced", "Microsteps in groups of 4 (like 1/4 stepping): smooth with more presence.", EVEN_STYLE(4, 0)},
    {"fastsmooth", "Fast & smooth", "Smooth, one octave higher: the motors turn twice as fast.", EVEN_STYLE(1, 1)},
    {"fastsoft", "Fast & soft", "Soft, one octave higher.", EVEN_STYLE(2, 1)},
    {"fastbalanced", "Fast & balanced", "Balanced, one octave higher.", EVEN_STYLE(4, 1)},
    {"smoothlow", "Smooth low", "Smooth, one octave lower: slower, deeper motors.", EVEN_STYLE(1, -1)},
    {"deep", "Deep", "Balanced, one octave lower.", EVEN_STYLE(4, -1)},
};
static const size_t STYLE_COUNT = sizeof(STYLES) / sizeof(STYLES[0]);

// Index = bit in unsentMask_ and the order they are sent in: "mode" first so the rest applies to
// the right mode. "micro" is last: it describes the hardware, not the style.
static const char *const SETTING_KEYS[] = {"mode", "group", "oct", "level", "spacing", "ramp", "maxfs", "release", "micro"};
static const uint8_t MICRO_INDEX = 8;

static long settingValue(const SoundSettings &settings, uint8_t index) {
  switch (index) {
    case 0: return settings.mode;
    case 1: return settings.group;
    case 2: return settings.octave;
    case 3: return settings.level;
    case 4: return settings.stepSpacingUs;
    case 5: return settings.rampPeriods;
    case 6: return settings.maxFullSteps;
    case 7: return settings.releaseMs;
    default: return settings.microsteps;
  }
}

static bool isPowerOfTwoUpTo(long value, long max) { return value >= 1 && value <= max && (value & (value - 1)) == 0; }

const SoundStyle *Sound::styles(size_t &count) {
  count = STYLE_COUNT;
  return STYLES;
}

const char *Sound::styleName() const {
  for (const SoundStyle &style : STYLES)
    if (!strcmp(style.id, styleId_)) return style.name;
  return "Custom";
}

void Sound::begin() {
  settings_ = STYLES[0].settings;
  load();
  // A saved style that no longer exists (removed in an update) falls back to the default.
  bool known = !strcmp(styleId_, "custom");
  for (const SoundStyle &style : STYLES) known |= !strcmp(style.id, styleId_);
  if (!known) applyStyle(STYLES[0].id);
  unsentMask_ = ALL_SETTINGS;
  Serial.printf("[SOUND] style: %s\n", styleName());
}

bool Sound::applyStyle(const char *id) {
  for (const SoundStyle &style : STYLES) {
    if (strcmp(style.id, id)) continue;
    SoundSettings before = settings_;
    uint8_t microsteps = settings_.microsteps;  // microstepping follows the jumpers, not the style
    settings_ = style.settings;
    settings_.microsteps = microsteps;
    strlcpy(styleId_, style.id, sizeof(styleId_));
    markChanged(before);
    save();
    Serial.printf("[SOUND] style: %s\n", style.name);
    return true;
  }
  return false;
}

// Ranges match what the GT2560 accepts (except level, which the GT2560 also allows up to 20).
bool Sound::set(const char *key, long value, String &error) {
  SoundSettings before = settings_;
  if (!strcmp(key, "mode") && (value == 0 || value == 1)) settings_.mode = value;
  else if (!strcmp(key, "group") && isPowerOfTwoUpTo(value, 16)) settings_.group = value;
  else if (!strcmp(key, "oct") && value >= -2 && value <= 2) settings_.octave = value;
  else if (!strcmp(key, "level") && value >= 1 && value <= 20) settings_.level = value;
  else if (!strcmp(key, "spacing") && value >= 12 && value <= 2000) settings_.stepSpacingUs = value;
  else if (!strcmp(key, "ramp") && value >= 0 && value <= 50) settings_.rampPeriods = value;
  else if (!strcmp(key, "maxfs") && value >= 0 && value <= 20000) settings_.maxFullSteps = value;
  else if (!strcmp(key, "release") && value >= 0 && value <= 60000) settings_.releaseMs = value;
  else if (!strcmp(key, "micro") && isPowerOfTwoUpTo(value, 16)) settings_.microsteps = value;
  else {
    error = String("Invalid value for ") + key;
    return false;
  }
  if (strcmp(key, "micro")) updateStyleId();  // micro describes the hardware; it does not make the style custom
  markChanged(before);
  if (!loading_) save();
  return true;
}

// The built-in style these settings equal (ignoring micro), or "custom".
void Sound::updateStyleId() {
  const char *match = "custom";
  for (const SoundStyle &style : STYLES) {
    bool same = true;
    for (uint8_t i = 0; i < MICRO_INDEX && same; i++) same = settingValue(style.settings, i) == settingValue(settings_, i);
    if (same) {
      match = style.id;
      break;
    }
  }
  strlcpy(styleId_, match, sizeof(styleId_));
}

void Sound::markChanged(const SoundSettings &before) {
  for (uint8_t i = 0; i < SETTING_COUNT; i++)
    if (settingValue(before, i) != settingValue(settings_, i)) unsentMask_ |= 1u << i;
}

bool Sound::nextUnsentSetting(char *args, size_t size, uint8_t &keyIndex, long &value) {
  for (uint8_t i = 0; i < SETTING_COUNT; i++) {
    if (!(unsentMask_ & (1u << i))) continue;
    value = settingValue(settings_, i);
    snprintf(args, size, "%s,%ld", SETTING_KEYS[i], value);
    keyIndex = i;
    return true;
  }
  return false;
}

void Sound::markSent(uint8_t keyIndex, long value) {
  // Only done if the setting was not changed again while its frame was on the way.
  if (keyIndex < SETTING_COUNT && settingValue(settings_, keyIndex) == value) unsentMask_ &= ~(1u << keyIndex);
}

// File format: "style=<id>" then one "key=value" line per setting.
void Sound::save() {
  File file = LittleFS.open(SOUND_FILE, "w");
  if (!file) return;
  file.printf("style=%s\n", styleId_);
  for (uint8_t i = 0; i < SETTING_COUNT; i++) file.printf("%s=%ld\n", SETTING_KEYS[i], settingValue(settings_, i));
  file.close();
}

void Sound::load() {
  File file = LittleFS.open(SOUND_FILE, "r");
  if (!file) return;
  loading_ = true;
  String error;
  char savedStyle[16] = "";
  while (file.available()) {
    String line = file.readStringUntil('\n');
    int equals = line.indexOf('=');
    if (equals < 1) continue;
    String key = line.substring(0, equals), value = line.substring(equals + 1);
    value.trim();
    if (key == "style") strlcpy(savedStyle, value.c_str(), sizeof(savedStyle));
    else set(key.c_str(), value.toInt(), error);
  }
  file.close();
  loading_ = false;
  if (savedStyle[0]) strlcpy(styleId_, savedStyle, sizeof(styleId_));
}

#include "Console.h"
#include <LittleFS.h>
#include "../config.h"

static const uint32_t RESTART_DELAY_MS = 500;  // lets the STOP to the GT2560 go out first

void Console::poll() {
  restartWhenDue();
  readLine();
}

void Console::restartWhenDue() {
  if (!restartAtMs_ || (int32_t)(millis() - restartAtMs_) < 0) return;
  if (factoryResetPending_) {
    Serial.println(F("[CONSOLE] factory reset: erasing the file system"));
    LittleFS.format();
  }
  Serial.println(F("[CONSOLE] restarting"));
  Serial.flush();
  ESP.restart();
}

// Collects characters until a newline (CR ignored); too long lines are cut off.
void Console::readLine() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      lineBuffer_[lineLength_] = 0;
      if (lineLength_) run(lineBuffer_);
      lineLength_ = 0;
    } else if (lineLength_ < sizeof(lineBuffer_) - 1) {
      lineBuffer_[lineLength_++] = c;
    }
  }
}

void Console::printHelp() {
  Serial.println(F("Music:"));
  Serial.println(F("  songs             list the songs"));
  Serial.println(F("  play <song>       play a song, e.g. play pirates"));
  Serial.println(F("  pause | resume | stop | next | prev"));
  Serial.println(F("  all | shuffle     play all songs, in order / shuffled"));
  Serial.println(F("  list <name> [shuffle]   play a playlist"));
  Serial.println(F("  repeat off|all|one      repeat mode (saved)"));
  Serial.println(F("  rest <0-60>       seconds of rest between songs (saved)"));
  Serial.println(F("  level <1-10>      loudness in push mode (default 5, saved)"));
  Serial.println(F("  test [motor|all] [note] [ms]   test tone, default all motors, note 60, 1000 ms"));
  Serial.println(F("                    motor = 0-4 or X, Y, Z, E0, E1; note 0-127; ms 100-3000"));
  Serial.println(F("                    e.g. test 4, test E0 72 2000"));
  Serial.println(F("  status            player + GT2560 playback status"));
  Serial.println(F("Motor drive tuning, sent straight to the GT2560 (applies to the next note, not saved:"));
  Serial.println(F("the sound style from the web page is sent again after a style change or GT2560 restart):"));
  Serial.println(F("  cfg               show the settings"));
  Serial.println(F("  cfg even          continuous rotation (default)"));
  Serial.println(F("  cfg push          one push of level*2 steps per note period (level = loudness)"));
  Serial.println(F("  cfg group <n>     even: microsteps sent together, 1, 2, 4, 8, 16 (default 1)"));
  Serial.println(F("  cfg oct <n>       octave shift -2..2 (default 0)"));
  Serial.println(F("  cfg spacing <us>  push: time between the steps of one push (12-2000, default 25)"));
  Serial.println(F("  cfg ramp <n>      push: soft start over n note periods (0-50, 0 = off, default 8)"));
  Serial.println(F("  cfg maxfs <n>     push: stall guard, max full steps/s (0 = off, default 1500)"));
  Serial.println(F("  cfg micro <n>     driver microstepping: 1, 2, 4, 8, 16 (default 16)"));
  Serial.println(F("  cfg release <ms>  switch a silent motor off after this long (0-60000, default 400)"));
  Serial.println(F("  cfg budget <n>    CPU guard, max steps/s per motor (0-40000, default 24000)"));
  Serial.println(F("  cfg classic       old burst behaviour: push, spacing 12, no ramp, no stall guard"));
  Serial.println(F("  cfg default       back to the GT2560 defaults"));
  Serial.println(F("Wi-Fi:"));
  Serial.println(F("  wifi              Wi-Fi status and the saved networks"));
  Serial.println(F("  wifi remove <name>   remove one saved network"));
  Serial.println(F("  wifi forget       erase all saved networks, open the setup hotspot"));
  Serial.println(F("                    (same as holding FLASH for 10 s)"));
  Serial.println(F("System:"));
  Serial.println(F("  restart           restart the ESP8266"));
  Serial.println(F("  factory reset     erase songs, playlists, networks, settings; restart"));
  Serial.println(F("Link:"));
  Serial.println(F("  ping [n]          n pings (default 100), losses and round trip"));
  Serial.println(F("  ver | stat        GT2560 version / link counters of both sides"));
  Serial.println(F("  corrupt           send corrupted frames, check they are rejected"));
  Serial.println(F("  hb on|off         heartbeat on/off (off -> GT2560 times out after 6 s without frames)"));
  Serial.println(F("  raw <text>        send text without framing"));
}

// "command [argument...]": the argument is everything after the first space.
void Console::run(char *line) {
  Serial.printf("> %s\n", line);
  char *arg = strchr(line, ' ');
  if (arg) *arg++ = 0;

  if (!strcmp(line, "help")) {
    printHelp();
    return;
  }
  if (runMusicCommand(line, arg) || runSystemCommand(line, arg) || runLinkCommand(line, arg)) return;
  Serial.println(F("unknown command, type: help"));
}

bool Console::runMusicCommand(const char *command, char *arg) {
  if (!strcmp(command, "songs")) player_.listSongs();
  else if (!strcmp(command, "play") && arg) queue_.playSong(arg);
  else if (!strcmp(command, "all")) queue_.playAll(false);
  else if (!strcmp(command, "shuffle")) queue_.playAll(true);
  else if (!strcmp(command, "next")) queue_.next();
  else if (!strcmp(command, "prev")) queue_.prev();
  else if (!strcmp(command, "list") && arg) runPlaylistCommand(arg);
  else if (!strcmp(command, "repeat") && arg)
    queue_.setRepeat(!strcmp(arg, "all") ? Queue::Repeat::ALL : !strcmp(arg, "one") ? Queue::Repeat::ONE : Queue::Repeat::OFF);
  else if (!strcmp(command, "rest") && arg) queue_.setRestSeconds((uint8_t)atoi(arg));
  else if (!strcmp(command, "pause")) player_.pause();
  else if (!strcmp(command, "resume")) player_.resume();
  else if (!strcmp(command, "stop")) queue_.stop();
  else if (!strcmp(command, "level") && arg) player_.setLevel((uint8_t)atoi(arg));
  else if (!strcmp(command, "test")) runTestCommand(arg);
  else if (!strcmp(command, "status")) {
    player_.printStatus();
    gt_.send("PS");
  } else if (!strcmp(command, "cfg")) runCfgCommand(arg);
  else return false;
  return true;
}

bool Console::runSystemCommand(const char *command, char *arg) {
  if (!strcmp(command, "wifi")) {
    runWifiCommand(arg);
  } else if (!strcmp(command, "restart")) {
    queue_.stop();
    restartAtMs_ = millis() + RESTART_DELAY_MS;
  } else if (!strcmp(command, "factory") && arg && !strcmp(arg, "reset")) {
    queue_.stop();
    factoryResetPending_ = true;
    restartAtMs_ = millis() + RESTART_DELAY_MS;
  } else {
    return false;
  }
  return true;
}

bool Console::runLinkCommand(const char *command, char *arg) {
  if (!strcmp(command, "ping")) gt_.startPingBurst(arg ? (uint16_t)atoi(arg) : 100);
  else if (!strcmp(command, "ver")) gt_.send("VER");
  else if (!strcmp(command, "stat")) gt_.send("STAT");
  else if (!strcmp(command, "corrupt")) gt_.startCorruptionTest();
  else if (!strcmp(command, "hb")) gt_.setHeartbeat(arg && !strcmp(arg, "on"));
  else if (!strcmp(command, "raw") && arg) gt_.sendRaw(arg);
  else return false;
  return true;
}

// "list <name> [shuffle]" (playlist names may contain spaces).
void Console::runPlaylistCommand(const char *arg) {
  String name = arg, error;
  bool shuffle = name.endsWith(" shuffle");
  if (shuffle) name.remove(name.length() - 8);
  if (!queue_.playPlaylist(name.c_str(), shuffle, error)) Serial.println(error);
}

// "test [motor|all] [note] [ms]"
void Console::runTestCommand(char *arg) {
  int motor = -1;  // all
  uint8_t note = 60;
  uint16_t ms = 1000;
  if (arg) {
    char *rest = strchr(arg, ' ');
    if (rest) *rest++ = 0;
    if (strcasecmp(arg, "all")) {
      motor = -2;
      for (int i = 0; i < MOTOR_COUNT; i++)
        if (!strcasecmp(arg, MOTOR_NAMES[i])) motor = i;
      if (motor == -2 && isdigit(arg[0]) && arg[1] == 0 && arg[0] <= '4') motor = arg[0] - '0';
      if (motor == -2) {
        Serial.println(F("motor must be 0-4 or X, Y, Z, E0, E1"));
        return;
      }
    }
    if (rest) {
      note = (uint8_t)constrain(atoi(rest), 0, 127);
      char *msArg = strchr(rest, ' ');
      if (msArg) ms = (uint16_t)constrain(atoi(msArg + 1), 100, 3000);
    }
  }
  player_.startMotorTest(motor, note, ms);
}

// "cfg spacing 30" -> CFG frame "spacing,30"; plain "cfg" -> "show".
void Console::runCfgCommand(const char *arg) {
  char args[32] = "show";
  if (arg) {
    strlcpy(args, arg, sizeof(args));
    for (char *p = args; *p; p++)
      if (*p == ' ') *p = ',';
  }
  gt_.send("CFG", args);
}

void Console::runWifiCommand(const char *arg) {
  if (arg && !strcmp(arg, "forget")) {
    wifi_.forget();
  } else if (arg && !strncmp(arg, "remove ", 7)) {
    if (!wifi_.removeNetwork(arg + 7)) Serial.println(F("no such saved network"));
  } else {
    Serial.printf("[WIFI] %s  network='%s'  ip=%s  signal=%d dBm  hotspot=%s\n", wifi_.stateName(), wifi_.ssid(),
                  wifi_.ip().c_str(), wifi_.rssi(), wifi_.apActive() ? wifi_.apName() : "off");
    for (uint8_t i = 0; i < wifi_.networkCount(); i++)
      Serial.printf("[WIFI] saved %u: %s\n", i + 1, wifi_.networkName(i));
  }
}

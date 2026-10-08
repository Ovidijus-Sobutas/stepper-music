#include "Display.h"
#include <U8g2lib.h>
#include "../config.h"

static U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI oled(U8G2_R0, OLED_CLOCK_PIN, OLED_DATA_PIN, U8X8_PIN_NONE,
                                                   OLED_DC_PIN, OLED_RESET_PIN);

static const uint32_t PROGRESS_REFRESH_MS = 1000;
static const uint8_t PROGRESS_BAR_WIDTH = 124;  // inside the 128 px frame

static void formatTime(char *out, size_t size, uint32_t ms) {
  uint32_t seconds = ms / 1000;
  snprintf(out, size, "%02lu:%02lu", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
}

// FNV-1a hash of what would be drawn; the screen is only redrawn when it changes.
static const uint32_t HASH_START = 2166136261UL;
static uint32_t hashAdd(uint32_t hash, const void *data, size_t length) {
  const uint8_t *bytes = (const uint8_t *)data;
  for (size_t i = 0; i < length; i++) hash = (hash ^ bytes[i]) * 16777619UL;
  return hash;
}
static uint32_t hashText(uint32_t hash, const char *text) { return hashAdd(hash, text, strlen(text)); }

void Display::begin() {
  oled.begin();
  // OLEDs have no backlight: brightness = contrast (current per pixel) plus the pre-charge
  // period and the VCOMH level. All three at their maximum.
  oled.setContrast(255);
  oled.sendF("ca", 0xD9, 0xF1);  // pre-charge: phase 1 = 1, phase 2 = 15 clocks
  oled.sendF("ca", 0xDB, 0x40);  // VCOMH deselect level: highest setting
}

void Display::showBoot() {
  oled.clearBuffer();
  drawHeader("MUSIC PLAYER");
  oled.drawStr(0, 30, "ESP8266 fw " FW_VERSION);
  oled.drawStr(0, 44, "Connecting GT2560...");
  oled.sendBuffer();
}

// ---------------------------------------------------------------- update

// The layout (screen, song, pause, connection, level, messages...) gets a full redraw when it
// changes. During a song the progress (time and bar) changes every second; then only tile rows
// 2-4 are sent, which is much quicker than the whole screen.
void Display::update(const Player &player, const Gt2560Link &gt, const WifiManager &wifi, const Queue &queue) {
  uint32_t now = millis();
  if (now - lastDrawMs_ < DISPLAY_REFRESH_MS) return;

  int resetCountdown = wifi.resetCountdown();
  Screen screen = chooseScreen(resetCountdown, player, wifi, queue);
  uint32_t layout = layoutSignature(screen, resetCountdown, player, gt, wifi, queue);
  uint32_t progress = screen == Screen::SONG ? progressSignature(player, gt) : 0;

  bool fullRedraw = layout != layoutSignature_;
  bool progressRedraw = !fullRedraw && screen == Screen::SONG && progress != progressSignature_ &&
                        now - lastDrawMs_ >= PROGRESS_REFRESH_MS;
  if (!fullRedraw && !progressRedraw) return;
  layoutSignature_ = layout;
  progressSignature_ = progress;
  lastDrawMs_ = now;

  uint32_t startUs = micros();
  oled.clearBuffer();
  draw(screen, resetCountdown, player, gt, wifi, queue);
  if (fullRedraw) oled.sendBuffer();
  else oled.updateDisplayArea(0, 2, 16, 3);  // tile columns 0-15, rows 2-4 (y 16..39)
  reportDrawTime(fullRedraw, micros() - startUs);
}

// Priority: Wi-Fi reset countdown, song, up next (rest between songs), motor test, Wi-Fi setup, idle.
Display::Screen Display::chooseScreen(int resetCountdown, const Player &player, const WifiManager &wifi,
                                      const Queue &queue) {
  if (resetCountdown >= 0) return Screen::RESET_COUNTDOWN;
  if (player.playingSong()) return Screen::SONG;
  if (queue.restLeftMs() > 0) return Screen::UP_NEXT;
  if (player.testing()) return Screen::TEST;
  if (wifi.state() == WifiState::NOT_CONFIGURED) return Screen::WIFI_SETUP;
  return Screen::IDLE;
}

uint32_t Display::layoutSignature(Screen screen, int resetCountdown, const Player &player, const Gt2560Link &gt,
                                  const WifiManager &wifi, const Queue &queue) {
  uint32_t hash = HASH_START;
  bool gtConnected = gt.connected();
  uint8_t level = player.level();
  uint8_t wifiState = (uint8_t)wifi.state();
  bool apActive = wifi.apActive();
  hash = hashAdd(hash, &screen, 1);
  hash = hashAdd(hash, &gtConnected, 1);
  hash = hashAdd(hash, &level, 1);
  hash = hashText(hash, player.sound().styleId());
  hash = hashAdd(hash, &wifiState, 1);
  hash = hashAdd(hash, &apActive, 1);
  hash = hashAdd(hash, &resetCountdown, sizeof(resetCountdown));

  switch (screen) {
    case Screen::SONG: {
      bool paused = player.paused();
      hash = hashText(hash, player.songName());
      hash = hashAdd(hash, &paused, 1);
      uint32_t queueState[4] = {queue.active(), queue.position(), queue.size(),
                                (uint32_t)queue.repeat() * 2 + queue.shuffled()};
      hash = hashAdd(hash, queueState, sizeof(queueState));
      break;
    }
    case Screen::UP_NEXT: {
      uint32_t secondsLeft = (queue.restLeftMs() + 999) / 1000;
      hash = hashAdd(hash, &secondsLeft, 4);
      hash = hashText(hash, queue.upcoming().c_str());
      break;
    }
    case Screen::TEST: {
      int motor = player.testMotor();
      hash = hashAdd(hash, &motor, sizeof(motor));
      break;
    }
    case Screen::IDLE: {
      hash = hashText(hash, wifi.ip().c_str());
      uint32_t songCount = player.library().count();
      hash = hashAdd(hash, &songCount, 4);
      hash = hashText(hash, player.lastMessage());
      hash = hashText(hash, gt.gtVersion());
      break;
    }
    default:
      break;
  }
  return hash;
}

uint32_t Display::progressSignature(const Player &player, const Gt2560Link &gt) {
  uint32_t lengthMs = player.lengthMs() ? player.lengthMs() : 1;
  uint32_t seconds = gt.gtSongMs() / 1000;
  uint32_t barWidth = (uint32_t)PROGRESS_BAR_WIDTH * min(gt.gtSongMs(), lengthMs) / lengthMs;
  return hashAdd(hashAdd(HASH_START, &seconds, 4), &barWidth, 4);
}

void Display::draw(Screen screen, int resetCountdown, const Player &player, const Gt2560Link &gt,
                   const WifiManager &wifi, const Queue &queue) {
  switch (screen) {
    case Screen::SONG: drawSong(player, gt, queue); break;
    case Screen::UP_NEXT: drawUpNext(queue); break;
    case Screen::TEST: drawTest(player); break;
    case Screen::RESET_COUNTDOWN: drawResetCountdown(resetCountdown); break;
    case Screen::WIFI_SETUP: drawSetup(wifi); break;
    default: drawIdle(player, gt, wifi); break;
  }
}

// Logged once each, to see what the redraws cost the main loop.
void Display::reportDrawTime(bool fullRedraw, uint32_t us) {
  if (fullRedraw && !reportedFullDraw_) {
    reportedFullDraw_ = true;
    Serial.printf("[OLED] full screen update: %.1f ms\n", us / 1000.0);
  }
  if (!fullRedraw && !reportedPartialDraw_) {
    reportedPartialDraw_ = true;
    Serial.printf("[OLED] progress update: %.1f ms\n", us / 1000.0);
  }
}

// ---------------------------------------------------------------- screens

void Display::drawHeader(const char *title) {
  oled.setFont(u8g2_font_7x14B_tf);
  oled.drawStr(0, 12, title);
  oled.drawHLine(0, 15, 128);
  oled.setFont(u8g2_font_6x13B_tf);
}

static void formatWifiLine(char *out, size_t size, const WifiManager &wifi) {
  switch (wifi.state()) {
    case WifiState::CONNECTED: snprintf(out, size, "%s", wifi.ip().c_str()); break;
    case WifiState::CONNECTING: snprintf(out, size, "WiFi connecting..."); break;
    case WifiState::DISCONNECTED: snprintf(out, size, "WiFi lost, retrying"); break;
    default: snprintf(out, size, "WiFi not set up"); break;
  }
}

void Display::drawIdle(const Player &player, const Gt2560Link &gt, const WifiManager &wifi) {
  char line[32];
  drawHeader("MUSIC PLAYER");
  snprintf(line, sizeof(line), "Ready  %u songs", (unsigned)player.library().count());
  oled.drawStr(0, 28, line);
  if (player.lastMessage()[0]) {
    strlcpy(line, player.lastMessage(), 22);  // 21 characters fit
    oled.drawStr(0, 40, line);
  }
  if (gt.connected()) snprintf(line, sizeof(line), "GT2560 OK  v%s", gt.gtVersion());
  else strlcpy(line, "GT2560 NOT CONNECTED", sizeof(line));
  oled.drawStr(0, 52, line);
  if (wifi.apActive() && wifi.state() != WifiState::CONNECTED) snprintf(line, sizeof(line), "Setup: %s", wifi.apName());
  else formatWifiLine(line, sizeof(line), wifi);
  oled.drawStr(0, 63, line);
}

void Display::drawSetup(const WifiManager &wifi) {
  drawHeader("WIFI SETUP");
  oled.drawStr(0, 28, "Join Wi-Fi:");
  oled.setFont(u8g2_font_7x14B_tf);
  oled.drawStr(0, 41, wifi.apName());
  oled.setFont(u8g2_font_6x13B_tf);
  oled.drawStr(0, 53, "Pass: " WIFI_AP_PASSWORD);
  oled.drawStr(0, 64, "Open 192.168.4.1");
}

void Display::drawResetCountdown(int seconds) {
  char line[32];
  drawHeader("WIFI RESET");
  oled.drawStr(0, 30, "Keep holding FLASH");
  oled.setFont(u8g2_font_7x14B_tf);
  snprintf(line, sizeof(line), "Reset in %d s", seconds);
  oled.drawStr(0, 47, line);
  oled.setFont(u8g2_font_6x13B_tf);
  oled.drawStr(0, 62, "Release to cancel");
}

void Display::drawSong(const Player &player, const Gt2560Link &gt, const Queue &queue) {
  char line[32], elapsed[8], total[8];

  // Title with a play or pause symbol.
  oled.setFont(u8g2_font_7x14B_tf);
  if (player.paused()) {
    oled.drawBox(0, 2, 3, 10);
    oled.drawBox(5, 2, 3, 10);
  } else {
    oled.drawTriangle(0, 1, 0, 12, 8, 6);
  }
  strlcpy(line, player.songName(), 16);
  oled.drawStr(12, 12, line);
  oled.drawHLine(0, 15, 128);
  oled.setFont(u8g2_font_6x13B_tf);

  uint32_t songMs = gt.gtSongMs();
  uint32_t lengthMs = player.lengthMs() ? player.lengthMs() : 1;
  if (songMs > lengthMs) songMs = lengthMs;

  // Progress bar and time: kept inside tile rows 2-4 (y 16..39), the only part redrawn each second.
  oled.drawFrame(0, 18, 128, 8);
  oled.drawBox(2, 20, (uint32_t)PROGRESS_BAR_WIDTH * songMs / lengthMs, 4);
  formatTime(elapsed, sizeof(elapsed), songMs);
  formatTime(total, sizeof(total), player.lengthMs());
  snprintf(line, sizeof(line), "%s / %s", elapsed, total);
  oled.drawStr(0, 37, line);
  if (player.paused()) oled.drawStr(92, 37, "PAUSED");

  if (player.sound().settings().mode == 1)  // push mode: the level is the loudness
    snprintf(line, sizeof(line), "%s  Lvl %u", player.sound().styleName(), player.level());
  else
    snprintf(line, sizeof(line), "Style: %s", player.sound().styleName());
  oled.drawStr(0, 50, line);

  if (!gt.connected()) {
    oled.drawStr(0, 62, "GT2560 NOT CONNECTED");
  } else if (queue.active() && queue.size() > 1) {
    snprintf(line, sizeof(line), "%u/%u %s%s", (unsigned)queue.position(), (unsigned)queue.size(),
             queue.shuffled() ? "Shuffle" : "", queue.repeat() == Queue::Repeat::ALL ? " Rpt" : "");
    oled.drawStr(0, 62, line);
  } else {
    oled.drawStr(0, 62, queue.repeat() == Queue::Repeat::ONE ? "GT2560 OK  Repeat 1" : "GT2560 OK");
  }
}

// Between songs (rest time): what comes next and when.
void Display::drawUpNext(const Queue &queue) {
  char line[32];
  drawHeader("UP NEXT");
  oled.setFont(u8g2_font_7x14B_tf);
  strlcpy(line, queue.upcoming().c_str(), 19);
  oled.drawStr(0, 34, line);
  oled.setFont(u8g2_font_6x13B_tf);
  snprintf(line, sizeof(line), "in %lu s", (unsigned long)((queue.restLeftMs() + 999) / 1000));
  oled.drawStr(0, 50, line);
  strlcpy(line, queue.source(), 22);
  oled.drawStr(0, 63, line);
}

void Display::drawTest(const Player &player) {
  char line[32];
  drawHeader("MOTOR TEST");
  int motor = constrain(player.testMotor(), 0, MOTOR_COUNT - 1);
  snprintf(line, sizeof(line), "Motor %d (%s)", motor, MOTOR_NAMES[motor]);
  oled.drawStr(0, 34, line);
  oled.drawStr(0, 50, "Released after test");
}

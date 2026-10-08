// OLED status screens (SSD1306 128x64 over software SPI, U8g2 full-buffer mode).
//
// SPI displays cannot be read back, so there is no "found" check: begin() only initialises it.
// Software SPI is slow (a full screen takes ~113 ms of main-loop time), so update() redraws only
// when what is shown would change, and during a song refreshes just the progress rows each second.
#pragma once
#include <Arduino.h>
#include "../gt2560/Gt2560Link.h"
#include "../music/Player.h"
#include "../wifi/WifiManager.h"
#include "../music/Queue.h"

class Display {
public:
  void begin();
  void showBoot();
  void update(const Player &player, const Gt2560Link &gt, const WifiManager &wifi, const Queue &queue);  // call every loop

private:
  enum class Screen : uint8_t { IDLE, SONG, TEST, RESET_COUNTDOWN, WIFI_SETUP, UP_NEXT };

  static Screen chooseScreen(int resetCountdown, const Player &player, const WifiManager &wifi, const Queue &queue);
  static uint32_t layoutSignature(Screen screen, int resetCountdown, const Player &player, const Gt2560Link &gt,
                                  const WifiManager &wifi, const Queue &queue);
  static uint32_t progressSignature(const Player &player, const Gt2560Link &gt);
  void draw(Screen screen, int resetCountdown, const Player &player, const Gt2560Link &gt, const WifiManager &wifi,
            const Queue &queue);
  void reportDrawTime(bool fullRedraw, uint32_t us);

  void drawIdle(const Player &player, const Gt2560Link &gt, const WifiManager &wifi);
  void drawSong(const Player &player, const Gt2560Link &gt, const Queue &queue);
  void drawUpNext(const Queue &queue);
  void drawTest(const Player &player);
  void drawSetup(const WifiManager &wifi);
  void drawResetCountdown(int seconds);
  void drawHeader(const char *title);

  uint32_t lastDrawMs_ = 0;
  uint32_t layoutSignature_ = 0;    // hash of everything on screen except the song progress
  uint32_t progressSignature_ = 0;  // hash of the song time (seconds) and progress bar width
  bool reportedFullDraw_ = false;
  bool reportedPartialDraw_ = false;
};

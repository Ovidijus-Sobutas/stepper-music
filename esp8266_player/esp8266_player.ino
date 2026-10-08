/**
 * Wi-Fi stepper music player: NodeMCU ESP8266 side.
 *
 * Keeps the songs and playlists in flash (LittleFS), serves the web page, shows status on the
 * OLED and streams each song's notes over a framed serial link (D5 = RX, D6 = TX, via the level
 * shifter) to the GT2560, which plays them on five stepper motors. Wi-Fi is set up from a phone
 * through the MusicPlayer-XXXX hotspot; hold FLASH for 10 s to reset it.
 * USB (Serial, 115200) is the debug log and a console ("help"). See docs/PLAN.md.
 *
 * Everything runs from loop(): every module has a poll() that never blocks, because the link
 * to the GT2560 and the note stream need the loop every few ms.
 */
#include "src/config.h"
#include "src/gt2560/Gt2560Link.h"
#include "src/music/Player.h"
#include "src/music/SongLibrary.h"
#include "src/music/Sound.h"
#include "src/music/Queue.h"
#include "src/system/Console.h"
#include "src/display/Display.h"
#include "src/wifi/WifiManager.h"
#include "src/web/WebPortal.h"

Gt2560Link gt2560;
SongLibrary library;
Sound sound;
Player player(gt2560, library, sound);
Queue queue(player, library);
WifiManager wifi;
WebPortal webPortal(wifi, gt2560, player, library, sound, queue);
Console console(gt2560, player, wifi, queue);
Display display;

static bool startupReportPending = true;
static uint32_t bootMs = 0;

static const uint32_t LOOP_REPORT_INTERVAL_MS = 5000;
static const uint32_t LOOP_PAUSE_WORTH_REPORTING_MS = 50;

static void printStartupReport() {
  Serial.println();
  Serial.println(F("Music Player"));
  Serial.println(F("ESP8266"));
  Serial.printf("Firmware: %s\n\n", FW_VERSION);
  Serial.println(F("OLED: INITIALISED (SPI, cannot be detected; check the screen)"));
  Serial.printf("Songs: %u in flash (%lu of %lu KB used)\n", (unsigned)library.count(),
                (unsigned long)(library.usedBytes() / 1024), (unsigned long)(library.totalBytes() / 1024));
  if (wifi.state() == WifiState::NOT_CONFIGURED) Serial.printf("WiFi: NOT CONFIGURED (setup hotspot %s)\n", wifi.apName());
  else Serial.printf("WiFi: CONFIGURED (%s, %s)\n", wifi.ssid(), wifi.stateName());
  if (gt2560.connected()) Serial.printf("GT2560: CONNECTED (firmware %s)\n", gt2560.gtVersion());
  else Serial.println(F("GT2560: NOT CONNECTED"));
  Serial.println(F("\nType 'help' for commands, 'songs' for the song list."));
}

void setup() {
  Serial.begin(USB_BAUD);
  delay(100);
  Serial.println(F("\n\n=== ESP8266 music player booting ==="));
  Serial.printf("Reset reason: %s\n", ESP.getResetReason().c_str());
  Serial.printf("Reset info: %s\n", ESP.getResetInfo().c_str());
  display.begin();
  display.showBoot();
  gt2560.begin();
  gt2560.send("VER");
  library.begin();  // mounts LittleFS; first start: copies the demo songs into flash
  sound.begin();    // saved sound style (sent to the GT2560 once it answers)
  queue.begin();    // repeat / rest settings
  wifi.begin();
  webPortal.begin();
  bootMs = millis();
}

// Reports the longest pause between two passes of the main loop, when it is long enough to matter.
static void reportLongLoopPauses() {
  static uint32_t lastPassMs = 0, longestPauseMs = 0, lastReportMs = 0;
  uint32_t now = millis();
  if (lastPassMs && now - lastPassMs > longestPauseMs) longestPauseMs = now - lastPassMs;
  lastPassMs = now;
  if (now - lastReportMs >= LOOP_REPORT_INTERVAL_MS) {
    if (longestPauseMs >= LOOP_PAUSE_WORTH_REPORTING_MS)
      Serial.printf("[LOOP] longest pause in the last 5 s: %lu ms (heap %u)\n", (unsigned long)longestPauseMs,
                    ESP.getFreeHeap());
    longestPauseMs = 0;
    lastReportMs = now;
  }
}

void loop() {
  reportLongLoopPauses();
  gt2560.poll();
  player.poll();
  queue.poll();
  wifi.poll();
  webPortal.poll();
  console.poll();
  // The boot screen stays until the startup report (GT2560 answered, or STARTUP_REPORT_WAIT_MS).
  if (!startupReportPending) display.update(player, gt2560, wifi, queue);

  if (startupReportPending && (gt2560.gtVersion()[0] || millis() - bootMs > STARTUP_REPORT_WAIT_MS)) {
    startupReportPending = false;
    printStartupReport();
  }
}

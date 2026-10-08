// Line-based test console on the USB serial port (USB_BAUD): type "help" for the commands.
// Commands go through the same Queue / Player / WifiManager calls as the web page; "cfg", "ver",
// "stat", "ping", "corrupt", "hb" and "raw" talk to the GT2560 link directly.
#pragma once
#include <Arduino.h>
#include "../gt2560/Gt2560Link.h"
#include "../music/Player.h"
#include "../wifi/WifiManager.h"
#include "../music/Queue.h"

class Console {
public:
  Console(Gt2560Link &gt, Player &player, WifiManager &wifi, Queue &queue)
      : gt_(gt), player_(player), wifi_(wifi), queue_(queue) {}
  void poll();  // call every loop

private:
  void readLine();
  void restartWhenDue();
  void run(char *line);
  bool runMusicCommand(const char *command, char *arg);
  bool runSystemCommand(const char *command, char *arg);
  bool runLinkCommand(const char *command, char *arg);
  void runPlaylistCommand(const char *arg);
  void runTestCommand(char *arg);
  void runCfgCommand(const char *arg);
  void runWifiCommand(const char *arg);
  void printHelp();

  Gt2560Link &gt_;
  Player &player_;
  WifiManager &wifi_;
  Queue &queue_;
  char lineBuffer_[96];
  uint8_t lineLength_ = 0;
  uint32_t restartAtMs_ = 0;  // 0 = no restart planned
  bool factoryResetPending_ = false;
};

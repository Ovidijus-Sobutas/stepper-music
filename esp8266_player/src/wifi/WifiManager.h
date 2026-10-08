// Owns the Wi-Fi credentials and connection. Nothing else touches them.
//  - Up to MAX_NETWORKS saved networks (newest first) in /config/wifi.txt. With several, a scan
//    picks the strongest one in range; if it cannot connect, the next one is tried.
//  - No saved network, or not connected for a while: opens the setup hotspot MusicPlayer-XXXX at
//    192.168.4.1 with a DNS server that answers every name (the web portal serves the setup page).
//  - Hold the FLASH button for WIFI_RESET_HOLD_MS: all networks are erased and the hotspot opens.
// Everything that changes the radio runs in poll() (main loop), never in a web handler.
#pragma once
#include <Arduino.h>

enum class WifiState : uint8_t {
  NOT_CONFIGURED,  // no saved network: setup hotspot only
  CONNECTING,
  CONNECTED,
  DISCONNECTED,    // was connected, lost it; retrying
};

class WifiManager {
public:
  void begin();
  void poll();  // call every loop

  static constexpr uint8_t MAX_NETWORKS = 5;

  WifiState state() const { return state_; }
  const char *stateName() const;
  bool apActive() const { return apActive_; }
  const char *apName() const { return apName_; }
  // The network in use / being tried.
  const char *ssid() const { return currentNetwork_ >= 0 ? networks_[currentNetwork_].ssid : ""; }
  String ip() const;
  int rssi() const;
  int resetCountdown() const;  // seconds left while FLASH is held, -1 otherwise

  uint8_t networkCount() const { return networkCount_; }
  const char *networkName(uint8_t i) const { return i < networkCount_ ? networks_[i].ssid : ""; }

  bool saveCredentials(const char *ssid, const char *password);  // adds (or updates) and connects to it
  bool removeNetwork(const char *ssid);                          // false if not saved
  void forget();                                                 // erase all, open the hotspot

private:
  struct Network {
    char ssid[33];
    char password[65];
  };

  bool load();
  bool store();
  int8_t findNetwork(const char *ssid) const;
  void connectTo(int8_t index);
  void connectBest();  // one network: connect; several: scan first, then pick
  void pickFromScan(int found);
  void startAp();
  void stopAp();

  void pollResetButton(uint32_t now);
  void pollScan(uint32_t now);
  void pollConnecting(uint32_t now, bool connected);
  void pollConnected(uint32_t now, bool connected);
  void onConnected(uint32_t now);

  Network networks_[MAX_NETWORKS];
  uint8_t networkCount_ = 0;
  int8_t currentNetwork_ = -1;
  uint8_t failedAttempts_ = 0;  // failed attempts in a row: rotates through the candidates
  bool scanning_ = false;
  uint32_t scanStartMs_ = 0;

  WifiState state_ = WifiState::NOT_CONFIGURED;
  char apName_[24] = "";
  bool apActive_ = false;
  uint32_t stateSinceMs_ = 0;
  uint32_t apStopAtMs_ = 0;  // 0 = keep the hotspot
  bool mdnsStarted_ = false;

  bool buttonHeld_ = false;
  bool resetDone_ = false;  // this press already reset: wait for the release
  uint32_t buttonSinceMs_ = 0;
};

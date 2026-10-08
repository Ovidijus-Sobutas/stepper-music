#include "WifiManager.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include "../config.h"

static DNSServer dnsServer;  // answers every name with 192.168.4.1 so phones open the setup page
static const IPAddress AP_IP(192, 168, 4, 1);
static const uint32_t SCAN_TIMEOUT_MS = 15000;
static const uint32_t RESET_COUNTDOWN_AFTER_MS = 1000;  // a short press of FLASH shows nothing

void WifiManager::begin() {
  pinMode(WIFI_RESET_BUTTON_PIN, INPUT_PULLUP);
  snprintf(apName_, sizeof(apName_), WIFI_AP_PREFIX "%04X", (unsigned)(ESP.getChipId() & 0xFFFF));

  WiFi.persistent(false);  // credentials live in our own file, not in the SDK's flash area
  WiFi.setAutoReconnect(true);
  WiFi.forceSleepWake();
  delay(1);
  // The name the router shows for this device (instead of "ESP-XXXXXX"); many home routers
  // then also answer http://<name>/ besides the mDNS name http://<name>.local.
  WiFi.hostname(MDNS_NAME);

  if (!LittleFS.begin()) Serial.println(F("[WIFI] file system not available"));
  if (load()) {
    Serial.printf("[WIFI] %u saved network(s):", networkCount_);
    for (uint8_t i = 0; i < networkCount_; i++) Serial.printf(" '%s'", networks_[i].ssid);
    Serial.println();
    connectBest();
  } else {
    Serial.println(F("[WIFI] no saved network"));
    state_ = WifiState::NOT_CONFIGURED;
    startAp();
  }
}

// ---------------------------------------------------------------- saved networks

// File: name and password on alternate lines, newest network first (a file from older firmware
// with one network reads the same way).
bool WifiManager::load() {
  networkCount_ = 0;
  File file = LittleFS.open(WIFI_CREDENTIALS_FILE, "r");
  if (!file) return false;
  while (file.available() && networkCount_ < MAX_NETWORKS) {
    String ssid = file.readStringUntil('\n');
    String password = file.readStringUntil('\n');
    ssid.trim();
    password.replace("\r", "");  // passwords may start or end with spaces: only strip CR
    if (!ssid.length() || ssid.length() > 32 || password.length() > 64) continue;
    strlcpy(networks_[networkCount_].ssid, ssid.c_str(), sizeof(networks_[networkCount_].ssid));
    strlcpy(networks_[networkCount_].password, password.c_str(), sizeof(networks_[networkCount_].password));
    networkCount_++;
  }
  file.close();
  return networkCount_ > 0;
}

bool WifiManager::store() {
  if (!networkCount_) return LittleFS.remove(WIFI_CREDENTIALS_FILE) || !LittleFS.exists(WIFI_CREDENTIALS_FILE);
  LittleFS.mkdir("/config");
  File file = LittleFS.open(WIFI_CREDENTIALS_FILE, "w");
  if (!file) return false;
  for (uint8_t i = 0; i < networkCount_; i++) file.printf("%s\n%s\n", networks_[i].ssid, networks_[i].password);
  file.close();
  return true;
}

// Index of a saved network, or -1. (The list never holds the same name twice.)
int8_t WifiManager::findNetwork(const char *ssid) const {
  for (int8_t i = networkCount_ - 1; i >= 0; i--)
    if (!strcmp(networks_[i].ssid, ssid)) return i;
  return -1;
}

bool WifiManager::saveCredentials(const char *ssid, const char *password) {
  if (!ssid || !*ssid || strlen(ssid) > 32 || strlen(password) > 64) return false;
  // Move it to the front (newest first); the oldest drops out when the list is full.
  int8_t found = findNetwork(ssid);
  uint8_t last = found >= 0 ? found : (networkCount_ < MAX_NETWORKS ? networkCount_++ : MAX_NETWORKS - 1);
  for (int i = last; i > 0; i--) networks_[i] = networks_[i - 1];
  strlcpy(networks_[0].ssid, ssid, sizeof(networks_[0].ssid));
  strlcpy(networks_[0].password, password, sizeof(networks_[0].password));
  if (!store()) return false;
  Serial.printf("[WIFI] saved network %s (%u saved), connecting\n", ssid, networkCount_);
  failedAttempts_ = 0;
  connectTo(0);
  return true;
}

bool WifiManager::removeNetwork(const char *ssid) {
  int8_t found = findNetwork(ssid);
  if (found < 0) return false;
  if (networkCount_ == 1) {
    forget();
    return true;
  }
  for (uint8_t i = found; i + 1 < networkCount_; i++) networks_[i] = networks_[i + 1];
  networkCount_--;
  store();
  Serial.printf("[WIFI] removed network %s (%u left)\n", ssid, networkCount_);
  if (found == currentNetwork_) {  // it was in use: switch to another one
    WiFi.disconnect();
    failedAttempts_ = 0;
    connectBest();
  } else if (found < currentNetwork_) {
    currentNetwork_--;
  }
  return true;
}

void WifiManager::forget() {
  networkCount_ = 0;
  currentNetwork_ = -1;
  scanning_ = false;
  store();
  WiFi.disconnect();
  if (mdnsStarted_) {
    MDNS.end();
    mdnsStarted_ = false;
  }
  state_ = WifiState::NOT_CONFIGURED;
  stateSinceMs_ = millis();
  Serial.println(F("[WIFI] all saved networks erased"));
  startAp();
}

// ---------------------------------------------------------------- connecting

void WifiManager::connectTo(int8_t index) {
  scanning_ = false;
  currentNetwork_ = index;
  WiFi.mode(apActive_ ? WIFI_AP_STA : WIFI_STA);
  WiFi.begin(networks_[index].ssid, networks_[index].password);
  state_ = WifiState::CONNECTING;
  stateSinceMs_ = millis();
  Serial.printf("[WIFI] connecting to %s\n", networks_[index].ssid);
}

void WifiManager::connectBest() {
  if (!networkCount_) return;
  if (networkCount_ == 1) {
    connectTo(0);
    return;
  }
  // Several networks: see which are in range first. The scan runs in the background (pollScan).
  WiFi.mode(apActive_ ? WIFI_AP_STA : WIFI_STA);
  WiFi.disconnect();
  if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) WiFi.scanNetworks(true);
  scanning_ = true;
  scanStartMs_ = millis();
  state_ = WifiState::CONNECTING;
  stateSinceMs_ = millis();
  Serial.println(F("[WIFI] scanning for the saved networks"));
}

// Candidates: saved networks in range, strongest first (none in range: all of them, newest
// first; a hidden network is not seen by the scan). failedAttempts_ rotates through them.
void WifiManager::pickFromScan(int found) {
  int8_t candidates[MAX_NETWORKS];
  int candidateRssi[MAX_NETWORKS];
  uint8_t candidateCount = 0;
  for (uint8_t k = 0; k < networkCount_; k++) {
    int rssi = -1000;
    for (int i = 0; i < found; i++)
      if (WiFi.SSID(i) == networks_[k].ssid && WiFi.RSSI(i) > rssi) rssi = WiFi.RSSI(i);
    if (rssi == -1000) continue;  // not in range
    uint8_t at = candidateCount++;
    while (at > 0 && candidateRssi[at - 1] < rssi) {  // insertion sort, strongest first
      candidates[at] = candidates[at - 1];
      candidateRssi[at] = candidateRssi[at - 1];
      at--;
    }
    candidates[at] = k;
    candidateRssi[at] = rssi;
  }
  if (found > 0) WiFi.scanDelete();
  if (candidateCount) {
    Serial.printf("[WIFI] %u saved network(s) in range, best %s (%d dBm)\n", candidateCount,
                  networks_[candidates[0]].ssid, candidateRssi[0]);
    connectTo(candidates[failedAttempts_ % candidateCount]);
  } else {
    Serial.println(F("[WIFI] no saved network in range, trying them in turn"));
    connectTo(failedAttempts_ % networkCount_);
  }
}

// ---------------------------------------------------------------- setup hotspot

void WifiManager::startAp() {
  if (apActive_) return;
  WiFi.mode(networkCount_ ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apName_, WIFI_AP_PASSWORD);
  dnsServer.start(53, "*", AP_IP);
  apActive_ = true;
  apStopAtMs_ = 0;
  Serial.printf("[WIFI] setup hotspot '%s' (password %s), page at http://192.168.4.1\n", apName_, WIFI_AP_PASSWORD);
}

void WifiManager::stopAp() {
  if (!apActive_) return;
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive_ = false;
  Serial.println(F("[WIFI] setup hotspot closed"));
}

// ---------------------------------------------------------------- main loop

void WifiManager::poll() {
  uint32_t now = millis();
  pollResetButton(now);
  if (apActive_) dnsServer.processNextRequest();
  if (mdnsStarted_) MDNS.update();

  bool connected = WiFi.status() == WL_CONNECTED;
  if (scanning_) {  // several networks: waiting for the scan before connecting
    pollScan(now);
    return;
  }
  switch (state_) {
    case WifiState::CONNECTING:
    case WifiState::DISCONNECTED: pollConnecting(now, connected); break;
    case WifiState::CONNECTED: pollConnected(now, connected); break;
    default: break;
  }
}

// FLASH button held for WIFI_RESET_HOLD_MS -> erase all saved networks (once per press).
void WifiManager::pollResetButton(uint32_t now) {
  bool pressed = digitalRead(WIFI_RESET_BUTTON_PIN) == LOW;
  if (pressed && !buttonHeld_) {
    buttonHeld_ = true;
    buttonSinceMs_ = now;
  } else if (!pressed) {
    buttonHeld_ = false;
    resetDone_ = false;
  }
  if (buttonHeld_ && !resetDone_ && now - buttonSinceMs_ >= WIFI_RESET_HOLD_MS) {
    resetDone_ = true;
    Serial.println(F("[WIFI] FLASH held 10 s: resetting Wi-Fi"));
    forget();
  }
}

void WifiManager::pollScan(uint32_t now) {
  int found = WiFi.scanComplete();
  bool timedOut = now - scanStartMs_ >= SCAN_TIMEOUT_MS;
  // FAILED also means "no results": a /scan request from the page took them. Scan again.
  if (found == WIFI_SCAN_FAILED && !timedOut) WiFi.scanNetworks(true);
  else if (found >= 0 || timedOut) pickFromScan(found > 0 ? found : 0);
}

// Waiting for a connection (first one, or after it was lost).
void WifiManager::pollConnecting(uint32_t now, bool connected) {
  if (connected) {
    onConnected(now);
    return;
  }
  uint32_t limit = state_ == WifiState::CONNECTING ? WIFI_CONNECT_TIMEOUT_MS : WIFI_LOST_TIMEOUT_MS;
  if (now - stateSinceMs_ <= limit) return;
  if (!apActive_) {
    Serial.printf("[WIFI] cannot reach %s, opening the setup hotspot too (still retrying)\n", ssid());
    startAp();
  }
  if (networkCount_ > 1) {  // try the next saved network
    failedAttempts_++;
    connectBest();
  }
  // One network: the SDK keeps retrying it (auto reconnect).
}

void WifiManager::onConnected(uint32_t now) {
  state_ = WifiState::CONNECTED;
  stateSinceMs_ = now;
  failedAttempts_ = 0;
  Serial.printf("[WIFI] connected to %s, IP %s, signal %d dBm\n", ssid(), WiFi.localIP().toString().c_str(),
                WiFi.RSSI());
  if (!mdnsStarted_ && MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    mdnsStarted_ = true;
    Serial.println(F("[WIFI] also reachable as http://" MDNS_NAME ".local"));
  }
  if (apActive_) apStopAtMs_ = now + WIFI_AP_LINGER_MS;  // the phone on the hotspot sees the result first
}

void WifiManager::pollConnected(uint32_t now, bool connected) {
  if (!connected) {
    state_ = WifiState::DISCONNECTED;
    stateSinceMs_ = now;
    Serial.println(F("[WIFI] connection lost, retrying"));
  } else if (apActive_ && apStopAtMs_ && (int32_t)(now - apStopAtMs_) >= 0) {
    stopAp();
  }
}

// ---------------------------------------------------------------- status

String WifiManager::ip() const {
  if (state_ == WifiState::CONNECTED) return WiFi.localIP().toString();
  if (apActive_) return AP_IP.toString();
  return String();
}

int WifiManager::rssi() const { return state_ == WifiState::CONNECTED ? WiFi.RSSI() : 0; }

int WifiManager::resetCountdown() const {
  if (!buttonHeld_ || resetDone_) return -1;
  uint32_t held = millis() - buttonSinceMs_;
  if (held < RESET_COUNTDOWN_AFTER_MS) return -1;
  return (int)((WIFI_RESET_HOLD_MS - held + 999) / 1000);
}

const char *WifiManager::stateName() const {
  switch (state_) {
    case WifiState::CONNECTING: return "CONNECTING";
    case WifiState::CONNECTED: return "CONNECTED";
    case WifiState::DISCONNECTED: return "DISCONNECTED";
    default: return "NOT CONFIGURED";
  }
}

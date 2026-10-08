#include "WebPortal.h"
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include "../config.h"
#include "WebAssets.h"

static AsyncWebServer server(80);

static const uint32_t WIFI_CHANGE_DELAY_MS = 300;  // lets the answer leave before the radio changes
static const uint32_t RESTART_DELAY_MS = 1000;     // lets the answer and the STOP to the GT2560 go out
static const char SONG_IS_PLAYING_MESSAGE[] = "That song is playing. Stop it first.";

static String jsonEscape(const String &text) {
  String out;
  out.reserve(text.length() + 4);
  for (char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if ((uint8_t)c < 0x20) {
      char escaped[8];
      snprintf(escaped, sizeof(escaped), "\\u%04x", c);
      out += escaped;
    } else {
      out += c;
    }
  }
  return out;
}

static String jsonBool(bool value) { return value ? "true" : "false"; }

// 200 with an empty or informative text, or 409 with the error text shown by the page.
static void reply(AsyncWebServerRequest *request, bool ok, const String &message) {
  request->send(ok ? 200 : 409, "text/plain", message);
}

// "a\nb\n" -> {"a", "b"}, each line trimmed, empty lines dropped.
static std::vector<String> splitLines(const String &text) {
  std::vector<String> lines;
  int start = 0;
  while (start < (int)text.length()) {
    int end = text.indexOf('\n', start);
    if (end < 0) end = text.length();
    String line = text.substring(start, end);
    line.trim();
    if (line.length()) lines.push_back(line);
    start = end + 1;
  }
  return lines;
}

// "0".."4" -> motor index, "all" -> -1, anything else -> -2.
static int parseMotorArg(const String &arg) {
  if (arg == "all") return -1;
  if (arg.length() == 1 && arg[0] >= '0' && arg[0] <= '4') return arg[0] - '0';
  return -2;
}

static Queue::Repeat parseRepeat(const String &value) {
  return value == "all" ? Queue::Repeat::ALL : value == "one" ? Queue::Repeat::ONE : Queue::Repeat::OFF;
}

// ---------------------------------------------------------------- routes

void WebPortal::begin() {
  registerPageRoutes();
  registerStatusRoutes();
  registerWifiRoutes();
  registerSystemRoutes();
  registerPlaybackRoutes();
  registerQueueRoutes();
  registerSoundRoutes();
  registerSongRoutes();
  server.onNotFound([this](AsyncWebServerRequest *r) { handleNotFound(r); });
  server.begin();
}

// The page files (web/ folder, gzipped into the firmware).
void WebPortal::registerPageRoutes() {
  for (size_t i = 0; i < WEB_ASSET_COUNT; i++) {
    const WebAsset *asset = &WEB_ASSETS[i];
    server.on(asset->path, HTTP_GET, [this, asset](AsyncWebServerRequest *r) { sendAsset(r, *asset); });
  }
}

void WebPortal::registerStatusRoutes() {
  server.on("/status", HTTP_GET, [this](AsyncWebServerRequest *r) { handleStatus(r); });
  server.on("/api/songs", HTTP_GET, [this](AsyncWebServerRequest *r) { handleSongs(r); });
  server.on("/api/sound", HTTP_GET, [this](AsyncWebServerRequest *r) { handleSound(r); });
  server.on("/scan", HTTP_GET, [this](AsyncWebServerRequest *r) { handleScan(r); });
  server.on("/api/info", HTTP_GET, [this](AsyncWebServerRequest *r) { handleInfo(r); });
  server.on("/api/playlists", HTTP_GET, [this](AsyncWebServerRequest *r) { handlePlaylists(r); });
}

// Answered at once, carried out in the main loop (switching networks waits and drops the answer).
void WebPortal::registerWifiRoutes() {
  server.on("/save", HTTP_POST, [this](AsyncWebServerRequest *r) {
    String ssid = r->arg("ssid"), password = r->arg("pass");
    ssid.trim();
    if (!ssid.length() || ssid.length() > 32 || password.length() > 64) {
      r->send(400, "text/plain", "Network name must be 1-32 characters, password at most 64.");
      return;
    }
    newSsid_ = ssid;
    newPassword_ = password;
    saveWifiPending_ = true;
    wifiChangeAtMs_ = millis() + WIFI_CHANGE_DELAY_MS;
    r->send(200, "text/plain", "Saved. Connecting to " + ssid + "… watch this page or the display.");
  });
  server.on("/forget", HTTP_POST, [this](AsyncWebServerRequest *r) {
    forgetWifiPending_ = true;
    wifiChangeAtMs_ = millis() + WIFI_CHANGE_DELAY_MS;
    r->send(200, "text/plain", "All networks forgotten. Join the hotspot " + String(wifi_.apName()) + " to set up again.");
  });
  server.on("/api/wifi-remove", HTTP_POST, [this](AsyncWebServerRequest *r) {
    ssidToRemove_ = r->arg("ssid");
    bool known = false;
    for (uint8_t i = 0; i < wifi_.networkCount(); i++) known |= ssidToRemove_ == wifi_.networkName(i);
    if (!known) {
      ssidToRemove_ = "";
      reply(r, false, "No such saved network");
      return;
    }
    wifiChangeAtMs_ = millis() + WIFI_CHANGE_DELAY_MS;
    reply(r, true, "Removed " + ssidToRemove_);
  });
}

void WebPortal::registerSystemRoutes() {
  server.on("/api/restart", HTTP_POST, [this](AsyncWebServerRequest *r) {
    queue_.stop();
    restartAtMs_ = millis() + RESTART_DELAY_MS;
    reply(r, true, "Restarting… the page reconnects by itself.");
  });
  server.on("/api/factory-reset", HTTP_POST, [this](AsyncWebServerRequest *r) {
    if (r->arg("confirm") != "RESET") {
      reply(r, false, "Not confirmed");
      return;
    }
    queue_.stop();
    factoryResetPending_ = true;
    restartAtMs_ = millis() + RESTART_DELAY_MS;
    reply(r, true, String("Erasing everything and restarting. Join the hotspot ") + wifi_.apName() + " to set up again.");
  });
}

void WebPortal::registerPlaybackRoutes() {
  server.on("/api/play", HTTP_POST, [this](AsyncWebServerRequest *r) {
    bool ok = queue_.playSong(r->arg("song").c_str());  // switches songs if one is already playing
    reply(r, ok, ok ? "" : "Unknown song");
  });
  server.on("/api/pause", HTTP_POST, [this](AsyncWebServerRequest *r) { player_.pause(); reply(r, true, ""); });
  server.on("/api/resume", HTTP_POST, [this](AsyncWebServerRequest *r) { player_.resume(); reply(r, true, ""); });
  server.on("/api/stop", HTTP_POST, [this](AsyncWebServerRequest *r) { queue_.stop(); reply(r, true, ""); });
  server.on("/api/next", HTTP_POST, [this](AsyncWebServerRequest *r) { queue_.next(); reply(r, true, ""); });
  server.on("/api/prev", HTTP_POST, [this](AsyncWebServerRequest *r) { queue_.prev(); reply(r, true, ""); });
  server.on("/api/level", HTTP_POST, [this](AsyncWebServerRequest *r) {
    int level = r->arg("value").toInt();
    if (level < 1 || level > MAX_LEVEL) {
      reply(r, false, "Level must be 1-" + String(MAX_LEVEL));
      return;
    }
    player_.setLevel((uint8_t)level);
    reply(r, true, "");
  });
  server.on("/api/test", HTTP_POST, [this](AsyncWebServerRequest *r) {
    int motor = parseMotorArg(r->arg("motor"));
    if (motor == -2) {
      reply(r, false, "Motor must be 0-4 or all");
      return;
    }
    player_.startMotorTest(motor, 60, 1000);  // middle C for 1 s; stops a playing song first
    reply(r, true, "");
  });
}

void WebPortal::registerQueueRoutes() {
  server.on("/api/queue", HTTP_POST, [this](AsyncWebServerRequest *r) {
    bool shuffle = r->arg("shuffle") == "1";
    String error;
    bool ok;
    if (r->arg("source") == "playlist") {
      ok = queue_.playPlaylist(r->arg("name").c_str(), shuffle, error);
    } else {
      ok = queue_.playAll(shuffle);
      if (!ok) error = "No songs";
    }
    reply(r, ok, ok ? "" : error);
  });
  server.on("/api/queue-settings", HTTP_POST, [this](AsyncWebServerRequest *r) {
    if (r->hasArg("repeat")) queue_.setRepeat(parseRepeat(r->arg("repeat")));
    if (r->hasArg("rest")) queue_.setRestSeconds((uint8_t)constrain(r->arg("rest").toInt(), 0L, 60L));
    reply(r, true, "");
  });
  server.on("/api/playlist", HTTP_POST, [this](AsyncWebServerRequest *r) {
    std::vector<String> songs = splitLines(r->arg("songs"));
    String name = r->arg("name"), error;
    name.trim();
    bool ok = queue_.savePlaylist(name.c_str(), songs, error);
    reply(r, ok, ok ? "" : error);
  });
  server.on("/api/playlist-delete", HTTP_POST, [this](AsyncWebServerRequest *r) {
    String error;
    bool ok = queue_.deletePlaylist(r->arg("name").c_str(), error);
    reply(r, ok, ok ? "" : error);
  });
}

void WebPortal::registerSoundRoutes() {
  server.on("/api/sound", HTTP_POST, [this](AsyncWebServerRequest *r) {
    String error;
    if (r->hasArg("style")) {
      bool ok = sound_.applyStyle(r->arg("style").c_str());
      reply(r, ok, ok ? "" : "Unknown style");
    } else if (r->hasArg("key")) {
      bool ok = sound_.set(r->arg("key").c_str(), r->arg("value").toInt(), error);
      reply(r, ok, ok ? "" : error);
    } else {
      reply(r, false, "style= or key=&value= expected");
    }
  });
}

void WebPortal::registerSongRoutes() {
  server.on(
      "/api/upload", HTTP_POST, [this](AsyncWebServerRequest *r) { handleUploadDone(r); },
      [this](AsyncWebServerRequest *r, const String &fileName, size_t index, uint8_t *data, size_t length,
             bool final) { handleUploadChunk(r, fileName, index, data, length, final); });
  server.on("/api/delete", HTTP_POST, [this](AsyncWebServerRequest *r) {
    String song = r->arg("song"), error;
    if (player_.isCurrent(song.c_str())) {
      reply(r, false, SONG_IS_PLAYING_MESSAGE);
      return;
    }
    bool ok = library_.remove(song.c_str(), error);
    reply(r, ok, ok ? "" : error);
  });
  server.on("/api/rename", HTTP_POST, [this](AsyncWebServerRequest *r) {
    String song = r->arg("song"), newName = r->arg("to"), error;
    newName.trim();
    if (player_.isCurrent(song.c_str())) {
      reply(r, false, SONG_IS_PLAYING_MESSAGE);
      return;
    }
    bool ok = library_.rename(song.c_str(), newName.c_str(), error);
    if (ok) queue_.renameSongInPlaylists(song.c_str(), newName.c_str());
    reply(r, ok, ok ? "" : error);
  });
  server.on("/api/demos", HTTP_POST, [this](AsyncWebServerRequest *r) {
    if (player_.playingSong()) {
      reply(r, false, "Stop the music first.");
      return;
    }
    if (restoreDemosPending_) {
      reply(r, false, "Already restoring");
      return;
    }
    // Writing ~120 KB takes a moment, so it happens in the main loop and the answer goes out now.
    // (The async server answers 501 by itself if a handler returns without replying.)
    restoreDemosPending_ = true;
    reply(r, true, "Restoring the demo songs…");
  });
}

// ---------------------------------------------------------------- deferred work (main loop)

void WebPortal::poll() {
  startRequestedScan();
  applyPendingWifiChange();
  restartWhenDue();
  restoreDemosIfRequested();
}

void WebPortal::startRequestedScan() {
  if (!scanRequested_) return;
  scanRequested_ = false;
  if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) WiFi.scanNetworks(true);  // async: results picked up by /scan
}

void WebPortal::applyPendingWifiChange() {
  if (!saveWifiPending_ && !forgetWifiPending_ && !ssidToRemove_.length()) return;
  if ((int32_t)(millis() - wifiChangeAtMs_) < 0) return;
  if (saveWifiPending_) wifi_.saveCredentials(newSsid_.c_str(), newPassword_.c_str());
  else if (forgetWifiPending_) wifi_.forget();
  else wifi_.removeNetwork(ssidToRemove_.c_str());
  saveWifiPending_ = forgetWifiPending_ = false;
  ssidToRemove_ = "";
}

void WebPortal::restartWhenDue() {
  if (!restartAtMs_ || (int32_t)(millis() - restartAtMs_) < 0) return;
  if (factoryResetPending_) {
    // Songs, playlists, Wi-Fi networks and settings. The demo songs come back at the next start.
    Serial.println(F("[WEB] factory reset: erasing the file system"));
    LittleFS.format();
  }
  Serial.println(F("[WEB] restarting"));
  Serial.flush();
  ESP.restart();
}

void WebPortal::restoreDemosIfRequested() {
  if (!restoreDemosPending_) return;
  restoreDemosPending_ = false;
  int restored = library_.installDemos(true);
  Serial.printf("[WEB] %d demo songs restored\n", restored);
}

// ---------------------------------------------------------------- GET handlers (JSON)

void WebPortal::sendAsset(AsyncWebServerRequest *request, const WebAsset &asset) {
  AsyncWebServerResponse *response = request->beginResponse(200, asset.type, asset.data, asset.size);
  response->addHeader("Content-Encoding", "gzip");
  response->addHeader("Cache-Control", "no-cache");  // check for a newer copy after a firmware update
  request->send(response);
}

// Polled by the page about once a second.
void WebPortal::handleStatus(AsyncWebServerRequest *request) {
  bool playing = player_.playingSong();
  const char *mode = playing ? "song" : player_.testing() ? "test" : "idle";
  String j;
  j.reserve(400);
  j = "{\"mode\":\"";
  j += mode;
  j += "\",\"song\":\"" + jsonEscape(playing ? player_.songName() : "");
  j += "\",\"paused\":" + jsonBool(player_.paused());
  j += ",\"t\":" + String(playing ? gt_.gtSongMs() : 0);
  j += ",\"len\":" + String(playing ? player_.lengthMs() : 0);
  j += ",\"level\":" + String(player_.level());
  j += ",\"style\":\"" + String(sound_.styleName()) + "\"";
  j += ",\"motor\":" + String(player_.testMotor());
  j += ",\"queue\":{\"active\":" + jsonBool(queue_.active());
  j += ",\"source\":\"" + jsonEscape(queue_.source()) + "\",\"pos\":" + String(queue_.position());
  j += ",\"size\":" + String(queue_.size()) + ",\"shuffle\":" + jsonBool(queue_.shuffled());
  j += ",\"repeat\":\"" + String(Queue::repeatName(queue_.repeat())) + "\",\"rest\":" + String(queue_.restSeconds());
  j += ",\"next\":\"" + jsonEscape(queue_.upcoming()) + "\",\"wait\":" + String(queue_.restLeftMs()) + "}";
  j += ",\"msg\":\"" + jsonEscape(player_.lastMessage());
  j += "\",\"wifi\":\"" + String(wifi_.stateName());
  j += "\",\"ssid\":\"" + jsonEscape(wifi_.ssid());
  j += "\",\"ip\":\"" + wifi_.ip();
  j += "\",\"rssi\":" + String(wifi_.rssi());
  j += ",\"ap\":\"" + String(wifi_.apActive() ? wifi_.apName() : "");
  j += "\",\"gt\":" + jsonBool(gt_.connected());
  j += ",\"gtver\":\"" + String(gt_.gtVersion()) + "\"";
  j += ",\"heap\":" + String(ESP.getFreeHeap());
  j += ",\"block\":" + String(ESP.getMaxFreeBlockSize());
  j += ",\"up\":" + String(millis() / 1000);
  j += "}";
  request->send(200, "application/json", j);
}

void WebPortal::handleSongs(AsyncWebServerRequest *request) {
  String j = "{\"songs\":[";
  for (size_t i = 0; i < library_.count(); i++) {
    const SongInfo &song = library_.at(i);
    if (i) j += ',';
    j += "{\"name\":\"" + jsonEscape(song.name) + "\",\"notes\":" + String(song.notes) +
         ",\"len\":" + String(song.lengthMs) + ",\"bytes\":" + String(song.bytes) + "}";
  }
  j += "],\"used\":" + String(library_.usedBytes()) + ",\"total\":" + String(library_.totalBytes()) + "}";
  request->send(200, "application/json", j);
}

void WebPortal::handleSound(AsyncWebServerRequest *request) {
  const SoundSettings &s = sound_.settings();
  String j;
  j.reserve(1400);
  j = "{\"style\":\"" + String(sound_.styleId()) + "\",\"s\":{";
  j += "\"mode\":" + String(s.mode) + ",\"group\":" + String(s.group) + ",\"oct\":" + String(s.octave);
  j += ",\"level\":" + String(s.level) + ",\"spacing\":" + String(s.stepSpacingUs) + ",\"ramp\":" + String(s.rampPeriods);
  j += ",\"maxfs\":" + String(s.maxFullSteps) + ",\"release\":" + String(s.releaseMs) + ",\"micro\":" + String(s.microsteps);
  j += "},\"styles\":[";
  size_t styleCount;
  const SoundStyle *styles = Sound::styles(styleCount);
  for (size_t i = 0; i < styleCount; i++) {
    if (i) j += ',';
    j += "{\"id\":\"" + String(styles[i].id) + "\",\"name\":\"" + jsonEscape(styles[i].name) + "\",\"desc\":\"" +
         jsonEscape(styles[i].description) + "\"}";
  }
  j += "]}";
  request->send(200, "application/json", j);
}

void WebPortal::handlePlaylists(AsyncWebServerRequest *request) {
  String j = "{\"playlists\":[";
  bool first = true;
  for (const Queue::Playlist &playlist : queue_.playlists()) {
    if (!first) j += ',';
    first = false;
    j += "{\"name\":\"" + jsonEscape(playlist.name) + "\",\"songs\":[";
    for (size_t i = 0; i < playlist.songs.size(); i++) {
      if (i) j += ',';
      j += "\"" + jsonEscape(playlist.songs[i]) + "\"";
    }
    j += "]}";
  }
  j += "]}";
  request->send(200, "application/json", j);
}

// Settings page: saved networks and system information.
void WebPortal::handleInfo(AsyncWebServerRequest *request) {
  String j = "{\"networks\":[";
  for (uint8_t i = 0; i < wifi_.networkCount(); i++) {
    if (i) j += ',';
    bool current = wifi_.state() == WifiState::CONNECTED && !strcmp(wifi_.networkName(i), wifi_.ssid());
    j += "{\"ssid\":\"" + jsonEscape(wifi_.networkName(i)) + "\",\"current\":" + jsonBool(current) + "}";
  }
  j += "],\"maxNetworks\":" + String(WifiManager::MAX_NETWORKS);
  j += ",\"fw\":\"" FW_VERSION "\",\"gt\":" + jsonBool(gt_.connected());
  j += ",\"gtver\":\"" + String(gt_.gtVersion()) + "\",\"up\":" + String(millis() / 1000);
  j += ",\"heap\":" + String(ESP.getFreeHeap()) + ",\"block\":" + String(ESP.getMaxFreeBlockSize());
  j += ",\"used\":" + String(library_.usedBytes()) + ",\"total\":" + String(library_.totalBytes());
  j += ",\"songs\":" + String(library_.count()) + ",\"playlists\":" + String(queue_.playlists().size());
  j += ",\"reset\":\"" + jsonEscape(ESP.getResetReason()) + "\",\"ap\":\"" + String(wifi_.apName());
  j += "\",\"host\":\"" MDNS_NAME ".local\",\"core\":\"" + jsonEscape(ESP.getCoreVersion()) + "\"}";
  request->send(200, "application/json", j);
}

// The scan runs in the background: the page asks again until it gets the list.
void WebPortal::handleScan(AsyncWebServerRequest *request) {
  int found = WiFi.scanComplete();
  if (found == WIFI_SCAN_RUNNING || found < 0) {
    if (found != WIFI_SCAN_RUNNING) scanRequested_ = true;  // no results yet: poll() starts a scan
    request->send(200, "application/json", "{\"scanning\":true}");
    return;
  }
  String j = "{\"networks\":[";
  int shown = 0;
  for (int i = 0; i < found; i++) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;
    bool duplicate = false;  // one entry per network name (mesh routers repeat it)
    for (int k = 0; k < i; k++)
      if (WiFi.SSID(k) == ssid) duplicate = true;
    if (duplicate) continue;
    if (shown++) j += ',';
    j += "{\"ssid\":\"" + jsonEscape(ssid) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
         ",\"open\":" + jsonBool(WiFi.encryptionType(i) == ENC_TYPE_NONE) + "}";
  }
  j += "]}";
  WiFi.scanDelete();  // the next request scans again
  request->send(200, "application/json", j);
}

// ---------------------------------------------------------------- upload

// Called for every chunk of an uploaded file. One upload at a time; the song only appears once
// the whole file has arrived and checked out (SongLibrary).
void WebPortal::handleUploadChunk(AsyncWebServerRequest *request, const String &fileName, size_t index,
                                  uint8_t *data, size_t length, bool final) {
  if (index == 0) {
    if (uploadOwner_ && uploadOwner_ != request) return;  // busy: handleUploadDone answers with an error
    uploadOwner_ = request;
    uploadOk_ = false;
    uploadError_ = "";
    request->onDisconnect([this, request] {
      if (uploadOwner_ == request) {
        library_.abortUpload();
        uploadOwner_ = nullptr;
      }
    });
    char name[SongLibrary::MAX_NAME + 1];
    String wanted = request->arg("name");
    SongLibrary::cleanName(wanted.length() ? wanted.c_str() : fileName.c_str(), name, sizeof(name));
    if (player_.isCurrent(name)) {
      uploadError_ = SONG_IS_PLAYING_MESSAGE;
      return;
    }
    if (!library_.beginUpload(name, uploadError_)) return;
  }
  if (request != uploadOwner_ || uploadError_.length()) return;
  if (length) library_.writeUpload(data, length);
  if (final) uploadOk_ = library_.endUpload(uploadError_);
}

// Called once the whole request has arrived (after the last chunk).
void WebPortal::handleUploadDone(AsyncWebServerRequest *request) {
  if (request != uploadOwner_) {
    reply(request, false, "Another upload is in progress, try again in a moment");
    return;
  }
  uploadOwner_ = nullptr;
  reply(request, uploadOk_, uploadOk_ ? "" : uploadError_);
}

// Captive portal: on the setup hotspot every unknown address (phone connectivity checks
// included) is redirected to the setup page, so it pops up by itself.
void WebPortal::handleNotFound(AsyncWebServerRequest *request) {
  if (wifi_.apActive() && request->host() != WiFi.softAPIP().toString()) {
    request->redirect("http://192.168.4.1/");
    return;
  }
  request->send(404, "text/plain", "Not found");
}

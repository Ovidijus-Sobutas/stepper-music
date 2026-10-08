// HTTP server on port 80 (ESPAsyncWebServer): the page files from web/ (packed into WebAssets.h
// by tools/build_web.ps1), Wi-Fi setup (also as a captive portal on the setup hotspot) and the
// API used by the page. Errors are answered with 409 (400 for a bad Wi-Fi form) and a plain-text
// message that the page shows as it is.
//   GET  /status, /api/songs, /api/sound, /scan
//   POST /api/play?song=, /api/pause, /api/resume, /api/stop, /api/level?value=, /api/test?motor=
//   POST /api/upload?name= (multipart), /api/delete?song=, /api/rename?song=&to=, /api/demos
//   POST /api/sound?style=id  or  /api/sound?key=k&value=v
//   POST /api/queue?source=all|playlist[&name=][&shuffle=1], /api/next, /api/prev,
//        /api/queue-settings?repeat=off|all|one&rest=s
//   GET  /api/playlists   POST /api/playlist?name=&songs=a%0Ab (save), /api/playlist-delete?name=
//   POST /save (ssid, pass), /forget (all networks), /api/wifi-remove?ssid=
//   GET  /api/info   POST /api/restart, /api/factory-reset?confirm=RESET
// A route also answers "<route>/anything": no route may be another one plus "/...".
//
// The async server handles requests between passes of the main loop, never in the middle of it,
// and never blocks the loop: a slow phone can no longer starve the music (2026-10-02). Handlers
// must not wait (no delay/yield): work that waits (Wi-Fi changes, scans, restart) or takes long
// (restoring demo songs) is recorded and carried out by poll(), which runs in the main loop.
#pragma once
#include <Arduino.h>
#include "../wifi/WifiManager.h"
#include "../gt2560/Gt2560Link.h"
#include "../music/Player.h"
#include "../music/SongLibrary.h"
#include "../music/Sound.h"
#include "../music/Queue.h"

class AsyncWebServerRequest;
struct WebAsset;

class WebPortal {
public:
  WebPortal(WifiManager &wifi, Gt2560Link &gt, Player &player, SongLibrary &library, Sound &sound, Queue &queue)
      : wifi_(wifi), gt_(gt), player_(player), library_(library), sound_(sound), queue_(queue) {}
  void begin();
  void poll();  // call every loop: deferred work

private:
  void registerPageRoutes();
  void registerStatusRoutes();
  void registerWifiRoutes();
  void registerSystemRoutes();
  void registerPlaybackRoutes();
  void registerQueueRoutes();
  void registerSoundRoutes();
  void registerSongRoutes();

  void startRequestedScan();
  void applyPendingWifiChange();
  void restartWhenDue();
  void restoreDemosIfRequested();

  void sendAsset(AsyncWebServerRequest *request, const WebAsset &asset);
  void handleStatus(AsyncWebServerRequest *request);
  void handleSongs(AsyncWebServerRequest *request);
  void handleSound(AsyncWebServerRequest *request);
  void handlePlaylists(AsyncWebServerRequest *request);
  void handleInfo(AsyncWebServerRequest *request);
  void handleScan(AsyncWebServerRequest *request);
  void handleUploadChunk(AsyncWebServerRequest *request, const String &fileName, size_t index, uint8_t *data,
                         size_t length, bool final);
  void handleUploadDone(AsyncWebServerRequest *request);
  void handleNotFound(AsyncWebServerRequest *request);

  WifiManager &wifi_;
  Gt2560Link &gt_;
  Player &player_;
  SongLibrary &library_;
  Sound &sound_;
  Queue &queue_;

  // Upload state (one upload at a time).
  AsyncWebServerRequest *uploadOwner_ = nullptr;
  bool uploadOk_ = false;
  String uploadError_;

  // Deferred to the main loop.
  bool scanRequested_ = false;
  bool saveWifiPending_ = false, forgetWifiPending_ = false;
  String newSsid_, newPassword_, ssidToRemove_;
  uint32_t wifiChangeAtMs_ = 0;  // after the answer has left (switching networks drops the connection)
  bool restoreDemosPending_ = false;  // set by /api/demos, carried out by poll()
  uint32_t restartAtMs_ = 0;  // 0 = no restart planned
  bool factoryResetPending_ = false;
};

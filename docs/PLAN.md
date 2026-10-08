# Wi-Fi Stepper Music Player — Build Plan

GT2560 Rev A (ATmega2560) + NodeMCU ESP8266 0.9

Last updated: 2026-10-05 · Firmware: GT2560 **0.6.0**, ESP8266 **0.11.0** (code refactored for readability; see README.md)

---

## Progress overview

What works today: Wi-Fi setup from a phone (hotspot `MusicPlayer-XXXX`, password from `.env`),
web page with songs, upload, playback controls, 10 sound styles, OLED status, 5 demo songs in
the ESP's flash, all 5 motors playing with safety stops.

| Step | What | Status |
|---|---|---|
| A1 | Wiring + raw serial link ESP ↔ GT2560 | ✅ |
| A2 | Link protocol: frames + CRC, retries, heartbeat, safety timeout | ✅ |
| A3 | Motor test per motor, `disableAllMotors()` | ✅ (reset check with 12 V still to do) |
| A4 | Music engine on the GT2560 (buffer, song clock) | ✅ |
| B1 | ESP streams songs with flow control | ✅ |
| B2 | Web page with controls | ✅ |
| B3 = G1 | Song library in flash: upload, rename, delete, demo songs | ✅ |
| B4 | Pause / resume / stop | ✅ · Next / previous / Play All / Shuffle → G4 |
| C3 | OLED screens | ✅ (bold font; brighter on 5 V) |
| C5 | Wi-Fi setup hotspot + captive portal, FLASH-hold reset, `musicplayer.local` | ✅ (several saved networks → G5) |
| M1 | Motor drive rewrite: timers, soft start, stall guard, CPU guard, motors off when silent | ✅ |
| M2 | Continuous-rotation drive ("even" mode) after the jzkmath instrument | ✅ |
| M3 | **Sound styles**: selectable ways to play, chosen on the web page (section 0.1); narrowed to 8 smooth styles after listening | ✅ · gift default: Smooth or Balanced |
| G2 | Converter: JavaScript port of `midi_to_stepper.py` (`web/converter.js`), `.mid` upload on the page | ✅ all 5 songs byte-identical (`tools/converter_test.html`); end-to-end upload on the real page |
| G3 | Convert panel (`web/convert.js`): instruments/tracks with names (drums off by default), motor assignment (spread / one per instrument / by pitch), keep-in-range folding, transpose, speed 50–200 %, piano-roll preview, notes-at-once and cut-short counts, Listen in the browser | ✅ tested on this PC and on the player (ESP 0.9.0) |
| W1 | **Web server that never blocks the music**: ESP Async WebServer 3.12.1 + ESP Async TCP 2.0.0 (ESP32Async); ESP8266 core updated 3.0.2 → 3.1.2 (needed by the library; same flash layout) | ✅ `tools/test_stress.ps1`: song + 3 clients polling every 0.3 s + 70 KB upload: 0 late notes, 0 link failures, buffer ≥ 248 |
| G4 | Playlists (`/playlists/<name>.txt`, editor on the page), Play All, Shuffle (no song twice in a round), Repeat off/all/one, next/previous, rest between songs (0–10 s, "UP NEXT" screen); the queue stops on Stop or an error, not only at the end | ✅ 2026-10-02: playlist played through with rest, next/prev, Repeat One, rename updates playlists, stress test passed (buffer ≥ 157, 0 late, 0 link failures) |
| G5 | Up to 5 saved Wi-Fi networks (newest first; with several, a scan picks the strongest in range and the next is tried after 30 s), Settings card (networks, About, restart, factory reset = erase LittleFS + restart, demos come back), console `wifi remove`, `restart`, `factory reset` | ✅ 2026-10-02: fallback from an absent network to the home network, boot with 2 saved networks, remove. Factory reset **not run** on the real player (would erase the songs and Wi-Fi), only its RESET guard |
| G6 | Firmware update from the web page | |
| G7 | Gift-proofing: link on hardware UART, one power supply, OLED on 5 V, guitar mounting, long-run test | |
| C1 | Link on the ESP's hardware UART (removes the ~1% retries) | part of G7 |
| C2 | SD card | optional (flash holds ~100 songs) |
| C4 | Printer knob or buttons on the device | optional |
| D1–D3 | MIDI on the ESP | replaced by the browser converter (G2/G3) |
| E1–E3 | Online songs / GitHub sync | optional: done by the browser, not the ESP |
| F1–F4 | Settings, recovery, OTA, final build | folded into G5–G7 |

---

## 0. Direction change (2026-10-01): a stand-alone gift

The player will be given away, so it must work on any Wi-Fi with **no server of ours**. The
recipient joins the setup hotspot `MusicPlayer-XXXX` (password set in `.env` when the firmware
is built, shown on the OLED), picks their Wi-Fi, and from then on uses the web page:
upload, play, playlists, settings.

**MIDI conversion runs in the browser.** The converter is JavaScript inside the web page that the
ESP serves from its flash; the phone or PC runs it, the ESP only stores and plays finished
`.stepper` songs. No internet, server or app needed.

| Step | What | Status |
|---|---|---|
| G1 | Songs in the ESP's flash (LittleFS `/songs`): upload `.stepper`, list, rename, delete, storage use; 5 demo songs installed on first start + "Restore demo songs"; uploads keep the music running; setup hotspot password | ✅ `tools/test_songs.ps1` |
| G2 | Converter page: JavaScript port of `midi_to_stepper.py`, byte-identical output on the 5 songs | ✅ |
| G3 | Converter settings (assignment mode, tempo, transpose, tracks/channels, motors used, note ranges) + preview (notes per motor, timeline, listen in browser) | ✅ |
| G4 | Playlists, Play All, Shuffle, Repeat, rest between songs | ✅ |
| G5 | Several saved Wi-Fi networks, settings page, factory reset | ✅ (factory reset untested on the device) |
| G6 | Firmware update from the web page | |
| G7 | Gift-proofing: link on hardware UART, single power supply, OLED on 5 V, mounting, long-run test | |
| opt. | Online library fetched by the browser (public GitHub repo); printer knob / buttons | |

### 0.0 Web page files

The page is plain files in `esp8266_player/web/`: `index.html` (markup only), `style.css`,
`app.js` (player, songs with search and "Show more", playlists, sound, settings), `convert.js`
(Convert MIDI card) and `converter.js` (MIDI → .stepper). After editing them, run
`tools/build_web.ps1`: it gzips every file in the folder into `src/web/WebAssets.h`, which the
firmware serves with `Content-Encoding: gzip` (about 23 KB in total).
Converter test: run `tools/serve.ps1` (local web server, `/ref/` = the stepper_music folder) and
open `http://localhost:8123/tools/converter_test.html`.

### 0.1 How the motors play: sound styles

The web page's **Sound style** card switches between these ways of driving the motors. A style
applies from the next note, also mid-song, is saved on the ESP (`/config/sound.txt`) and is sent
to the GT2560 again after the GT2560 restarts. **Fine-tune** changes single settings; a mix that
matches no style shows as **Custom**.

Two drive methods underneath:

- **Continuous rotation ("even")**, after jzkmath's Arduino MIDI stepper instrument: the motor
  turns at one full step per note period, so the step rate is the pitch. With the 1/16 jumpers
  that is 16 evenly spaced microsteps per period. *Grain* = how many microsteps go out together:
  1 is smoothest, 16 equals full-step driving. Level has no effect.
- **Bursts ("push")**: once per note period a burst of level × 2 microsteps, spread by the step
  spacing, with soft start and stall guard. Level = loudness. This is the stepper_music method,
  improved.

Styles since ESP 0.7.1 (all continuous rotation; in continuous rotation speed is tied to pitch,
so "fast" = one octave higher):

| Style | Grain | Octave | Character |
|---|---|---|---|
| **Smooth** (default) | 1 | 0 | smoothest, quietest; "more beautiful" |
| **Soft** | 2 | 0 | smooth, a little more body |
| **Balanced** | 4 | 0 | like 1/4 stepping; **user's favourite so far** |
| **Fast & smooth** | 1 | +1 | twice the rotation speed, smooth |
| **Fast & soft** | 2 | +1 | |
| **Fast & balanced** | 4 | +1 | (was "Bright") |
| **Smooth low** | 1 | −1 | slower, deeper |
| **Deep** | 4 | −1 | Balanced, an octave lower |

Removed after listening (2026-10-02): **Push** and **Classic** (bursts: "grinding gears"),
**Punchy** (grain 8) and **Full step** (grain 16): too hard, vibrating, grinding. Anything that
moves the rotor in jerks sounds bad. The burst code stays in the GT2560 firmware, unused, and the
page no longer shows burst settings or the Level slider.

Fast styles: notes above about F♯5 would exceed the step budget at +1 octave and are played at
their normal octave.

All styles: motors are switched off 0.4 s after their last note. Fine-tune: octave (−1/0/+1),
microsteps together (1/2/4), motor-off delay, driver microstepping (must match the STEP_SIZE
jumpers).

To do: the user picks the gift's default (Smooth or Balanced so far).

#### Style review by a separate agent (planned, not started)

The styles are still being chosen by ear, so for now they stay as they are. Later, a **separate
agent** (its own session, working only on this) reviews and updates the playing styles:

- **Input:** the user's listening notes per style and song (what sounds smooth, grinding, too
  slow, too quiet); the current styles (`esp8266_player/src/music/Sound.cpp`); the GT2560 drive
  (`gt2560_engine/src/motors/ToneGenerator.cpp`); sections 0.1 and 0.2 of this plan; test songs.
- **Rules it keeps:** continuous rotation only (bursts and grain 8/16 were rejected as
  "grinding"); speed stays tied to pitch; per-motor step budget and CPU guard stay (link safety);
  motors still switch off when silent; changes go through the Fine-tune settings first, so the
  user can try them live before they become styles.
- **What it may try:** grain/octave combinations, a short speed ramp at note start for the fast
  styles, per-motor octave (bass motors lower), per-song style memory, a different default.
- **What it delivers:** an updated style list with the reason for each change, the GT2560/ESP
  changes if any, test results (no late notes, no dropped steps at the default budget), and an
  update of section 0.1.
- **Done when:** the user has picked the gift's default and at most 2–3 alternatives remain.

### 0.2 Lessons from the motor work (2026-10-02)

- The stepper_music method (bursts ~3 µs apart) is an instant jump the rotor can't follow:
  vibration, and stalls at level 7–10.
- Moving and stopping every period sounds rough ("like strong resistance"). Continuous rotation
  (one full step per period) sounds smooth: that is what stepper instruments do.
- Every step is an interrupt. Five busy motors at level 10 used ~100% of the ATmega's time and the
  link failed, so there is a step budget per motor (24000 pulses/s) and late steps are dropped
  instead of caught up. Main-loop pauses stay at 3–7 ms.

Lessons from G4/G5 (2026-10-02):

- ESPAsyncWebServer: a route `/api/x` also answers `/api/x/anything`, and the first route
  registered wins. `/api/queue/settings` started "play all". Routes now use flat names
  (`/api/queue-settings`, `/api/playlist-delete`); no route may be another route plus `/...`.
- Several Wi-Fi networks: the scan runs before connecting (the SDK only retries one network). The
  page's own `/scan` can take the results first, so the Wi-Fi manager starts the scan again if
  needed, with a 15 s limit.

Lessons from the repo preparation (2026-10-07):

- **Lost-reply race (fixed in Gt2560Link):** the link kept only the result of the *last*
  finished request. Saving an upload pauses the ESP's loop for ~1 s; afterwards the late reply
  arrived and, in the same pass, the overdue heartbeat PING went out. Its reply replaced the
  Player's result, so the Player waited until the 8-bit sequence number came round again
  (~2 minutes): the GT2560 ran out of notes and Stop/Play were ignored. The link now keeps the
  results of the last 8 requests. Found by `test_songs.ps1` (upload while playing, then Stop).
- Demo songs are public-domain MIDI files from the Mutopia Project (`songs/demos`), converted with
  the page's own converter (`tools/convert_demos.ps1`) and packed by `tools/build_demos.ps1`.
  Copyrighted songs stay in the git-ignored `songs/private/`.
- Local values (hotspot password, player name, tool paths) live in the git-ignored `.env`;
  `tools/prepare_firmware.ps1` turns them into `src/LocalConfig.h`.

## 1. Goal

A standalone music box that plays MIDI songs on 5 stepper motors:

- manage songs from a phone or PC over Wi-Fi (upload, rename, delete, playlists)
- play one song, Play All, or Shuffle; pause / resume / stop / next / previous
- OLED display and joystick for use without a phone
- Wi-Fi setup through its own hotspot page, with no passwords in the code
- optional sync from a GitHub song repository, optional playback from a URL
- motors always released (drivers disabled) when playback ends or anything goes wrong

### Who does what

| ESP8266 (NodeMCU) | GT2560 (ATmega2560) |
|---|---|
| Wi-Fi, web UI, config portal | Real-time timing of notes |
| microSD song storage | Stepper step generation (hardware timers) |
| MIDI parsing, note → motor mapping | Motor enable / disable, safety timeout |
| Playlists, Play All, Shuffle | Event buffer, playback clock |
| OLED + joystick | Reports status back to the ESP |
| GitHub sync, URL playback, OTA | (No Wi-Fi, no SD, no HTTP) |

The GT2560 never needs the Internet, and the ESP8266 never generates step pulses.

---

## 2. Current status

| Item | Status |
|---|---|
| Toolchain (arduino-cli, AVR + ESP8266 cores) | ✅ Working, uploads from this PC |
| GT2560 on USB (COM5, FTDI) | ✅ |
| NodeMCU on USB (COM6, CP2102) | ✅ |
| Level shifter wired and powered (HV 5 V, LV 3.3 V) | ✅ Verified with multimeter + pin test |
| ESP ↔ GT2560 serial link | ✅ **Verified: 312/312 messages each way, 0 errors, ~4 ms round trip** |
| Live monitor for both boards (`tools/monitor.ps1`) | ✅ |
| A2 protocol: frames + CRC, retries, heartbeat, 2 s safety timeout | ✅ 5000/5000 pings; corrupted frames rejected; timeout disables motors and recovers |
| A3 motor test (`test all`) | ✅ X, Y, Z, E0, E1 each enabled, toned, released |
| A4 music engine on GT2560 (timers 1/3/4/5 + Timer2 for E1, 128-note buffer, song clock) | ✅ |
| B1 ESP streams a song | ✅ Songs built into the ESP firmware for now. `pirates`: 1453 notes, 77.6 s, 0 late, 0 retries |
| Console scripting for tests (`tools/console.ps1`) | ✅ |
| Level range 1–10, default 5 (higher levels stall motors on high notes) | ✅ |
| C3 OLED, first version (U8g2, software SPI): idle / playing / paused / motor test screens | ✅ Full redraw 113 ms, so while playing only the time/progress rows are sent (44 ms, once a second) |
| Pause / resume / stop | ✅ Motors released on pause and stop |
| C5 Wi-Fi manager (own code, no library): open setup hotspot `MusicPlayer-XXXX` + captive portal page (network list, password), credentials in LittleFS `/config/wifi.txt`, auto-reconnect, hotspot reopens after 30 s (never connected) / 60 s (lost), `musicplayer.local` | ✅ Hotspot verified; phone setup pending user test |
| Wi-Fi reset: **hold FLASH (GPIO0) 10 s** with countdown on the OLED (RST can't be timed: the chip is off while it's held). Also `wifi forget` in the console | ✅ Code; pending user test |
| OLED brightness: contrast 255, pre-charge 0xF1, VCOMH 0x40; bold 6×13 font | ✅ Still dim on 3.3 V: power from 5 V |
| B2 web page: now playing (progress, pause/resume, stop), level slider, song list with Play, motor test, Wi-Fi settings; JSON API `/status`, `/api/*` | ✅ Tested from the PC over Wi-Fi (`tools/test_web.ps1`) |
| Link hardening for Wi-Fi traffic: GT2560 buffer 256 notes, link timeout 6 s (web server can block up to 5 s on weak Wi-Fi), retries answered from a reply cache (never run twice), song time in every note ack | ✅ 2 × full test: 0 timeouts, 0 late notes, 0 failures; ~1% retries from software serial RX errors while Wi-Fi is busy |
| Web page responsiveness: connections closed after each answer, instant button feedback, request timeouts; motor test stops a song, Play cuts a test short | ✅ ~30 ms per request |
| G1 song library (`tools/test_songs.ps1`): upload during playback keeps the music running | ✅ 70 KB in ~2.6 s, buffer stays full |
| Motor drive + sound styles (`tools/test_sound.ps1`) | ✅ styles reach the GT2560 mid-song and after a GT2560 restart |

Note: FLASH is GPIO0 (D3), so the SD card CS must not use D3 (see 4.2).
Note: software serial RX on the ESP sees ~1% bad frames while Wi-Fi is busy (recovered by retries). Moving the link to the ESP's hardware UART (C1) removes this.
Note: the OLED looked dim at 3.3 V; its on-board regulator (662K) drops that to ~3.0 V. Power VCC from the NodeMCU's VIN (5 V) instead.

Link rule learned in A2: the ESP's software serial can't receive while it transmits (interrupts are blocked during each byte to avoid bit errors). So **only one frame is in flight at a time**, and the GT2560 only answers.

### Parts on hand (2026-09-30) and revised order

- On hand: OLED (type to be confirmed), 5 steppers, printer LCD + knob. **No SD module, no buttons yet.**
- Songs stay in the ESP's internal flash (LittleFS, 2–3 MB, roughly 50–100 songs). **The SD card (C2) is optional** and only needed for a bigger library.
- The web page on the phone is the controller until physical input exists.
- Physical input (C4) may use the **printer knob (rotary encoder + click)** instead of a 5-way switch. Two checks are needed first: the LCD board must not pull the encoder lines to 5 V, and the knob must not rest in a "closed" position at boot (it would use the boot-sensitive pins D3/D4).
- Revised order: **A2 → A3 → A4 → B1 → B2 → C3 (OLED) → B3 → B4 → C5 → D → E**, then C4 (knob or buttons) and C2 (SD) when parts arrive. C1 (move link to hardware UART) is only required together with C2.

Lessons from the link work:

- The LCD ENCODER header on this board is **physically reversed** compared with common diagrams. TX1/RX1 are at the end that diagrams label GND/5V. Always confirm header pins by measuring.
- If the level shifter's HV side has no supply, signals pass weakly in one direction only. Measure HV = 5 V and LV = 3.3 V first whenever the link acts up.
- If the link stops working, check in this order:
  1. Level shifter power: HV = 5 V (from the GT2560), LV = 3.3 V (from the NodeMCU), GND shared.
  2. The two signal wires: GT2560 D18 (TX1) → HV1/LV1 → NodeMCU D5, and D19 (RX1) ← HV2/LV2 ← D6.
     Swapped wires are the most common fault; check continuity with a multimeter, power off.
  3. With both firmwares running, `ping 100` in the ESP console (`tools\console.ps1`) must answer
     100 of 100. `stat` shows bad frames / timeouts on both sides.
  (The pin-level diagnostic sketches used during bring-up, step A1, are no longer in the repo.)

---

## 3. Hardware facts (verified)

### GT2560 Rev A

- ATmega2560, 16 MHz, 256 KB flash, **8 KB RAM** (keep buffers small).
- USB serial through FTDI on Serial0. Opening the port resets the board (DTR auto-reset).
- Serial ports:
  - Serial0 (0/1): USB, keep it for uploads and debug.
  - **Serial1 (TX1 = D18, RX1 = D19): link to the ESP**, on the LCD ENCODER header.
  - Serial2 (16/17): also on the LCD ENCODER header, spare.
  - Serial3 (14/15): not wired to any connector.
- Stepper pins, from Marlin `pins_GT2560_REV_A.h` and matching your `stepper_music/config.h`:

| Motor | STEP | DIR | ENABLE (LOW = on) |
|---|---|---|---|
| X  | 25 | 23 | 27 |
| Y  | 31 | 33 | 29 |
| Z  | 37 | 39 | 35 |
| E0 | 43 | 45 | 41 |
| E1 | 49 | 47 | 48 |

- Heaters D2, D3, D4 and the bed MOSFET: firmware forces them LOW at boot. Keep heaters and thermistors disconnected for this project.
- Board fan on D7: turned on while powered, as in your current firmware.
- Stepper-driver ENABLE lines have no pull-up in the schematic, so they may float during a reset or while the bootloader runs. This is **checked in step A3**, and fixed with 10 kΩ pull-ups if needed.

### NodeMCU 0.9 (ESP-12, 4 MB flash)

- About 40–50 KB free RAM for the application. HTTPS (GitHub) needs 20–30 KB, so network jobs run only when nothing is playing.
- Boot-sensitive pins:
  - GPIO0 (D3) and GPIO2 (D4) must be HIGH at boot.
  - GPIO15 (D8) must be LOW at boot.
  - Never hold a button on these pins during power-up.
- A0 range is 0–1 V or 0–3.3 V depending on the board variant. It gets measured before designing the joystick resistor ladder.
- Your board enumerates as CP2102. Its USB-serial chip is wired to TX/RX (GPIO1/GPIO3).

---

## 4. Wiring

### 4.1 Now (steps A–B): link on D5/D6

```
GT2560 LCD ENCODER            Level shifter            NodeMCU
D18 (TX1)  ─────────────►  HV1 ═══ LV1  ─────────►  D5  (ESP RX, software serial)
D19 (RX1)  ◄─────────────  HV2 ═══ LV2  ◄─────────  D6  (ESP TX, software serial)
5V         ─────────────►  HV
                               LV   ◄─────────────  3V3
GND        ─────────────►  GND  ◄─────────────────  GND
```

Both boards are powered from USB during development. **Don't connect the NodeMCU's VIN to the GT2560's 5V while both are on USB.**

### 4.2 Final: the link moves to hardware UART, and SD/OLED/joystick are added

The SD card needs D5/D6/D7 (hardware SPI), so in step C1 the link moves to the ESP's real UART:

| Function | NodeMCU pin | GPIO | Goes to |
|---|---|---|---|
| Link: ESP RX | **RX** | 3 | LV1 (from GT2560 D18 TX1) |
| Link: ESP TX | **TX** | 1 | LV2 (to GT2560 D19 RX1) |
| SD SCK | D5 | 14 | SD module SCK |
| SD MISO | D6 | 12 | SD module MISO |
| SD MOSI | D7 | 13 | SD module MOSI |
| SD CS | D8 | 15 | SD module CS. D3 is the FLASH button (Wi-Fi reset) and D0 is OLED DC. D8 must be LOW at boot: use an SD module without a pull-up on CS, and move the GT2560 reset transistor to another pin (decide at C2) |
| OLED clock (module pin D0) | D1 | 5 | 7-pin SPI SSD1306, software SPI |
| OLED data (module pin D1) | D2 | 4 | |
| OLED RES | D7 | 13 | |
| OLED DC | D0 | 16 | |
| OLED CS | — | — | Tied to GND (only device on these lines) |
| OLED VCC / GND | **5 V** / GND | | The module's own regulator makes 3.3 V; on 3V3 it is dim. 5 V from the level shifter's HV pin or NodeMCU VIN |
| Joystick (5 directions) | A0 | ADC | Resistor ladder (section 4.3) |
| GT2560 reset | D8 | 15 | 1 kΩ → NPN base; collector → GT2560 RESET (SD CARD header); emitter → GND |
| Spare | D3, D4 | 0, 2 | Keep free. D4 can carry a debug log (TX only) |

Power in the final build:

```
12 V PSU ──► GT2560 DC IN ──► on-board 5 V regulator (MP2303, 3 A)
                                   │
                                   ├─► LCD ENCODER 5V ──► NodeMCU VIN   (+ 470 µF cap at VIN)
                                   └─► level shifter HV
NodeMCU 3V3 ──► level shifter LV, SD module (if 3.3 V type), OLED VCC
```

Uploading ESP firmware over USB still works with the link connected. The GT2560 only talks when spoken to, and ignores anything that isn't a valid frame, including the ESP's boot messages and upload traffic.

### 4.3 Joystick resistor ladder (values confirmed after the A0 measurement)

One resistor from 3.3 V to A0, and each button pulls A0 toward GND through a different resistor. Starting values if A0 accepts 0–3.3 V:

| Button | Resistor to GND | ≈ Voltage |
|---|---|---|
| none | — | 3.3 V |
| UP | 0 Ω | 0.0 V |
| RIGHT | 1 kΩ | 0.3 V |
| DOWN | 2.2 kΩ | 0.6 V |
| LEFT | 4.7 kΩ | 1.05 V |
| PRESS | 10 kΩ | 1.65 V |

The pull-up to 3.3 V is 10 kΩ. Holding **PRESS at power-up = Wi-Fi config mode**. This uses A0, so it's safe at boot.

---

## 5. Firmware architecture

### 5.1 Project layout

```
stepper-music/
├── .env.example          local settings template (hotspot password, player name, tool paths)
├── docs/                 PLAN.md (this file)
├── gt2560_engine/        GT2560 firmware (Arduino sketch)
│   └── src/
│       ├── config.h      all settings, with units
│       ├── comm/         EspLink (commands from the ESP), LinkFrame (frames + CRC)
│       ├── motors/       MotorManager (enable pins), ToneGenerator (timer-driven steps)
│       └── music/        MusicEngine (note buffer, song clock)
├── esp8266_player/       ESP8266 firmware
│   ├── web/              the web page: index.html, style.css, settings.js, app.js, convert.js, converter.js
│   └── src/
│       ├── config.h      all settings; LocalConfig.h is generated from .env (not committed)
│       ├── gt2560/       Gt2560Link (link to the GT2560), LinkFrame (same codec as the GT side)
│       ├── music/        Player, Queue (playlists), SongLibrary (LittleFS), Sound (styles), EmbeddedSongs (demos)
│       ├── web/          WebPortal (HTTP API), WebAssets.h (packed page, generated)
│       ├── wifi/         WifiManager (saved networks, setup hotspot, mDNS)
│       ├── display/      Display (OLED screens)
│       └── system/       Console (USB serial commands)
├── songs/demos/          public-domain demo songs (songs/private/ is ignored)
├── tools/                build and test scripts (see tools/README.md)
└── logs/                 test output (ignored)
```

Libraries:
- ESP8266 core 3.1.2 built-ins: WiFi, mDNS, DNSServer, LittleFS, SoftwareSerial
- **U8g2** (OLED, supports SSD1306 and SH1106)
- **ESP Async WebServer** 3.12.1 + **ESP Async TCP** 2.0.0 (ESP32Async); JSON is built by hand
- No extra libraries on the GT2560

### 5.2 Motor drive (GT2560 `ToneGenerator`)

- Timer1 and Timer3 run freely at 2 MHz; each motor has its own output-compare channel
  (1A, 1B, 1C, 3A, 3B) that schedules its next step. Timers 2, 4, 5 are no longer used.
- One interrupt = one step, or one group of steps (the style's grain).
- Guards: stall guard (push mode), step budget per motor (CPU), late steps dropped, notes over
  the budget played an octave lower (continuous mode).
- Each motor's driver is switched on at its note and off after the release time.
- Settings change at run time with the `CFG` frame (section 0.1); the ESP owns the saved values.
- `.stepper` format (time_ms, motor, note, duration_ms) is the song format. `midi_to_stepper.py`
  is the reference for the browser converter (G2).
- The original LCD + SD firmware (the earlier stepper_music project) is kept outside this repo as a fallback.

### 5.3 Link protocol (both directions)

Readable text frames, so the traffic can be followed in the monitor:

```
$<TYPE>,<seq>[,<fields>...]*<CRC8 hex>\n        max 60 bytes per frame
```

- Anything that doesn't start with `$` or fails its CRC is dropped and counted. This makes the link immune to boot messages and noise.
- `seq` goes 0–255 and wraps. Replies echo the sequence number they answer.
- Link speed:
  - 57600 baud while on software serial (steps A–B)
  - 250000 baud on hardware UART (step C1 onward): 0% timing error on both chips
  - fallback 115200

As implemented (57600 baud, software serial on the ESP):

| ESP → GT2560 | Meaning | GT2560 reply |
|---|---|---|
| `PING` | Heartbeat every 500 ms, also used to poll buffer space | `PONG,<state>,<free>,<songMs>` |
| `VER` | Firmware version | `VER,<version>` |
| `STAT` | Link counters | `STAT,<state>,<uptime>,<ok>,<bad>,<overflow>,<junk>,<timeouts>,<motors>` |
| `PS` | Playback status | `PS,<state>,<songMs>,<buffered>,<late>,<motors>,<level>,<repeats>,<loopGapMs>,<droppedSteps>` |
| `LOAD` | Clear buffer and song clock, state READY | `OK` |
| `N,<t>,<motor>,<note>,<dur>` | Note event | `A,<free>,<songMs>` |
| `END,<t>` | Song ends at time t | `OK` |
| `PLAY` / `PAUSE` / `RESUME` / `STOP` | Transport | `OK` |
| `LEVEL,<n>` | Burst loudness | `OK` |
| `CFG,<key>,<value>` | Drive setting: `mode group oct level spacing ramp maxfs release micro budget`; presets `classic`, `default`; `show` | `OK`, or `TUNE,...` for show |
| `TEST,<motor>,<note>,<ms>` | Test tone | `OK` |

| GT2560 → ESP (unrequested) | Meaning |
|---|---|
| `BOOT,<version>` | Sent once after every reset; the ESP re-sends the sound style |

Rules:
- **One frame in flight:** the ESP waits for each reply (the software serial port can't receive
  while it transmits). No reply in 100 ms → the same frame again, up to 3 tries. A repeated frame
  (same seq and type within 1 s) gets the cached reply and is never executed twice.
- **Flow control:** notes are sent while the GT2560 reports free space; playback starts after 64
  notes. Buffer: 256 notes (6–12 s of music).
- **Timing:** the GT2560 keeps the song clock; the ESP never times notes.
- **Safety timeout:** no valid frame for 6 s → playback stopped, all motors released, IDLE.
  (6 s because the ESP's web server can block for up to 5 s on a weak Wi-Fi link.)

### 5.4 GT2560 states

```
BOOT ──► IDLE ◄──────────────────────────────┐
          │ LOAD                              │ STOP / DONE / timeout / error
          ▼                                   │   (always via disableAllMotors)
        READY ──PLAY──► PLAYING ◄──RESUME── PAUSED
                          │  └──PAUSE──────────┘
                          └── END reached ─► DONE ─► IDLE
```

`disableAllMotors()` is the only function that releases motors: it stops the timers, sets STEP LOW and sets ENABLE HIGH. Every stop and error path calls it.

### 5.5 ESP player states (shown on both the OLED and the web page)

`STOPPED → LOADING → PLAYING ⇄ PAUSED → FINISHED → (next song | STOPPED)`, plus `ERROR`.

Play modes: single song, Play All (in order), Shuffle (every song once per round, no immediate repeat), and optional Repeat. A configurable rest between songs keeps your old 10 s motor cooldown.

---

## 6. Build steps (each ends with a test)

These are the original step descriptions. **Current status is in the Progress overview at the
top**; since the gift decision, stages D–F are replaced by G2–G7.

Legend: 🔧 = needs new parts, 🎵 = motors + 12 V needed.

### Stage A: link and safety (no new parts)

| Step | What | Test |
|---|---|---|
| **A1** ✅ | Wiring and raw serial link | 312/312 each way, 0 errors (done) |
| **A2** | Protocol v1: frames, CRC, `PING/PONG`, `VER`, `STAT`, heartbeat, 2 s timeout. GT2560 boots with motors disabled | 1000 PINGs with 0 losses; corrupted frames rejected and counted; unplug a data wire → GT2560 logs timeout and goes IDLE; reconnect → recovers by itself |
| **A3** 🎵 | MotorManager on GT2560: `MEN`, `MDIS`, `TEST`, `disableAllMotors()` | Each motor hums in turn (X, Y, Z, E0, E1), then turns freely by hand. **Reset check:** hold GT2560 reset with 12 V on; if any motor locks, fit the 10 kΩ EN pull-ups |
| **A4** 🎵 | MusicEngine: port timer code from `playback.cpp`, E1 on Timer2, event buffer, song clock | ESP sends a hard-coded scale; notes sound in time on all 5 motors; motors released at the end |

### Stage B: music over Wi-Fi (no new parts)

| Step | What | Test |
|---|---|---|
| **B1** 🎵 | ESP streams a `.stepper` file from its internal flash (LittleFS) with flow control | `pirates.stepper` plays start to finish with correct tempo; `DONE` received; motors released |
| **B2** | Minimal web page (Wi-Fi password in a local `secrets.h`, not committed): status, song list, Play, Stop, motor test | Control from your phone's browser at the ESP's IP address |
| **B3** | Upload / delete / rename songs in LittleFS from the web page | Upload `hotel.stepper` from the phone, play it, delete it |
| **B4** | Pause / Resume / Stop / Next / Previous, Play All, Shuffle, rest between songs, motors off at the end | Play All over 3 songs; Shuffle over 5 songs plays each once; after the last song all motors are free |

After stage B you already have a working Wi-Fi music box. Everything after this adds convenience.

### Stage C: local hardware (🔧 parts needed)

| Step | What | Test |
|---|---|---|
| **C1** | Move the link to hardware UART: D5 wire → RX, D6 wire → TX; 250000 baud | Rerun the A2 tests at the new speed |
| **C2** 🔧 | SD card + Storage abstraction (`/songs`, `/playlists`, `/cache`, `/config`); songs move to SD; filename validation | List, create, read, rename, delete each tested; SD removed → "SD: NOT FOUND", internal flash songs still play |
| **C3** 🔧 | OLED: startup diagnostics, Wi-Fi status, main screen, browser, now playing with time, settings, errors, sync | Every screen shown once; unplugged OLED doesn't stop anything else |
| **C4** 🔧 | Joystick: measure A0 range, calibrate ladder, debounce, short and long press | Each direction recognised 20/20 times; menu navigation and playback controls work without Wi-Fi |
| **C5** | Wi-Fi manager: hotspot `MusicPlayer-XXXX` + setup page, saved credentials, reconnect, clear credentials (hold PRESS at boot), `musicplayer.local` | Configure from phone → reboot → reconnects by itself; router off/on → reconnects; clear credentials works without Wi-Fi |

### Stage D: MIDI directly (no new parts)

| Step | What | Test |
|---|---|---|
| **D1** | MIDI parser on ESP: format 0 and 1, tempo changes, running status, note on/off, velocity | For all 5 songs in `stepper_music`, event times and notes match `midi_to_stepper.py` output within 1 ms |
| **D2** | Note → motor mapping from `/config/mapping.json`: note range per motor, octave folding, transpose, invert, enable, voice allocation (round-robin / by pitch / by channel, like the Python options) | Change mapping in the web page → audible difference; notes outside a motor's range are folded instead of dropped |
| **D3** | Play `.mid` files directly (`.stepper` still supported) | Upload `howl.mid` from the phone and play it with no PC conversion |

### Stage E: online (no new parts; only after stage D is solid)

| Step | What | Test |
|---|---|---|
| **E1** | Play from URL (HTTP/HTTPS): download to `/cache`, then play. MIDI files are 10–60 KB, so this takes about a second | Paste a raw GitHub MIDI link → it plays; pull the Wi-Fi mid-download → clean error, motors off |
| **E2** | GitHub sync: `manifest.json` with name, path, size, sha; only changed files downloaded (temp file then rename); optional cleanup | Add, change and remove songs in the repo → sync result matches; second sync downloads nothing |
| **E3** | Startup sequence: SD → GT2560 → config → Wi-Fi → sync (if configured) → library → Ready, with offline mode | No Internet → "Offline mode" and local songs still play |

### Stage F: finishing

| Step | What | Test |
|---|---|---|
| **F1** | Web settings: Wi-Fi, music, playback, online, hardware (versions, motor status), system (restart ESP, restart GT2560, factory reset) | Every setting survives a reboot |
| **F2** | Recovery: ESP watchdog, GT2560 watchdog, ESP reboot mid-song, GT2560 reboot mid-song | In every case the motors end up disabled and the system recovers without USB |
| **F3** | OTA firmware update for the ESP from the web page | Update from the browser; USB flashing still works as a fallback |
| **F4** 🔧 | Final power (ESP from GT2560 5 V), mount motors on the guitar, cable tidy | Full day of Play All without a fault |

GT2560 firmware updates stay USB-only, because its bootloader only listens on the USB port.

---

## 7. Parts list

### Already have

| Part | Notes |
|---|---|
| Geeetech GT2560 Rev A | Drivers on board |
| NodeMCU ESP8266 0.9 | |
| 5 × stepper motors + cables | X, Y, Z, E0, E1 |
| 12 V power supply (from the printer) | Motors only, heaters disconnected, so it needs little current |
| Bidirectional level shifter (BSS138, 4 channels) | 2 channels used, 2 spare |
| USB cables × 2, multimeter | |
| RepRapDiscount Smart Controller (LCD + SD slot) | Not used in this project: its SD slot is built for the GT2560's 5 V signals, and SD belongs to the ESP. Kept as a fallback for the old `stepper_music` firmware |
| Printer SD card | Reused in the ESP's SD module (if ≤ 32 GB) |

### Needed

| # | Part | Qty | Needed for | What to look for |
|---|---|---|---|---|
| 1 | **SD card module (SPI) for the ESP** | 1 | C2 | **Full-size SD type**, so the SD card from the printer can be reused. A microSD module works too (then buy a microSD card). Either the plain 3.3 V type or the 5 V type with an on-board regulator + level shifter |
| 2 | **SD card** | 1 | C2 | Reuse the printer's card if it's 32 GB or smaller; reformat as FAT32. Songs are only kilobytes |
| 3 | **OLED display, I2C** | 1 | C3 | 0.96" SSD1306 128×64 (4 pins: GND, VCC, SCL, SDA), or 1.3" SH1106 (easier to read). Must run on 3.3 V (most do) |
| 4 | **5-way navigation switch** | 1 | C4 | A "5-way tactile / navigation switch module" (UP, DOWN, LEFT, RIGHT, CENTER + COM). **Not** the analog thumb joystick. 5 separate push buttons also work |
| 5 | **Resistor assortment** | 1 kit | C4, A3 | 1 kΩ, 2.2 kΩ, 4.7 kΩ, 10 kΩ, 22 kΩ at minimum; a 1/4 W kit covers everything |
| 6 | **2×5 IDC ribbon cable + 2×5 breakout board** | 1 | Now / C1 | Plugs into the LCD ENCODER header and gives labelled screw or pin terminals. Stops loose jumpers and header-orientation mistakes |
| 7 | **Dupont jumper wires** (M-F, F-F, M-M) | 1 set each | All | 10–20 cm |
| 8 | **Electrolytic capacitor 470 µF, 10 V or more** | 2 | F4 | Across NodeMCU VIN/GND, absorbs Wi-Fi current spikes |
| 9 | **Ceramic capacitors 100 nF** | a few | C2, C3 | Next to SD module and OLED power pins |
| 10 | **Perfboard / prototype PCB + female pin headers** | 1–2 | C2 onward | A carrier board for NodeMCU, level shifter, SD, OLED, joystick ladder |
| 11 | **NPN transistor** (2N2222, 2N3904 or BC547) + 1 kΩ base resistor | 1 | F1, F2 | Lets the ESP restart the GT2560: "Restart GT2560" in the web UI, automatic recovery. Collector → RESET on the GT2560 SD CARD header, emitter → GND, base ← 1 kΩ ← NodeMCU D8 |

### Maybe needed

| # | Part | Qty | When |
|---|---|---|---|
| 12 | **10 kΩ resistors** for stepper ENABLE pull-ups | 5 | Only if the A3 reset check shows motors locking during reset (covered by the kit in #5) |
| 13 | **USB-TTL adapter, 3.3 V** (CP2102 / CH340) | 1 | Handy after C1 to read ESP debug on D4 while the USB port carries the link. Not required |
| 14 | **ESP32 DevKit** | 1 | Only if the ESP8266 runs out of memory for HTTPS sync + web UI (stage E). The plan works on the ESP8266 first |

### Nice to have for the final build

| Part | Why |
|---|---|
| **Old guitar (soundboard)** + a hardwood strip or plate | Motors mounted near the bridge through a strip or plate (not directly on the thin top); total motor weight ≈ 1.5 kg. Loosen or remove strings to avoid unwanted ringing, or keep them for the effect |
| M3 standoffs, screws, rubber feet | Mounting the boards and damping rattles |
| Heat-shrink, cable ties, spiral wrap | Tidy, reliable cabling |
| Soldering iron + solder | Perfboard carrier and final wiring |

### Shopping summary (minimum)

Full-size SD card module (reuse the printer's SD card), I2C OLED, 5-way switch module, resistor kit, 2×5 IDC breakout + ribbon, jumper wires, 470 µF capacitors, 100 nF capacitors, perfboard + headers, NPN transistor (2N2222 / 2N3904 / BC547).

---

## 8. Risks and how they're handled

| Risk | Handling |
|---|---|
| Motors energised during GT2560 reset (floating ENABLE) | Measured in A3; 10 kΩ pull-ups if needed |
| Software serial drops bytes while Wi-Fi is busy | Only used in stages A–B; CRC + sequence numbers catch it; moves to hardware UART in C1 |
| ESP8266 memory (≈ 40–50 KB) for HTTPS + web UI + SD | Network jobs only when idle; small TLS buffers; ESP32 as fallback |
| A0 voltage range unknown | Measured in C4 before choosing ladder resistors |
| SD module pull-ups interfering with boot | CS on D0, not D8 |
| Very high notes can make steppers stall | Stall guard (bursts) / octave down when over the step budget (continuous); converter can fold notes (G3) |
| Step interrupts starving the link | Step budget per motor, late steps dropped; loop gap reported in `PS` |
| Driver heat during long Play All | Rest between songs (configurable), lower driver current (trim pots), motors off when idle |
| GT2560 8 KB RAM | 256-event buffer (2 KB), fixed buffers, no String objects; 43% RAM used |
| Header orientation mistakes | Always measure 5 V/GND on a header before wiring; IDC breakout board (#6) |

---

## 9. Working method

- One step at a time. Each step ends with its test, run on the real boards, and gets reviewed before the next step starts.
- I build and upload with arduino-cli and read both serial ports myself. You can watch live with:
  `powershell -ExecutionPolicy Bypass -File tools\monitor.ps1` (from the repo folder)
  Press Ctrl+C in the monitor before each upload, because it holds the COM ports.
- The protocol and wiring docs are updated whenever they change.

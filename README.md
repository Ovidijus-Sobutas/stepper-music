# Wi-Fi Stepper Music Player

A music box that plays songs on **5 stepper motors**: each motor turns at the pitch of its note,
so together they play the melody. You control it from a phone or a PC in a web browser: upload
MIDI files, play songs and playlists, pick a sound style. It runs on any Wi-Fi network and needs
no app, cloud service or server. The MIDI conversion runs in the browser.

Two boards share the work:

- **GT2560 Rev A** (Geeetech 3D-printer board, ATmega2560): times the notes and makes the
  step pulses for the 5 motors (X, Y, Z, E0, E1). Firmware: [`gt2560_engine/`](gt2560_engine).
- **NodeMCU ESP8266** (0.9, ESP-12, 4 MB flash): handles the Wi-Fi, the web page, the song
  storage, the OLED and playlists, and streams the notes to the GT2560 over a serial link.
  Firmware: [`esp8266_player/`](esp8266_player).

The full plan, with the protocol, the design decisions, the progress and the lessons learned, is in
**[docs/PLAN.md](docs/PLAN.md)**.

## Hardware

| Part | Notes |
|---|---|
| Geeetech GT2560 Rev A | Stepper drivers on board. STEP_SIZE jumpers set to 1/16 |
| NodeMCU ESP8266 0.9 (CP2102 USB) | |
| 5 stepper motors | On the X, Y, Z, E0 and E1 driver sockets |
| BSS138 4-channel bidirectional level shifter | 5 V GT2560 to 3.3 V ESP; 2 channels used |
| 0.96" SSD1306 128×64 OLED, 7-pin SPI | |
| 12 V power supply, about 5 A | Into the GT2560's DC input. The heaters and thermistors are **not connected** and never switched on |

## Wiring (summary)

Pictures: [esp and mega wiring.png](esp%20and%20mega%20wiring.png) and
[oled wiring.png](oled%20wiring.png). Details are in [PLAN.md section 4](docs/PLAN.md#4-wiring).

**Serial link** (57600 baud, framed text with a CRC). The GT2560 uses Serial1 on its LCD ENCODER
header. The ESP uses software serial on D5/D6:

| GT2560 | Level shifter | NodeMCU |
|---|---|---|
| D18 (TX1) | HV1 ↔ LV1 | D5 (ESP RX) |
| D19 (RX1) | HV2 ↔ LV2 | D6 (ESP TX) |
| 5V | HV | |
| | LV | 3V3 |
| GND | GND | GND |

The LCD ENCODER header is physically reversed compared with many diagrams on the internet.
**Measure 5 V and GND before you connect anything.**

**OLED** (software SPI). Module pins: D0 (clock) → NodeMCU D1, D1 (data) → D2, RES → D7,
DC → D0, CS → GND, VCC → 5 V (on 3.3 V it is dim), GND → GND. The pins are set in
`esp8266_player/src/config.h`.

**Power:** 12 V into the GT2560's DC input. During development both boards also run on their
own USB cables. While both are on USB, **don't** join the NodeMCU's VIN to the GT2560's 5 V.

## Folder layout

```
gt2560_engine/      GT2560 firmware (link, motors, music engine). Settings: src/config.h
esp8266_player/     ESP8266 firmware. Settings: src/config.h
  web/              the web page sources (index.html, style.css, settings.js, app.js,
                    convert.js, converter.js); page settings are in web/settings.js
  src/LocalConfig.h     GENERATED from .env by tools/prepare_firmware.ps1 (not in git)
  src/web/WebAssets.h   GENERATED from web/ by tools/build_web.ps1 (committed so the firmware
                        compiles without running the script; don't edit it by hand)
  src/music/EmbeddedSongs.cpp  the 5 demo songs built into the firmware
songs/demos/        public-domain demo songs (songs/private/ is for your own songs, not in git)
.env.example        template for your local settings (copy to .env, which is not in git)
docs/PLAN.md        the master plan: protocol, wiring, progress, lessons
tools/              PowerShell helpers: serial monitor, web build, hardware tests (see tools/README.md)
link_test/, gt2560_test/, nodemcu_test/   old bring-up and diagnostic sketches (each has a README)
logs/               output of the monitor and test scripts (not in git)
```

## Build and flash

You need [arduino-cli](https://arduino.github.io/arduino-cli/). The copy that comes with
Arduino IDE 2 works too: `C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe`.

One-time setup:

```powershell
arduino-cli core update-index --additional-urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli core install arduino:avr
arduino-cli core install esp8266:esp8266@3.1.2 --additional-urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli lib install "U8g2" "ESP Async WebServer@3.12.1" "ESP Async TCP@2.0.0"
```

ESP Async WebServer and ESP Async TCP are the **ESP32Async** versions. They need ESP8266 core
3.1.x. The GT2560 firmware uses no extra libraries.

**GT2560** (FTDI USB port, here COM5):

```powershell
arduino-cli compile --fqbn arduino:avr:mega:cpu=atmega2560 gt2560_engine
arduino-cli upload  --fqbn arduino:avr:mega:cpu=atmega2560 -p COM5 gt2560_engine
```

**ESP8266** (CP2102 USB port, here COM6). First create your local settings file. It is not in
git, so each copy of the project has its own:

1. Copy `.env.example` to `.env` (in the repo folder).
2. Edit `.env`: set **`WIFI_AP_PASSWORD`** (the setup hotspot password, 8-63 characters) and,
   if you like, **`PLAYER_NAME`** (the page's address becomes `http://<PLAYER_NAME>.local`).
3. Run `tools\prepare_firmware.ps1`. It writes `esp8266_player/src/LocalConfig.h` from `.env`
   and packs the web page. Run it again whenever you change `.env` or anything in
   `esp8266_player/web/`. Without it the firmware doesn't compile ("LocalConfig.h is missing").

```powershell
powershell -ExecutionPolicy Bypass -File tools\prepare_firmware.ps1
arduino-cli compile --fqbn esp8266:esp8266:nodemcu esp8266_player
arduino-cli upload  --fqbn esp8266:esp8266:nodemcu -p COM6 esp8266_player
```

The default flash layout (4 MB, 2 MB file system) is used. Re-flashing keeps the songs and the
Wi-Fi settings. Before you upload, close `tools/monitor.ps1` and any Arduino IDE serial monitor:
a COM port can be open in only one program. You can upload to the ESP with the link wires
connected.

## First-time setup (for the person who gets it)

1. Switch it on. With no Wi-Fi saved, the player opens its own hotspot **`MusicPlayer-XXXX`**.
   The OLED shows the name and the password.
2. Join that hotspot with a phone. The setup page should open by itself. If it doesn't, open
   **http://192.168.4.1**.
3. Choose your Wi-Fi network and enter its password. The OLED shows the result and the player's
   new address. From then on, open that address, or **http://musicplayer.local** on devices that
   support it. Up to 5 networks can be saved (Settings card).
4. **Forget Wi-Fi:** hold the NodeMCU's **FLASH** button for 10 s (the OLED counts down). This
   erases all saved networks and opens the setup hotspot again.

If the saved network can't be reached for a while, the setup hotspot opens again, so you can
always get back in.

**Always the same address:** the player announces itself as **http://musicplayer.local** (or the
`PLAYER_NAME` from `.env`). This works on Windows 10/11, macOS, iPhone/iPad, Linux and most
Android phones. The player also gives the router the same name, so on many home routers
**http://musicplayer** works as well. The IP address comes from the router and can change after
a restart; to fix it too, reserve it for the player in the router's DHCP settings ("address
reservation"; the player is listed as `musicplayer` among the connected devices). A fixed IP
set in the firmware is deliberately not used: it would break on any other network. The setup
hotspot is always **http://192.168.4.1**.

## Adding songs

On the web page, upload a **`.mid`** file. The page converts it to the player's `.stepper` format
**in the browser**. Before saving, you can choose the instruments and tracks, how notes are
spread over the 5 motors, transpose and speed, and you can preview it and listen. Finished
`.stepper` files can be uploaded directly too. Songs live in the ESP's flash (LittleFS), which holds
roughly 50–100 songs. The demo songs are installed on first start, and "Restore demo songs" brings
them back. The page also has playlists, Play All, Shuffle, Repeat and sound styles.

**Demo songs** are public-domain classical pieces from the [Mutopia Project](https://www.mutopiaproject.org)
(sources and licenses in [songs/demos/README.md](songs/demos/README.md)). Keep your own,
copyrighted songs in `songs/private/`, which git ignores, so they never end up in a public repo.

## Tools

The PowerShell scripts in [`tools/`](tools) are the serial monitor for both boards, the web page
packer, a local test web server and the hardware test scripts. See
[tools/README.md](tools/README.md).

## License

The code is released under the [MIT License](LICENSE) © 2026 Ovidijus Sobutas. The demo songs
in `songs/demos/` are public domain (see their README).

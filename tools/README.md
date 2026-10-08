# tools: PowerShell helpers

These scripts run in Windows PowerShell 5.1 with nothing to install. They use `curl.exe`, which
comes with Windows 10/11. Each script has a help header, which you can read with
`Get-Help .\tools\<script>.ps1 -Full`. If scripts are blocked, start them like this:

```powershell
powershell -ExecutionPolicy Bypass -File tools\monitor.ps1
```

| Script | What it does | Talks to |
|---|---|---|
| `prepare_firmware.ps1` | **Run before compiling the ESP firmware** (after a fresh clone, after editing `.env` or the web page): writes `esp8266_player/src/LocalConfig.h` from `.env`, then runs `build_web.ps1` | files only |
| `build_web.ps1` | Gzips `esp8266_player/web/*` into `esp8266_player/src/web/WebAssets.h` (also run by `prepare_firmware.ps1`) | files only |
| `DotEnv.ps1` | Shared helper: reads `.env` for the other scripts | files only |
| `serve.ps1` | Local web server on http://localhost:8123 for trying the page and the converter in a browser | files only |
| `converter_test.html` | Checks that the browser MIDI converter gives byte-identical output to the reference `.stepper` files. Open it through `serve.ps1` | files only |
| `monitor.ps1` | Live log of both boards (GT2560 yellow, ESP cyan), saved to `logs/` | both USB ports |
| `console.ps1` | Types commands into the ESP's USB console and prints both boards' output, e.g. `-Steps "test all\|8\|motor test done"` | both USB ports |
| `test_a2.ps1` | Link protocol test: 1000 pings, corrupted frames, heartbeat loss and recovery | both USB ports |
| `test_web.ps1` | Web API test: play, switch, level, pause/resume, stop, motor test, with polling | Wi-Fi + both USB ports |
| `test_songs.ps1` | Song library test: upload, invalid uploads, upload while playing, rename, delete, restore demos | Wi-Fi |
| `test_sound.ps1` | Sound styles: switching mid-song, fine-tune, check on the GT2560. Ends on "balanced" | Wi-Fi + ESP USB port |
| `convert_demos.ps1` + `.html` | Converts `songs/demos/*.mid` to `.stepper` with the page's converter (opens a page in your browser) | files + local browser |
| `build_demos.ps1` | Packs `songs/demos/*.stepper` into `esp8266_player/src/music/EmbeddedSongs.cpp` | files only |
| `test_stress.ps1` | Song + parallel `/status` pollers + 43 KB upload. The link must never drop and the buffer never run empty | Wi-Fi + ESP USB port |

## Things to know

- **The motors play during the tests**: they use the demo songs and the motor test. The tests
  also change the player's state: `test_songs.ps1` deletes and restores the demo songs, and
  `test_sound.ps1` leaves the style on "balanced".
- **One program per COM port.** Close `monitor.ps1` (Ctrl+C) and any Arduino IDE serial monitor
  before you run a test or upload firmware.
- **Opening the GT2560's port (COM5) resets the board** (FTDI DTR auto-reset). Scripts that only
  need the ESP open only its port, so a song keeps playing. On the NodeMCU, RTS restarts the ESP:
  `console.ps1 -ResetEsp` uses this on purpose.
- **Local settings come from `.env`** (copy `.env.example`): the player's address is
  `PLAYER_HOST`, or `<PLAYER_NAME>.local` when that is empty; the reference song folder is
  `REFERENCE_SONG_DIR`. Every script also takes `-Ip`, `-SongDir`, `-GtPort` and `-EspPort`
  (default ports: GT2560 = `COM5`, ESP = `COM6`; check yours in Device Manager).
- **If `musicplayer.local` doesn't work** on your PC, use the IP address instead. The router
  hands it out (DHCP), so it can change. Find it on the OLED's idle screen (bottom line), on the
  Settings card, or by typing `wifi` in the ESP console (`console.ps1 -Steps "wifi|2"`).
- `test_songs.ps1`, `test_stress.ps1` and `test_web.ps1` use the demo songs (`songs/demos`).
  Only the converter test (`serve.ps1` + `converter_test.html`) needs reference files made by the
  original Python converter, in `REFERENCE_SONG_DIR`.
- **Changing the demo songs:** `convert_demos.ps1` (MIDI → `.stepper` with the page's converter),
  then `build_demos.ps1` (packs them into the firmware). See [songs/demos/README.md](../songs/demos/README.md).
- The player is often on weak Wi-Fi, so the scripts use generous timeouts. A single failed poll
  is counted, not fatal.
- If the link between the boards stops working, follow the checklist in
  [docs/PLAN.md](../docs/PLAN.md) ("Lessons from the link work").

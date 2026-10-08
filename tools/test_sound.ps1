<#
.SYNOPSIS
  Sound style test: switches styles during a song, fine-tunes, and checks the GT2560 got them.

.DESCRIPTION
  Over Wi-Fi (HTTP API): lists the styles, plays "indila", switches through several styles while
  it plays, fine-tunes single settings (the style name must change to the matching style, or to
  "Custom"), sends an invalid value (must fail), stops, and finally restores the "balanced" style
  (the owner's chosen style).
  After each change it types "cfg" into the ESP's USB console to read back the settings the
  GT2560 reports ([CFG] line), so it checks the whole path page -> ESP -> GT2560.

  Only the ESP's USB port is opened. Opening the GT2560's port would reset it, which this test
  must avoid. Close tools/monitor.ps1 and any Arduino IDE serial monitor first.

  Changed 2026-10: the style list now uses the current style ids from
  esp8266_player/src/music/Sound.cpp. The old list had "fullstep", "punchy" and "push", which
  were removed in ESP 0.7.1. The script now ends on "balanced" instead of "smooth".

.PARAMETER Ip
  The player's address (IP or name). Default: PLAYER_HOST from .env, else <PLAYER_NAME>.local.
.PARAMETER EspPort
  COM port of the NodeMCU (CP2102 USB-serial). Default COM6.

.EXAMPLE
  .\tools\test_sound.ps1 -Ip 192.168.1.50 -EspPort COM4
#>
param([string]$Ip = "", [string]$EspPort = "COM6")
. "$PSScriptRoot\DotEnv.ps1"
if (-not $Ip) { $Ip = Get-PlayerHost (Read-DotEnv) }

$baseUrl = "http://$Ip"
# Styles to switch through during the song (ids from Sound.cpp). The list ends with "smooth"
# (grain 1, octave 0) because the fine-tune check below expects that starting point.
$stylesToTry = "soft", "fastsmooth", "deep", "smoothlow", "fastbalanced", "smooth"
$finalStyle = "balanced"   # the owner's chosen style: left set when the test ends

$espSerial = New-Object System.IO.Ports.SerialPort $EspPort, 115200
$espSerial.DtrEnable = $false; $espSerial.RtsEnable = $false   # DTR/RTS would reset the ESP
$espSerial.Open(); $espSerial.ReadExisting() | Out-Null

# POSTs to an API path; returns "<body>|<HTTP code>".
function Invoke-Post($path) { & curl.exe -s -o - -w "|%{http_code}" -X POST "$baseUrl$path" }

# Asks the ESP console for the GT2560's current drive settings; returns its [CFG] line.
function Get-GtSettings {
  # The console's "cfg" is skipped when the link is busy with notes, so try a few times.
  for ($attempt = 0; $attempt -lt 5; $attempt++) {
    $espSerial.ReadExisting() | Out-Null
    $espSerial.Write("cfg`n"); Start-Sleep -Milliseconds 600
    $cfgLine = $espSerial.ReadExisting() -split "`n" | Where-Object { $_ -match "^\[CFG\]" } | Select-Object -Last 1
    if ($cfgLine) { return $cfgLine.Trim() }
  }
  return "(no answer)"
}

$sound = Invoke-RestMethod "$baseUrl/api/sound"
Write-Output "styles: $(($sound.styles | ForEach-Object { $_.name }) -join ', ')"
Write-Output "current: $($sound.style)"
Write-Output "GT2560 now: $(Get-GtSettings)"

Write-Output "`nplay indila"; Invoke-Post "/api/play?song=indila" | Out-Null; Start-Sleep 3
foreach ($styleId in $stylesToTry) {
  Write-Output "style $styleId during the song: $(Invoke-Post "/api/sound?style=$styleId")"
  Start-Sleep 2
  Write-Output "  GT2560: $(Get-GtSettings)"
}
# From "smooth" (grain 1), grain 4 is exactly "Balanced"; octave +2 matches no style -> "Custom".
Write-Output "fine-tune group=4: $(Invoke-Post '/api/sound?key=group&value=4')"
Start-Sleep 1
$sound = Invoke-RestMethod "$baseUrl/api/sound"; Write-Output "  style is now: $($sound.style) (expected: balanced)"
Write-Output "fine-tune oct=2: $(Invoke-Post '/api/sound?key=oct&value=2')"
Start-Sleep 1
$sound = Invoke-RestMethod "$baseUrl/api/sound"; Write-Output "  style is now: $($sound.style) (expected: custom)"
Write-Output "  GT2560: $(Get-GtSettings)"
Write-Output "bad value group=3: $(Invoke-Post '/api/sound?key=group&value=3')"   # grain must be a power of 2
$status = Invoke-RestMethod "$baseUrl/status"
Write-Output "song still playing: mode=$($status.mode) song=$($status.song) t=$([math]::Round($status.t/1000,1))s style=$($status.style)"
Write-Output "stop: $(Invoke-Post '/api/stop')"
Write-Output "back to ${finalStyle}: $(Invoke-Post "/api/sound?style=$finalStyle")"; Start-Sleep 1
Write-Output "  GT2560: $(Get-GtSettings)"
$espSerial.Close()

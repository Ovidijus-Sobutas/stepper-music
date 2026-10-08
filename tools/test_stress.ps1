<#
.SYNOPSIS
  Web stress test: a song plays while several clients poll /status and a big song is uploaded.

.DESCRIPTION
  Pass = the ESP never loses the GT2560 link ("NOT CONNECTED" never appears), the GT2560's note
  buffer never runs empty, and the GT2560 reports no late notes.

  1. Starts the song (retrying for ~30 s, because after a restart the ESP needs a few seconds
     to join the Wi-Fi).
  2. Starts -Clients background jobs that each GET /status every 0.3 s for -Seconds seconds.
  3. After 8 s, uploads a 43 KB song during playback (unless -NoUpload).
  4. Every 2 s types "status" into the ESP's USB console and collects the log.
  5. Prints each poller's result, the lowest buffer fill, the last playback status and the
     important log lines, and saves the whole ESP log to logs\stress_esp.log.
  6. Stops the song and deletes the uploaded test song.

  The ESP is usually reached over weak Wi-Fi, so the curl timeouts are generous (5 s per poll,
  60 s for the upload). Only the ESP's USB port is opened (opening the GT2560's port would reset
  it). Close tools/monitor.ps1 and any Arduino IDE serial monitor first.

.PARAMETER Ip
  The player's address (IP or name). Default: PLAYER_HOST from .env, else <PLAYER_NAME>.local.
.PARAMETER Clients
  Number of parallel /status pollers. Default 3; 0 = none.
.PARAMETER Seconds
  Test length in seconds. Default 40.
.PARAMETER Song
  Song to play during the test. Default "Rondo alla Turca" (a demo song).
.PARAMETER EspPort
  COM port of the NodeMCU (CP2102 USB-serial). Default COM6.
.PARAMETER NoUpload
  Skip the upload during playback.
.PARAMETER SongDir
  Folder with "Eine kleine Nachtmusik.stepper" (the 43 KB upload). Default: songs\demos.

.EXAMPLE
  .\tools\test_stress.ps1 -Clients 5 -Seconds 60
#>
param(
  [string]$Ip = "",
  [int]$Clients = 3,
  [int]$Seconds = 40,
  [string]$Song = "Rondo alla Turca",
  [string]$EspPort = "COM6",
  [switch]$NoUpload,
  [string]$SongDir = ""
)
. "$PSScriptRoot\DotEnv.ps1"
$localSettings = Read-DotEnv
if (-not $Ip) { $Ip = Get-PlayerHost $localSettings }
if (-not $SongDir) { $SongDir = Join-Path $ProjectRoot "songs\demos" }

$baseUrl = "http://$Ip"
$uploadName = "Stress upload"
$uploadFile = "$env:TEMP\$uploadName.stepper"
$uploadDelaySeconds = 8   # upload after the pollers have been running for a while

$espSerial = New-Object System.IO.Ports.SerialPort $EspPort, 115200
$espSerial.DtrEnable = $false; $espSerial.RtsEnable = $false   # DTR/RTS would reset the ESP
$espSerial.Open(); $espSerial.ReadExisting() | Out-Null
$espLog = ""

# Plays the song; retries until /status reports mode "song". Returns $true if it started.
function Start-TestSong {
  for ($attempt = 0; $attempt -lt 15; $attempt++) {
    & curl.exe -s -m 5 -X POST "$baseUrl/api/play?song=$([uri]::EscapeDataString($Song))" | Out-Null
    Start-Sleep 2
    try { if ((Invoke-RestMethod "$baseUrl/status" -TimeoutSec 5).mode -eq "song") { return $true } } catch { }
  }
  return $false
}

# Starts one background job that polls /status like an open web page; it returns a summary line.
function Start-StatusPoller {
  Start-Job -ArgumentList $baseUrl, $Seconds -ScriptBlock {
    param($url, $durationSeconds)
    $ok = 0; $failed = 0; $slowestMs = 0
    $endTime = (Get-Date).AddSeconds($durationSeconds)
    while ((Get-Date) -lt $endTime) {
      $timer = [Diagnostics.Stopwatch]::StartNew()
      $code = & curl.exe -s -o NUL -m 5 -w "%{http_code}" "$url/status"
      if ($code -eq "200") { $ok++ } else { $failed++ }
      $slowestMs = [Math]::Max($slowestMs, $timer.ElapsedMilliseconds)
      Start-Sleep -Milliseconds 300
    }
    "ok=$ok failed=$failed slowest=${slowestMs}ms"
  }
}

if (-not (Start-TestSong)) { "could not start '$Song' - is the ESP reachable at $Ip?"; $espSerial.Close(); return }

$pollerJobs = if ($Clients -le 0) { @() } else { 1..$Clients | ForEach-Object { Start-StatusPoller } }

Start-Sleep $uploadDelaySeconds
if (-not $NoUpload) {
  Copy-Item (Join-Path $SongDir "Eine kleine Nachtmusik.stepper") $uploadFile -Force
  $timer = [Diagnostics.Stopwatch]::StartNew()
  $answer = & curl.exe -s -m 60 -w "|%{http_code}" -F "file=@$uploadFile" "$baseUrl/api/upload?name=Stress%20upload"
  "upload 43 KB during playback: $answer in $($timer.ElapsedMilliseconds) ms"
}

# Ask the ESP for its status every 2 s until the test time is over.
$deadline = (Get-Date).AddSeconds($Seconds - $uploadDelaySeconds)
while ((Get-Date) -lt $deadline) {
  $espLog += $espSerial.ReadExisting()
  $espSerial.Write("status`n"); Start-Sleep 2
}
$pollerResults = $pollerJobs | Wait-Job | Receive-Job
$pollerJobs | Remove-Job
$pollerResults | ForEach-Object { "poller: $_" }
$espLog += $espSerial.ReadExisting()
$espSerial.Write("stat`n"); Start-Sleep 1; $espLog += $espSerial.ReadExisting()   # final link counters
$espSerial.Close()

$logDir = Join-Path $PSScriptRoot "..\logs"; New-Item -ItemType Directory -Force $logDir | Out-Null
[IO.File]::WriteAllText((Join-Path $logDir "stress_esp.log"), $espLog)

# "GT2560 playback ... buffered=N ..." lines come from the console's "status" command.
$playbackLines = $espLog -split "`n" | Where-Object { $_ -match "GT2560 playback" }
$lowestBuffer = ($playbackLines | ForEach-Object { if ($_ -match "buffered=(\d+)") { [int]$Matches[1] } } | Measure-Object -Minimum).Minimum
$lastPlayback = ($playbackLines | Select-Object -Last 1)
"GT2560 status samples: $($playbackLines.Count), lowest buffer fill: $lowestBuffer notes"
"last playback status: $lastPlayback"
"link lost during test: " + [bool]($espLog -match "NOT CONNECTED")
($espLog -split "`n" | Where-Object { $_ -match "STAT:|ESP:|no reply|stopped|booting|\[LOOP\]|NOT CONNECTED" }) -join "`n"

# Clean up: stop the song and remove the uploaded test song.
& curl.exe -s -m 5 -X POST "$baseUrl/api/stop" | Out-Null
& curl.exe -s -m 5 -X POST "$baseUrl/api/delete?song=Stress%20upload" | Out-Null
Remove-Item $uploadFile -ErrorAction SilentlyContinue
"cleaned up"

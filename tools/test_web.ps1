<#
.SYNOPSIS
  Web control test (step B2): uses the ESP's HTTP API like the phone page does, and records both
  boards over USB.

.DESCRIPTION
  Plays "Minuet in G", switches to "Mountain King", changes the level, pauses, resumes, stops and runs a
  motor test, while polling /status once a second like the open web page. Calls that must be
  refused are marked "(must fail)". At the end it prints the poll counts, saves both boards' logs
  to logs\test_web_esp.log and logs\test_web_gt.log, and prints the important lines of each.

  Opening the GT2560's port (COM5) resets it, so start this script while nothing is playing.
  Close tools/monitor.ps1 and any Arduino IDE serial monitor first (they hold the COM ports).
  The ESP is reached over Wi-Fi, which can be weak, so requests time out after 3-5 s and a failed
  poll is counted rather than stopping the test.

.PARAMETER Ip
  The player's address (IP or name). Default: PLAYER_HOST from .env, else <PLAYER_NAME>.local.
.PARAMETER GtPort
  COM port of the GT2560 (FTDI USB-serial). Default COM5.
.PARAMETER EspPort
  COM port of the NodeMCU (CP2102 USB-serial). Default COM6.

.EXAMPLE
  .\tools\test_web.ps1 -Ip 192.168.1.50
#>
param([string]$Ip = "", [string]$GtPort = "COM5", [string]$EspPort = "COM6")
. "$PSScriptRoot\DotEnv.ps1"
if (-not $Ip) { $Ip = Get-PlayerHost (Read-DotEnv) }

$BAUD = 115200   # both boards' USB debug/console speed
$gtSerial = New-Object System.IO.Ports.SerialPort $GtPort, $BAUD
$espSerial = New-Object System.IO.Ports.SerialPort $EspPort, $BAUD
$gtSerial.DtrEnable = $false; $espSerial.DtrEnable = $false; $espSerial.RtsEnable = $false
$gtSerial.Open(); $espSerial.Open()
$script:gtLog = ""; $script:espLog = ""; $script:pollsOk = 0; $script:pollsFailed = 0

function Read-BothBoards {
  $script:gtLog += $gtSerial.ReadExisting()
  $script:espLog += $espSerial.ReadExisting()
}

# Calls an API path; returns "HTTP <code> '<body>'" or "FAILED: <reason>".
function Invoke-Api($path, $method = "POST") {
  try {
    $response = Invoke-WebRequest -Uri "http://$Ip$path" -Method $method -TimeoutSec 5 -UseBasicParsing
    return "HTTP $($response.StatusCode) '$($response.Content)'"
  } catch { return "FAILED: $($_.Exception.Message)" }
}

# Polls /status once a second for $seconds (like the web page), then prints the last status.
function Wait-Polling([double]$seconds, [string]$label) {
  $timer = [Diagnostics.Stopwatch]::StartNew(); $lastStatus = $null
  while ($timer.Elapsed.TotalSeconds -lt $seconds) {
    try { $lastStatus = Invoke-RestMethod -Uri "http://$Ip/status" -TimeoutSec 3; $script:pollsOk++ } catch { $script:pollsFailed++ }
    Read-BothBoards
    Start-Sleep -Milliseconds 1000
  }
  if ($lastStatus) {
    Write-Output ("  [{0}] mode={1} song={2} paused={3} t={4:N1}s/{5:N1}s level={6} gt={7} heap={8} block={9} up={10}s" -f
      $label, $lastStatus.mode, $lastStatus.song, $lastStatus.paused, ($lastStatus.t/1000), ($lastStatus.len/1000),
      $lastStatus.level, $lastStatus.gt, $lastStatus.heap, $lastStatus.block, $lastStatus.up)
  }
}

Write-Output "songs: $((Invoke-RestMethod -Uri "http://$Ip/api/songs").songs | ForEach-Object { "$($_.name) $([math]::Round($_.len/1000))s" })"
Write-Output "play Minuet in G: $(Invoke-Api '/api/play?song=Minuet%20in%20G')";            Wait-Polling 6 "minuet"
Write-Output "play Mountain King (switch): $(Invoke-Api '/api/play?song=Mountain%20King')"; Wait-Polling 8 "switched"
Write-Output "level 7: $(Invoke-Api '/api/level?value=7')";                  Wait-Polling 4 "level"
Write-Output "pause: $(Invoke-Api '/api/pause')";                            Wait-Polling 4 "paused"
Write-Output "resume: $(Invoke-Api '/api/resume')";                          Wait-Polling 30 "resumed"
Write-Output "level 99 (must fail): $(Invoke-Api '/api/level?value=99')"
Write-Output "test 3 while playing (stops the song, then tests): $(Invoke-Api '/api/test?motor=3')"
Write-Output "stop: $(Invoke-Api '/api/stop')";                              Wait-Polling 3 "stopped"
Write-Output "test 3: $(Invoke-Api '/api/test?motor=3')";                    Wait-Polling 3 "test"
Write-Output "level 5: $(Invoke-Api '/api/level?value=5')"
Write-Output "status polls: ok=$($script:pollsOk) failed=$($script:pollsFailed)"

$espSerial.Write("stat`n"); Start-Sleep -Milliseconds 800   # final link counters
Read-BothBoards
$logDir = Join-Path $PSScriptRoot "..\logs"; New-Item -ItemType Directory -Force $logDir | Out-Null
[IO.File]::WriteAllText((Join-Path $logDir "test_web_esp.log"), $script:espLog)
[IO.File]::WriteAllText((Join-Path $logDir "test_web_gt.log"), $script:gtLog)
Write-Output "`n--- ESP log ---"
Write-Output ($script:espLog -split "`n" | Where-Object { $_ -match "PLAYER|STAT|ESP:|refused|no reply|WIFI|TEST" } | ForEach-Object { $_.TrimEnd() })
Write-Output "`n--- GT2560 log ---"
Write-Output ($script:gtLog -split "`n" | Where-Object { $_ -match "PLAY|TEST|SAFETY|timeout" } | ForEach-Object { $_.TrimEnd() })
$gtSerial.Close(); $espSerial.Close()

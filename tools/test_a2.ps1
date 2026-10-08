<#
.SYNOPSIS
  Step A2 test of the ESP <-> GT2560 link protocol (frames, CRC, retries, heartbeat, safety timeout).

.DESCRIPTION
  Drives the ESP's test console over USB and prints what both boards logged in each step:
    1. startup        both boards restarted, startup reports
    2. N pings        "ping N": the ESP sends N PING frames; must end with "result: PASS"
    3. corruption     "corrupt": damaged frames must be rejected and counted by the GT2560
    4. heartbeat off  "hb off": after 6 s without frames the GT2560 must hit its link timeout,
                      release the motors and go IDLE
    5. heartbeat on   "hb on": the link must recover by itself
    6. final status   "stat": link counters of both sides
  Section 5.3 of docs/PLAN.md describes the protocol and the expected results.

  Close tools/monitor.ps1 and any Arduino IDE serial monitor first (they hold the COM ports).

.PARAMETER GtPort
  COM port of the GT2560 (FTDI USB-serial). Default COM5.
.PARAMETER EspPort
  COM port of the NodeMCU (CP2102 USB-serial). Default COM6.
.PARAMETER Pings
  Number of PING frames in step 2. Default 1000.

.EXAMPLE
  .\tools\test_a2.ps1 -Pings 5000
#>
param([string]$GtPort = "COM5", [string]$EspPort = "COM6", [int]$Pings = 1000)

$BAUD = 115200   # both boards' USB debug/console speed

# Opening the GT2560's port resets it (FTDI DTR auto-reset), which this test wants: step 1 shows
# its startup. DTR/RTS are then kept low so neither board is held in reset.
$gtSerial = New-Object System.IO.Ports.SerialPort $GtPort, $BAUD
$espSerial = New-Object System.IO.Ports.SerialPort $EspPort, $BAUD
$gtSerial.DtrEnable = $false; $espSerial.DtrEnable = $false; $espSerial.RtsEnable = $false
$gtSerial.Open(); $espSerial.Open()

# Everything received so far from each board; each step prints only its own part.
$script:gtLog = ""; $script:espLog = ""

function Read-BothBoards {
  $script:gtLog += $gtSerial.ReadExisting()
  $script:espLog += $espSerial.ReadExisting()
}

# Collects output for up to $seconds, or until the ESP's new output matches $stopPattern.
function Wait-Output([double]$seconds, [string]$stopPattern = "") {
  $timer = [Diagnostics.Stopwatch]::StartNew()
  $espStart = $script:espLog.Length
  while ($timer.Elapsed.TotalSeconds -lt $seconds) {
    Read-BothBoards
    if ($stopPattern -and $script:espLog.Substring($espStart) -match $stopPattern) {
      Start-Sleep -Milliseconds 300; Read-BothBoards   # pick up the lines that follow the match
      break
    }
    Start-Sleep -Milliseconds 50
  }
}

# Sends $command to the ESP console (none if $null), waits, and prints both boards' output.
function Invoke-TestStep($title, $command, $seconds, $stopPattern = "") {
  $gtStart = $script:gtLog.Length; $espStart = $script:espLog.Length
  if ($command) { $espSerial.Write("$command`n") }
  Wait-Output $seconds $stopPattern
  Write-Output "`n########## $title ##########"
  Write-Output "--- ESP ---"; Write-Output $script:espLog.Substring($espStart).Trim()
  Write-Output "--- GT2560 ---"; Write-Output $script:gtLog.Substring($gtStart).Trim()
}

# Restart the ESP too (on the NodeMCU, RTS pulls the ESP's EN pin low) so its startup report is
# captured. The GT2560 was already reset by opening its port.
$espSerial.RtsEnable = $true; Start-Sleep -Milliseconds 100; $espSerial.RtsEnable = $false

Invoke-TestStep "1. startup (both boards restarted)" $null 4
Invoke-TestStep "2. $Pings pings" "ping $Pings" ($Pings / 50 + 30) "result: (PASS|FAIL)"   # ~50 pings/s, plus margin
Invoke-TestStep "3. corruption test" "corrupt" 3
# The GT2560's link timeout is LINK_TIMEOUT_MS = 6000 (gt2560_engine/src/config.h; was 2 s in the
# first A2 version), so this step waits 8 s to see the timeout and the motors being released.
Invoke-TestStep "4. heartbeat off (GT2560 must time out after 6 s)" "hb off" 8
Invoke-TestStep "5. heartbeat on (link must recover by itself)" "hb on" 2
Invoke-TestStep "6. final status" "stat" 1.5

$gtSerial.Close(); $espSerial.Close()

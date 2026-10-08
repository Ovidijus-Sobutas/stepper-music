<#
.SYNOPSIS
  Sends commands to the ESP8266 test console over USB and prints what both boards logged.

.DESCRIPTION
  Opens both USB serial ports, waits for the boards to settle, then runs each step in turn:
  the command is typed into the ESP's console (115200 baud) and both boards' output is collected
  until the step's time is up or the ESP prints a line matching the step's stop pattern.
  Type "help" in the console (as a step) to list the ESP's commands.

  Close tools/monitor.ps1 and any Arduino IDE serial monitor first: a COM port can be open in
  only one program at a time.

.PARAMETER Steps
  One or more steps, each "command|seconds[|stop-regex]":
    command     a line for the ESP console, e.g. "play harry", "test all", "stat"
    seconds     how long to collect output at most
    stop-regex  optional; stop waiting as soon as the ESP's output matches it
                (the rest after the second "|" is the regex, so it may itself contain "|")

.PARAMETER GtPort
  COM port of the GT2560 (FTDI USB-serial). Default COM5.

.PARAMETER EspPort
  COM port of the NodeMCU (CP2102 USB-serial). Default COM6.

.PARAMETER ResetEsp
  Restart the ESP first (pulses RTS, which pulls the ESP's EN pin low), so its startup report
  is captured too.

.EXAMPLE
  .\tools\console.ps1 -Steps "test all|8|motor test done", "play harry|45|finished|stopped"
#>
param(
  [string[]]$Steps,
  [string]$GtPort = "COM5",
  [string]$EspPort = "COM6",
  [switch]$ResetEsp
)

$BAUD = 115200   # both boards' USB debug/console speed

# DTR/RTS are kept low so opening the ports does not hold either board in reset.
# Note: the GT2560 (FTDI + Arduino bootloader) may still reset once when its port opens.
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

function Restart-Esp {
  # On the NodeMCU, RTS drives the ESP's EN (chip enable) pin: a short pulse restarts it.
  $espSerial.RtsEnable = $true; Start-Sleep -Milliseconds 100; $espSerial.RtsEnable = $false
}

# Runs one console command and collects output for up to $seconds, or until $stopPattern matches.
function Invoke-Step([string]$command, [double]$seconds, [string]$stopPattern) {
  $gtStart = $script:gtLog.Length; $espStart = $script:espLog.Length
  $espSerial.Write("$command`n")
  $timer = [Diagnostics.Stopwatch]::StartNew()
  while ($timer.Elapsed.TotalSeconds -lt $seconds) {
    Read-BothBoards
    if ($stopPattern -and $script:espLog.Substring($espStart) -match $stopPattern) {
      Start-Sleep -Milliseconds 500; Read-BothBoards   # pick up the lines that follow the match
      break
    }
    Start-Sleep -Milliseconds 50
  }
  Write-Output "`n########## $command  ($([math]::Round($timer.Elapsed.TotalSeconds,1)) s) ##########"
  Write-Output "--- ESP ---"; Write-Output $script:espLog.Substring($espStart).Trim()
  Write-Output "--- GT2560 ---"; Write-Output $script:gtLog.Substring($gtStart).Trim()
}

if ($ResetEsp) { Restart-Esp }
Start-Sleep -Seconds 3   # opening COM5 may reset the GT2560; let both boards settle
Read-BothBoards
Write-Output "--- startup: ESP ---"; Write-Output $script:espLog.Trim()
Write-Output "--- startup: GT2560 ---"; Write-Output $script:gtLog.Trim()

foreach ($step in $Steps) {
  $parts = $step -split '\|', 3
  $stopPattern = if ($parts.Count -gt 2) { $parts[2] } else { "" }
  Invoke-Step $parts[0] ([double]$parts[1]) $stopPattern
}
$gtSerial.Close(); $espSerial.Close()

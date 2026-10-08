<#
.SYNOPSIS
  Live serial monitor for both boards at once, with a log file per board.

.DESCRIPTION
  Prints every line from the GT2560 (yellow) and the ESP8266 (cyan) with a timestamp, and appends
  it to logs\gt2560.log and logs\esp8266.log (created next to the tools folder).
  A board whose port cannot be opened is skipped with a red message; the other is still shown.

  Stop it with Ctrl+C before uploading firmware or running any tools\test_*.ps1 script: while it
  runs it holds both COM ports, and a port can be open in only one program at a time (the same
  goes for the Arduino IDE serial monitor).

.PARAMETER GtPort
  COM port of the GT2560 (FTDI USB-serial). Default COM5.
.PARAMETER GtBaud
  GT2560 USB baud rate. Default 115200 (USB_BAUD in gt2560_engine/src/config.h).
.PARAMETER EspPort
  COM port of the NodeMCU (CP2102 USB-serial). Default COM6.
.PARAMETER EspBaud
  ESP8266 USB baud rate. Default 115200 (USB_BAUD in esp8266_player/src/config.h).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\monitor.ps1
.EXAMPLE
  .\tools\monitor.ps1 -GtPort COM3 -EspPort COM4
#>
param(
  [string]$GtPort = "COM5", [int]$GtBaud = 115200,
  [string]$EspPort = "COM6", [int]$EspBaud = 115200
)

$logDir = Join-Path $PSScriptRoot "..\logs"
New-Item -ItemType Directory -Force $logDir | Out-Null
$gtLogFile = Join-Path $logDir "gt2560.log"
$espLogFile = Join-Path $logDir "esp8266.log"

# Opens a port without asserting DTR/RTS (they are wired to the boards' reset/boot pins).
# Returns $null if the port is missing or busy (e.g. held by the Arduino IDE).
function Open-SerialPort($portName, $baudRate) {
  $serial = New-Object System.IO.Ports.SerialPort $portName, $baudRate
  $serial.DtrEnable = $false; $serial.RtsEnable = $false
  try { $serial.Open(); return $serial }
  catch { Write-Host "Cannot open $portName ($($_.Exception.Message))" -ForegroundColor Red; return $null }
}

# Adds newly received text to the board's line buffer, then prints and logs every complete line.
# $lineBuffer is passed by reference because a partial line must survive until the next call.
function Write-CompleteLines([ref]$lineBuffer, $newText, $tag, $color, $logFile) {
  $lineBuffer.Value += $newText
  while (($newlineAt = $lineBuffer.Value.IndexOf("`n")) -ge 0) {
    $line = $lineBuffer.Value.Substring(0, $newlineAt).TrimEnd("`r")
    $lineBuffer.Value = $lineBuffer.Value.Substring($newlineAt + 1)
    $stamp = (Get-Date).ToString("HH:mm:ss.fff")
    Write-Host "$stamp $tag $line" -ForegroundColor $color
    Add-Content -Path $logFile -Value "$stamp $line"
  }
}

$gtSerial = Open-SerialPort $GtPort $GtBaud
$espSerial = Open-SerialPort $EspPort $EspBaud
Write-Host "Monitoring GT2560 on $GtPort ($GtBaud) [yellow] and ESP8266 on $EspPort ($EspBaud) [cyan]. Ctrl+C to stop."
Write-Host "Logs: $gtLogFile , $espLogFile"
$gtPartialLine = ""; $espPartialLine = ""
try {
  while ($true) {
    if ($gtSerial) { Write-CompleteLines ([ref]$gtPartialLine) $gtSerial.ReadExisting() "[GT2560]" Yellow $gtLogFile }
    if ($espSerial) { Write-CompleteLines ([ref]$espPartialLine) $espSerial.ReadExisting() "[ESP]   " Cyan $espLogFile }
    Start-Sleep -Milliseconds 50
  }
} finally {
  # Runs on Ctrl+C too, so the ports are always released for the next upload.
  if ($gtSerial) { $gtSerial.Close() }; if ($espSerial) { $espSerial.Close() }
  Write-Host "Ports released."
}

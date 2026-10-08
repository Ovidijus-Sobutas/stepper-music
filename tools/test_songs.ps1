<#
.SYNOPSIS
  Song library test (step G1) over Wi-Fi, using the ESP's HTTP API.

.DESCRIPTION
  Runs these checks against the player and prints each answer as "<body>|<HTTP code>":
    - upload a song, an invalid file (must fail), and a bad name "../evil" (saved as "evil")
    - play the uploaded song, then upload a 43 KB song WHILE it plays (the music must keep going)
    - delete the playing song (must fail), stop, rename, rename onto an existing name (must fail)
    - delete the test songs and the demo song "Minuet in G", then "Restore demo songs" (/api/demos)
    - show the storage use
  The test files are copied into %TEMP%\musicplayer-test and deleted at the end.
  Uploads use curl.exe (part of Windows 10/11) because it does multipart uploads easily.

  Needs no USB connection, only the player on the same network as this PC.

.PARAMETER Ip
  The player's address (IP or name; the IP is shown on the OLED and the Settings card).
  Default: PLAYER_HOST from .env, else <PLAYER_NAME>.local.
.PARAMETER SongDir
  Folder with "Mountain King.stepper", "Eine kleine Nachtmusik.stepper" and "Fur Elise.mid".
  Default: the demo songs in songs\demos.

.EXAMPLE
  .\tools\test_songs.ps1 -Ip 192.168.1.50
#>
param(
  [string]$Ip = "",
  [string]$SongDir = ""
)
. "$PSScriptRoot\DotEnv.ps1"
$localSettings = Read-DotEnv
if (-not $Ip) { $Ip = Get-PlayerHost $localSettings }
if (-not $SongDir) { $SongDir = Join-Path $ProjectRoot "songs\demos" }

$baseUrl = "http://$Ip"
$tempDir = Join-Path $env:TEMP "musicplayer-test"
New-Item -ItemType Directory -Force $tempDir | Out-Null
Copy-Item (Join-Path $SongDir "Mountain King.stepper") "$tempDir\Test Song.stepper" -Force        # 11 KB
Copy-Item (Join-Path $SongDir "Eine kleine Nachtmusik.stepper") "$tempDir\Big One.stepper" -Force  # 43 KB
Copy-Item (Join-Path $SongDir "Fur Elise.mid") "$tempDir\not-a-stepper.stepper" -Force   # wrong format on purpose

# Song list as "name (length s)" strings.
function Get-SongList {
  (Invoke-RestMethod "$baseUrl/api/songs" -TimeoutSec 8).songs |
    ForEach-Object { "$($_.name) ($([math]::Round($_.len/1000))s)" }
}

# POSTs to an API path; returns "<body>|<HTTP code>".
function Invoke-Post($path) {
  return & curl.exe -s -o - -w "|%{http_code}" -X POST "$baseUrl$path"
}

# Uploads a file under a song name; returns "<body>|<HTTP code> in <ms> ms".
function Send-Song($file, $songName) {
  $timer = [Diagnostics.Stopwatch]::StartNew()
  $answer = & curl.exe -s -o - -w "|%{http_code}" -F "file=@$file" "$baseUrl/api/upload?name=$([uri]::EscapeDataString($songName))"
  return "$answer in $($timer.ElapsedMilliseconds) ms"
}

# One line with the player's mode, song and song time.
function Get-StatusLine([switch]$WithLink) {
  $status = Invoke-RestMethod "$baseUrl/status"
  $line = "mode=$($status.mode) song=$($status.song) t=$([math]::Round($status.t/1000,1))s"
  if ($WithLink) { $line += " gt=$($status.gt)" }
  return $line
}

Write-Output "songs at start: $((Get-SongList) -join ', ')"
Write-Output "upload 'Test Song' (11 KB): $(Send-Song "$tempDir\Test Song.stepper" 'Test Song')"
Write-Output "upload invalid file: $(Send-Song "$tempDir\not-a-stepper.stepper" 'not-a-stepper')"
Write-Output "upload bad name '../evil' (path removed, saved as 'evil'): $(Send-Song "$tempDir\Test Song.stepper" '../evil')"
Write-Output "songs now: $((Get-SongList) -join ', ')"
Write-Output "play 'Test Song': $(Invoke-Post '/api/play?song=Test%20Song')"
Start-Sleep 4
Write-Output "  status: $(Get-StatusLine)"
Write-Output "upload 'Big One' (43 KB) WHILE PLAYING: $(Send-Song "$tempDir\Big One.stepper" 'Big One')"
Write-Output "  status after upload: $(Get-StatusLine -WithLink)"
Write-Output "delete playing song (must fail): $(Invoke-Post '/api/delete?song=Test%20Song')"
Write-Output "stop: $(Invoke-Post '/api/stop')"; Start-Sleep 1
Write-Output "rename 'Test Song' -> 'Renamed': $(Invoke-Post '/api/rename?song=Test%20Song&to=Renamed')"
Write-Output "rename to existing 'Ode to Joy' (must fail): $(Invoke-Post '/api/rename?song=Renamed&to=Ode%20to%20Joy')"
Write-Output "play 'Renamed': $(Invoke-Post '/api/play?song=Renamed')"; Start-Sleep 3
Write-Output "  status: $(Get-StatusLine)"
Write-Output "stop: $(Invoke-Post '/api/stop')"; Start-Sleep 1
Write-Output "delete 'Renamed': $(Invoke-Post '/api/delete?song=Renamed')"
Write-Output "delete 'Big One': $(Invoke-Post '/api/delete?song=Big%20One')"
Write-Output "delete 'evil': $(Invoke-Post '/api/delete?song=evil')"
Write-Output "delete demo 'Minuet in G': $(Invoke-Post '/api/delete?song=Minuet%20in%20G')"
Write-Output "songs now: $((Get-SongList) -join ', ')"
Write-Output "restore demos: $(Invoke-Post '/api/demos')"
Start-Sleep 4   # the player answers at once and writes the demo songs right after
Write-Output "songs at end: $((Get-SongList) -join ', ')"
$library = Invoke-RestMethod "$baseUrl/api/songs"
Write-Output "storage: $([math]::Round($library.used/1024)) of $([math]::Round($library.total/1024)) KB"
Remove-Item -Recurse -Force $tempDir

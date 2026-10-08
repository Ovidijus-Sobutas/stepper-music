# Reads the project's .env file (see .env.example) for the other tools.
# Usage, from a script in tools\:
#   . "$PSScriptRoot\DotEnv.ps1"
#   $settings = Read-DotEnv            # hashtable; missing keys are simply absent
#   $player = Get-PlayerHost $settings # "musicplayer.local" unless PLAYER_HOST is set

$ProjectRoot = (Resolve-Path "$PSScriptRoot\..").Path

# Lines are KEY=value; "#" starts a comment line; empty values count as not set.
function Read-DotEnv([string]$Path = (Join-Path $ProjectRoot ".env")) {
  $settings = @{}
  if (-not (Test-Path $Path)) { return $settings }
  foreach ($line in Get-Content $Path) {
    $line = $line.Trim()
    if (-not $line -or $line.StartsWith("#") -or -not $line.Contains("=")) { continue }
    $key, $value = $line.Split("=", 2)
    $value = $value.Trim().Trim('"')
    if ($value) { $settings[$key.Trim()] = $value }
  }
  return $settings
}

function Get-PlayerHost($settings) {
  if ($settings.PLAYER_HOST) { return $settings.PLAYER_HOST }
  $name = if ($settings.PLAYER_NAME) { $settings.PLAYER_NAME } else { "musicplayer" }
  return "$name.local"
}

function Get-ReferenceSongDir($settings) {
  if ($settings.REFERENCE_SONG_DIR) { return $settings.REFERENCE_SONG_DIR }
  return (Join-Path $ProjectRoot "songs\demos")
}

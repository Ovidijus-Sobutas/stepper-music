# Tiny local web server for testing pages in a browser (no installs needed).
#   /         -> this project folder
#   /ref/     -> REFERENCE_SONG_DIR from .env (MIDI files + .stepper files made by the Python converter)
# Usage: powershell -ExecutionPolicy Bypass -File tools\serve.ps1 [-Port 8123]
param([int]$Port = 8123)
. "$PSScriptRoot\DotEnv.ps1"

$roots = @{ "/ref/" = (Get-ReferenceSongDir (Read-DotEnv)); "/" = $ProjectRoot }
$types = @{ ".html" = "text/html; charset=utf-8"; ".js" = "text/javascript; charset=utf-8"; ".css" = "text/css";
            ".json" = "application/json"; ".mid" = "audio/midi"; ".stepper" = "application/octet-stream" }
$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add("http://localhost:$Port/")
$listener.Start()
Write-Host "Serving http://localhost:$Port/  (Ctrl+C to stop)"
try {
  while ($listener.IsListening) {
    $ctx = $listener.GetContext()
    $path = [Uri]::UnescapeDataString($ctx.Request.Url.AbsolutePath)
    $prefix = if ($path.StartsWith("/ref/")) { "/ref/" } else { "/" }
    $rel = $path.Substring($prefix.Length) -replace '/', '\'
    $file = Join-Path $roots[$prefix] $rel
    # Files of the player page asked for at the top level (/app.js, /style.css, ...) come from the web folder.
    $webFile = Join-Path $roots["/"] ("esp8266_player\web\" + $rel)
    if ($prefix -eq "/" -and -not (Test-Path $file -PathType Leaf) -and (Test-Path $webFile -PathType Leaf)) { $file = $webFile }
    $full = [IO.Path]::GetFullPath($file)
    $res = $ctx.Response
    # Allow the player's own page (another address) to fetch test files from here.
    $res.Headers.Add("Access-Control-Allow-Origin", "*")
    $res.Headers.Add("Access-Control-Allow-Private-Network", "true")
    if ($ctx.Request.HttpMethod -eq "OPTIONS") { $res.StatusCode = 204; $res.Close(); continue }
    if ($full.StartsWith($roots[$prefix]) -and (Test-Path $full -PathType Leaf)) {
      $bytes = [IO.File]::ReadAllBytes($full)
      $ext = [IO.Path]::GetExtension($full).ToLower()
      $res.ContentType = if ($types.ContainsKey($ext)) { $types[$ext] } else { "application/octet-stream" }
      $res.Headers.Add("Cache-Control", "no-store")
      $res.OutputStream.Write($bytes, 0, $bytes.Length)
    } else {
      $res.StatusCode = 404
    }
    $res.Close()
  }
} finally { $listener.Stop() }

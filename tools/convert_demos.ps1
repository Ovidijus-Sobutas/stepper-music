<#
.SYNOPSIS
  Converts every MIDI file in songs\demos\ to a .stepper file next to it, with the web page's own
  converter (esp8266_player\web\converter.js), so the demos match what an upload would give.

.DESCRIPTION
  Starts a small web server on http://localhost:8124 and opens tools\convert_demos.html in your
  browser. The page converts each songs\demos\*.mid with the Convert card's default settings
  (all instruments except drums, "Spread" motor assignment, no transpose, speed 100 %) and sends
  the result back here, where it is saved as songs\demos\<name>.stepper. Press Ctrl+C when the
  page says "done".
  Afterwards run tools\build_demos.ps1 to build the demos into the ESP8266 firmware.

.PARAMETER NoBrowser
  Only start the server; open the page yourself.
.PARAMETER Port
  Local port for the server (default 8124).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\convert_demos.ps1
#>
param([int]$Port = 8124, [switch]$NoBrowser)
. "$PSScriptRoot\DotEnv.ps1"

$demoDir = Join-Path $ProjectRoot "songs\demos"
$contentTypes = @{ ".html" = "text/html; charset=utf-8"; ".js" = "text/javascript; charset=utf-8";
                   ".json" = "application/json"; ".mid" = "audio/midi" }

function Send-Response($context, [int]$status, [byte[]]$body, [string]$type = "text/plain; charset=utf-8") {
  $context.Response.StatusCode = $status
  $context.Response.ContentType = $type
  $context.Response.OutputStream.Write($body, 0, $body.Length)
  $context.Response.Close()
}
function Send-Text($context, [int]$status, [string]$text) {
  Send-Response $context $status ([Text.Encoding]::UTF8.GetBytes($text))
}

# GET /demo-midis -> JSON list of the MIDI file names in songs\demos (without ".mid").
function Send-MidiList($context) {
  $names = @(Get-ChildItem $demoDir -Filter *.mid | ForEach-Object { $_.BaseName })
  Send-Response $context 200 ([Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject $names))) "application/json"
}

# POST /save-stepper?name=<song> with the .stepper bytes as the body -> songs\demos\<song>.stepper
function Save-Stepper($context) {
  $name = $context.Request.QueryString["name"]
  if (-not $name -or $name -notmatch '^[A-Za-z0-9 _().-]{1,23}$') { Send-Text $context 400 "bad song name"; return }
  $memory = New-Object IO.MemoryStream
  $context.Request.InputStream.CopyTo($memory)
  [IO.File]::WriteAllBytes((Join-Path $demoDir "$name.stepper"), $memory.ToArray())
  Write-Host ("saved {0}.stepper ({1} bytes)" -f $name, $memory.Length)
  Send-Text $context 200 "saved"
}

# Any other GET: a file from the project folder (the page, converter.js, the MIDI files).
function Send-ProjectFile($context, [string]$path) {
  $file = Join-Path $ProjectRoot ($path.TrimStart('/') -replace '/', '\')
  $resolved = [IO.Path]::GetFullPath($file)
  if (-not $resolved.StartsWith($ProjectRoot) -or -not (Test-Path $resolved -PathType Leaf)) {
    Send-Text $context 404 "not found"; return
  }
  $type = $contentTypes[[IO.Path]::GetExtension($resolved).ToLower()]
  if (-not $type) { $type = "application/octet-stream" }
  Send-Response $context 200 ([IO.File]::ReadAllBytes($resolved)) $type
}

$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add("http://localhost:$Port/")
$listener.Start()
$pageUrl = "http://localhost:$Port/tools/convert_demos.html"
Write-Host "Converting the demos: open $pageUrl  (Ctrl+C to stop when it says done)"
if (-not $NoBrowser) { Start-Process $pageUrl }
try {
  while ($listener.IsListening) {
    $context = $listener.GetContext()
    $path = [Uri]::UnescapeDataString($context.Request.Url.AbsolutePath)
    try {
      if ($path -eq "/demo-midis") { Send-MidiList $context }
      elseif ($path -eq "/save-stepper" -and $context.Request.HttpMethod -eq "POST") { Save-Stepper $context }
      else { Send-ProjectFile $context $path }
    } catch { Write-Host "error on $path : $_"; try { $context.Response.Abort() } catch { } }
  }
} finally { $listener.Stop() }

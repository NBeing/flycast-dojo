<#
  T5: text-codec round-trip proof, headless.

  Launches a replay with dojo:TextRoundTrip=yes - Replay::Init exports the freshly loaded movie to
  <clip>\movie.tas.txt, re-imports it, and byte-compares against session_inputs, logging a
  "TAS TEXT" verdict. This script asserts the verdict and shows the file's shape.

    .\tastext.ps1                     # magnetoNew
    .\tastext.ps1 -Clip hayato
#>
param([string]$Clip = 'magnetoNew', [string]$Game = 'NoBGM_VMU', [int]$WaitSec = 12)
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
if (Get-Process flycast -EA SilentlyContinue) { throw "flycast is running - close it first." }

$clipDir = Join-Path $repo "build\data\replays\$Game\$Clip"
$flyr = Get-ChildItem $clipDir -Filter *.flyr | Select-Object -First 1
if (-not $flyr) { throw "no .flyr in $clipDir" }

$log = Join-Path $repo 'build\flycast.log'
Remove-Item $log -EA SilentlyContinue
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
$rom = Join-Path $repo 'NoBGM_VMU.cdi'
$p = Start-Process -FilePath (Join-Path $repo 'build\flycast.exe') -WorkingDirectory (Join-Path $repo 'build') -PassThru -ArgumentList @(
  '-config','log:LogToFile=yes','-config','dojo:NativeConsole=no','-config','dojo:UiIni=no',
  '-config','dojo:TextRoundTrip=yes',
  '-config','dojo:Replay=yes','-config',"dojo:ReplayFilename=$($flyr.FullName)",
  '-config','dojo:Training=yes','-config','dojo:StartupPrompt=no',$rom)
Start-Sleep -Seconds $WaitSec
Stop-Process -Id $p.Id -Force -EA SilentlyContinue
Start-Sleep -Seconds 1

$text = Get-Content $log -Raw
$txtFile = Join-Path $clipDir 'movie.tas.txt'
$verdict = [regex]::Match($text, 'TAS TEXT: [^\r\n]*')

Write-Host ''
Write-Host '=============== text round-trip (T5) ==============='
Write-Host ("  verdict : {0}" -f $(if ($verdict.Success) { $verdict.Value } else { '(no TAS TEXT line - hook did not run)' }))
if (Test-Path $txtFile) {
  $lines = Get-Content $txtFile
  $frameLines = @($lines | Where-Object { $_ -like '|*' }).Count
  $rawLines = @($lines | Where-Object { $_ -like '@raw*' }).Count
  Write-Host ("  file    : {0} lines total, {1} frame lines, {2} @raw lines" -f $lines.Count, $frameLines, $rawLines)
  Write-Host '  head    :'
  $lines | Select-Object -First 6 | ForEach-Object { Write-Host "    $_" }
  Write-Host '  busiest :'
  $lines | Where-Object { $_ -like '|*' -and $_ -notmatch '^\|\.+\|' } | Select-Object -First 3 | ForEach-Object { Write-Host "    $_" }
}
if ($verdict.Success -and $verdict.Value -match 'round-trip OK') {
  Write-Host 'PASS - text round-trips byte-identically' -ForegroundColor Green
} else {
  Write-Host 'FAIL' -ForegroundColor Red; exit 1
}

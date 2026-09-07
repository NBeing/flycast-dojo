<#
  Tail the Flycast Dojo log live. Run this in whatever terminal you like (Windows Terminal, a second
  tab, etc.) alongside .\run.ps1. It just follows build\flycast.log and can't touch the emulator.

  Git Bash equivalent, if you prefer:  tail -f build/flycast.log
#>
param([int]$Tail = 40)

$log = Join-Path $PSScriptRoot 'build\flycast.log'
if (-not (Test-Path $log)) { New-Item -ItemType File -Path $log -Force | Out-Null }

Write-Host "Watching $log  (Ctrl+C to stop)" -ForegroundColor Cyan
Get-Content -LiteralPath $log -Wait -Tail $Tail

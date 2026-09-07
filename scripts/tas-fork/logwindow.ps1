<#
  Open a Git Bash window that tails build\flycast.log live (tail -F). Shared by run.ps1 and by the
  VS Code F5 preLaunchTask, so the log console looks the same however you launch.

  Lifecycle: the window is tied to the emulator - it waits (up to 60s) for flycast.exe to start,
  then closes itself when flycast exits, so sessions don't leave a trail of dead consoles.
  Single-instance: if a tail window is already watching the log, no second one is opened.
#>
$log = Join-Path $PSScriptRoot 'build\flycast.log'
if (-not (Test-Path $log)) { New-Item -ItemType File -Path $log -Force | Out-Null }

# Reap strays first. The tail window is meant to die with the emulator, but an abnormal exit (a
# force-kill, a crash) leaves it behind - and the single-instance check below would then see it
# and refuse to open a working one, leaving you staring at a dead window all session. This script
# runs BEFORE flycast launches, so any tail alive right now with no emulator running is by
# definition left over from a previous session.
if (-not (Get-Process flycast -ErrorAction SilentlyContinue)) {
  Get-CimInstance Win32_Process -Filter "Name='tail.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -match 'flycast\.log' } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}

# Already got a live tail window on this log? Reuse it.
$existing = Get-CimInstance Win32_Process -Filter "Name='tail.exe'" -ErrorAction SilentlyContinue |
            Where-Object { $_.CommandLine -match 'flycast\.log' }
if ($existing) { return }

# Console geometry, set in Settings > TAS > UI (dojo:ConsoleCols / ConsoleRows). Read straight
# from emu.cfg so the emulator and this script cannot disagree about it.
$cols = 110; $rows = 30
$cfg = Join-Path $PSScriptRoot 'build\emu.cfg'
if (Test-Path $cfg) {
  $inDojo = $false
  foreach ($line in Get-Content $cfg) {
    if ($line -match '^\[(.+)\]') { $inDojo = ($Matches[1] -eq 'dojo'); continue }
    if ($inDojo -and $line -match '^\s*ConsoleCols\s*=\s*(\d+)') { $cols = [int]$Matches[1] }
    if ($inDojo -and $line -match '^\s*ConsoleRows\s*=\s*(\d+)') { $rows = [int]$Matches[1] }
  }
}

$gitBash = @(
  'C:\Program Files\Git\bin\bash.exe',
  'C:\Program Files (x86)\Git\bin\bash.exe',
  "$env:LOCALAPPDATA\Programs\Git\bin\bash.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if ($gitBash) {
  # C:\a\b -> /c/a/b  (the path has no spaces, so no inner quoting needed).
  $logMsys = '/' + $log.Substring(0,1).ToLower() + ($log.Substring(2) -replace '\\','/')
  # tail in the background; wait for flycast.exe to appear (60s grace), then hold while it lives,
  # then kill the tail -> bash exits -> the console window closes with the game.
  # NOTE: '//FI' (doubled slash) stops MSYS from path-mangling the /FI switch. Single quotes only
  # inside the command - the whole -lc payload must be ONE double-quoted ArgumentList string
  # (an array ArgumentList mangles quoting under Windows PowerShell and bash exits instantly).
  # Pipe through logcolor.awk so the window is readable at a glance. The FILE stays plain text -
  # the escapes only exist in the terminal, so grep and the VS Code reader are unaffected.
  $awk = '/' + $PSScriptRoot.Substring(0,1).ToLower() + ($PSScriptRoot.Substring(2) -replace '\\','/') + '/logcolor.awk'
  $body = "tail -n 50 -F $logMsys | awk -f $awk & TP=`$!; " +
          "up=0; for i in `$(seq 1 60); do tasklist '//FI' 'IMAGENAME eq flycast.exe' 2>/dev/null | grep -qi flycast && { up=1; break; }; sleep 1; done; " +
          "if [ `$up -eq 1 ]; then while tasklist '//FI' 'IMAGENAME eq flycast.exe' 2>/dev/null | grep -qi flycast; do sleep 2; done; fi; " +
          "kill `$TP 2>/dev/null; pkill -f 'tail -n 50 -F $logMsys' 2>/dev/null"
  # MSYS honours these when the console is created, which is how the window comes up at the size
  # you asked for instead of the default 80x25.
  $env:MSYS = "$env:MSYS winsymlinks:nativestrict"
  $env:CHERE_INVOKING = '1'
  $prev = $env:MSYSCON
  Start-Process $gitBash -ArgumentList "-lc `"mode con: cols=$cols lines=$rows >/dev/null 2>&1; $body`"" | Out-Null
  $env:MSYSCON = $prev
} else {
  Write-Host "Git Bash not found - run .\logwatch.ps1 in a terminal instead." -ForegroundColor Yellow
}

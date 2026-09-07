<#
  Automated end-to-end replay tests for the TAS fork.

  For each clip folder under build\data\replays\<Game>\ (newest first), this launches the emulator
  headlessly in read-only playback of that clip - the same path as "Play a Movie" - with:
    dojo:VerifyState=yes     every savestate load is byte-verified against its file
    dojo:AutoSeekState=0     auto-press "F3 slot 0" once playback is running (if the clip has states)
  then watches the log for the verdict markers and reports PASS/FAIL per clip:

    PASS needs: replay loaded from the clip's own .flyr
              + savestate folder pointed at the clip's folder
              + (if it has states) seek happened AND STATE VERIFY: idempotent OK
              + no NOT-idempotent, no crash before the timeout

  Usage:
    .\test.ps1                 # test the newest clip - the routine check
    .\test.ps1 -MaxClips 99    # test everything
    .\test.ps1 -TimeoutSec 90  # give slow clips longer to play out
#>
param(
  [int]$MaxClips = 1,          # newest clip only; enough signal for a routine check
  [int]$TimeoutSec = 60,
  [string]$Game = 'NoBGM_VMU',
  [string]$Match = '',         # only test clips whose folder name matches (e.g. '2026-08-11')
  [switch]$Capture             # ALSO auto-record each replay to build\captures\<clip>.mov (4K, cfhd)
)
if ($Capture -and $TimeoutSec -lt 300) { $TimeoutSec = 300 }   # capture-speed-bound playback + mux
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
$log  = Join-Path $repo 'build\flycast.log'
$clipRoot = Join-Path $repo "build\data\replays\$Game"

if (Get-Process flycast -EA SilentlyContinue) { throw "flycast is running - close it before testing." }

$clips = Get-ChildItem $clipRoot -Directory | Where-Object { $_.Name -notmatch '^_' } |
         Where-Object { $Match -eq '' -or $_.Name -match $Match } |
         Sort-Object Name -Descending | Select-Object -First $MaxClips
if (-not $clips) { throw "No clip folders under $clipRoot" }

# Stop controls: press Q/Esc in this console, or create build\test.stop (for external tools),
# to stop after the current clip finishes. The current clip's emulator is always cleaned up.
$stopFile = Join-Path $repo 'build\test.stop'
Remove-Item $stopFile -EA SilentlyContinue
function Test-StopRequested {
  if (Test-Path $script:stopFile) { return $true }
  try {
    while ([Console]::KeyAvailable) {
      $k = [Console]::ReadKey($true)
      if ($k.Key -eq 'Q' -or $k.Key -eq 'Escape') { return $true }
    }
  } catch {}
  return $false
}
Write-Host ("Testing {0} clip(s). Press Q or Esc to stop after the current clip." -f @($clips).Count) -ForegroundColor DarkGray
$stopRequested = $false

$results = @()
foreach ($c in $clips) {
  if ($stopRequested -or (Test-StopRequested)) {
    Write-Host "Stop requested - ending the suite." -ForegroundColor Yellow
    break
  }
  $flyr = Get-ChildItem $c.FullName -Filter *.flyr | Select-Object -First 1
  if (-not $flyr) { $results += [pscustomobject]@{ Clip=$c.Name; Result='SKIP'; Detail='no .flyr' }; continue }
  $hasState = Test-Path (Join-Path $c.FullName "$Game.state")
  $seekFlag = if ($hasState) { '0' } else { '-1' }

  Write-Host ("--- testing {0}  (states: {1}) ---" -f $c.Name, $(if ($hasState) {'yes'} else {'no'})) -ForegroundColor Cyan

  # Archive any leftover log FIRST so verdict matches can never hit a previous session's content.
  if (Test-Path $log) {
    $logsDir = Join-Path $repo 'build\logs'
    if (-not (Test-Path $logsDir)) { New-Item -ItemType Directory $logsDir | Out-Null }
    try { Move-Item $log (Join-Path $logsDir ("pretest_{0}.log" -f (Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'))) -Force } catch {}
  }

  # NEVER touch a session the harness didn't start: if any flycast is already running (the user
  # launched one mid-suite), stop testing instead of monitoring/killing by process name.
  if (Get-Process flycast -EA SilentlyContinue) {
    Write-Host "A flycast session is already running (yours?) - aborting the remaining tests." -ForegroundColor Yellow
    $results += [pscustomobject]@{ Clip=$c.Name; Result='SKIP'; Detail='user session active' }
    break
  }

  # NOTE: single-line Start-Process - a multi-line ArgumentList silently drops the command.
  # AutoLoadNetState=no: without it the netplay "Download Savestate" popup blocks the boot forever.
  # -NoLogWindow: tests read the log file directly; no Bash tail consoles.
  $capFlag = if ($Capture) { ' -config dojo:AutoCapture=yes' } else { '' }
  $cmd = ("& '{0}\run.ps1' -LogTag test -NoLogWindow -config dojo:StartupPrompt=no -config dojo:NativeConsole=no -config dojo:UiIni=no -config dojo:AutoLoadNetState=no -config dojo:VerifyState=yes -config dojo:VerifyInputs=yes -config dojo:Replay=yes -config ""dojo:ReplayFilename={1}"" -config dojo:AutoSeekState={2}{3}" -f $repo, $flyr.FullName, $seekFlag, $capFlag)
  Start-Process powershell -WindowStyle Hidden -ArgumentList @('-NoProfile', '-Command', $cmd)

  # wait for OUR emulator process to appear and record its PID - all monitoring and cleanup below
  # is PID-scoped so a user-launched flycast is never confused with (or killed as) the test's.
  $ourPid = $null
  foreach ($i in 1..25) {
    Start-Sleep -Seconds 1
    $p = Get-Process flycast -EA SilentlyContinue | Select-Object -First 1
    if ($p) { $ourPid = $p.Id; break }
  }
  $started = [bool]$ourPid

  # poll the log for verdict markers (PID-scoped; a second flycast = user interference -> abort)
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  $loaded=$false; $folderOK=$false; $seek=$false; $verifyOK=$false; $verifyFail=$false; $ended=$false; $contaminated=$false; $fidelityOK = $false; $fidelityFail = $false
  $movReady=$false; $capFail=$false
  while ($started -and (Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 3
    if (Test-StopRequested) { $stopRequested = $true; break }
    $t = Get-Content $log -Raw -EA SilentlyContinue
    if ($t) {
      if ($t -match [regex]::Escape("LOAD REPLAY FILE $($flyr.FullName)")) { $loaded = $true }
      if ($t -match ("savestate folder -> .*" + [regex]::Escape($c.Name))) { $folderOK = $true }
      if ($t -match 'TAS: replay seek to movie frame') { $seek = $true }
      if ($t -match 'STATE VERIFY: idempotent OK') { $verifyOK = $true }
      if ($t -match 'STATE VERIFY: NOT idempotent') { $verifyFail = $true; break }
      if ($t -match 'INPUT FIDELITY: .* fidelity-ok') { $fidelityOK = $true }
      if ($t -match 'FIDELITY-FAIL') { $fidelityFail = $true; break }
      if ($t -match 'TAS: replay end at frame') {
        $ended = $true
        if (-not $Capture) { break }    # capture mode keeps waiting for the .mov mux to finish
      }
      if ($Capture) {
        if ($t -match 'ready \(video \+ audio synced\)') { $movReady = $true; break }
        if ($t -match 'Encode failed|encoder died|failed to start') { $capFail = $true; break }
      }
    }
    $procs = @(Get-Process flycast -EA SilentlyContinue)
    if ($procs.Count -gt 1) { $contaminated = $true; break }        # user launched mid-test
    if (-not ($procs | Where-Object Id -eq $ourPid)) { break }      # ours crashed / exited
  }
  $alive = [bool](Get-Process -Id $ourPid -EA SilentlyContinue)
  if ($ourPid) { Stop-Process -Id $ourPid -Force -EA SilentlyContinue }
  # wait until OUR instance is fully gone so the next test's launch/rotation can't race it
  foreach ($i in 1..10) {
    if (-not (Get-Process -Id $ourPid -EA SilentlyContinue)) { break }
    Start-Sleep -Seconds 1
  }
  Start-Sleep -Seconds 2
  if (-not $started) { $results += [pscustomobject]@{ Clip=$c.Name; Result='FAIL'; Detail='emulator never started' }; continue }
  if ($contaminated) {
    Write-Host "Another flycast appeared mid-test (yours?) - result unreliable; aborting the remaining tests." -ForegroundColor Yellow
    $results += [pscustomobject]@{ Clip=$c.Name; Result='SKIP'; Detail='user session interfered' }
    break
  }
  if ($stopRequested) {
    $results += [pscustomobject]@{ Clip=$c.Name; Result='SKIP'; Detail='stopped by user mid-test' }
    Write-Host "Stopped by user - test emulator cleaned up." -ForegroundColor Yellow
    break
  }

  $pass = $loaded -and $folderOK -and -not $verifyFail -and -not $fidelityFail -and ($alive -or $ended) -and
          ((-not $hasState) -or ($seek -and $verifyOK)) -and
          ((-not $Capture) -or $movReady)
  $detail = @()
  $detail += $(if ($loaded)   {'loaded'}    else {'NO-LOAD'})
  $detail += $(if ($folderOK) {'folder-ok'} else {'FOLDER-WRONG'})
  if ($hasState) {
    $detail += $(if ($seek)     {'seek'}      else {'NO-SEEK'})
    $detail += $(if ($verifyOK) {'verify-ok'} else {'NO-VERIFY'})
    if ($fidelityFail) { $detail += 'FIDELITY-FAIL' }
    elseif ($fidelityOK) { $detail += 'fidelity-ok' }
    else { $detail += 'fidelity-skip' }
  }
  if ($verifyFail) { $detail += 'VERIFY-FAIL' }
  if ($ended)      { $detail += 'played-to-end' }
  if ($Capture) {
    $movFile = Join-Path $repo ("build\captures\{0}.mov" -f $flyr.BaseName)
    if ($movReady -and (Test-Path $movFile)) {
      $detail += ('mov {0:N0} MB' -f ((Get-Item $movFile).Length / 1MB))
    } elseif ($capFail) { $detail += 'CAPTURE-FAIL' }
    else { $detail += 'NO-MOV' }
  }
  if (-not $alive -and -not $ended) { $detail += 'EXITED-EARLY' }
  $results += [pscustomobject]@{ Clip=$c.Name; Result=$(if ($pass) {'PASS'} else {'FAIL'}); Detail=($detail -join ' ') }
}

Remove-Item $stopFile -EA SilentlyContinue

# Machine-readable results for external tooling (e.g. a VS Code extension view).
$jsonPath = Join-Path $repo 'build\logs\test_results.json'
try {
  if (-not (Test-Path (Split-Path $jsonPath))) { New-Item -ItemType Directory (Split-Path $jsonPath) | Out-Null }
  @{ ranAt = (Get-Date).ToString('s'); game = $Game; results = $results } | ConvertTo-Json -Depth 4 | Set-Content $jsonPath
} catch {}

Write-Host ""
Write-Host "=============== TAS replay test results ===============" -ForegroundColor Cyan
$results | Format-Table -AutoSize
Write-Host "JSON: build\logs\test_results.json" -ForegroundColor DarkGray
$failed = @($results | Where-Object Result -eq 'FAIL').Count
Write-Host ("{0} tested, {1} failed" -f $results.Count, $failed) -ForegroundColor $(if ($failed) {'Red'} else {'Green'})
exit $failed

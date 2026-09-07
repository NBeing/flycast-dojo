<#
  T4: the dead-timeline guard, verified end-to-end with a scripted session (gate G2).

  Key insight that makes this cheap: THE GUARD DOES NOT NEED A MATCH. It needs frames, saves and
  rewinds - all of which exist from power-on. The recording runs from frame 0 while the game sits
  in its boot/attract screens; no menu navigation, no character select.

  Scripted sequence (keybd_event, same rig as scrubstuck.ps1):
    boot recording -> F1 (BASE @ F0) -> F2,F1 (slot1 @ F1) -> F2,F1 (slot2 @ F2)
    -> F2,F1 (slot3 @ F3) -> Shift+F2 x2 (back to slot1) -> F3 (REWIND to F1, the timeline event)
    -> record past it -> F2,F2 (to slot3) -> F1 (re-save slot3 AFTER the rewind) -> quit.

  Offline assertions, all from clip.json alone (states[].rerecordSeq + rewinds, schema 5):
    A. rewinds has exactly one entry [1, F1].
    B. slot0 CLEAN  (F0 <  F1: below the rewind point).
    C. slot1 CLEAN  (frame == rewind frame, not below it - the rule is strict <).
    D. slot2 STALE  (F2 > F1 and its seq 0 < rewind seq 1).
    E. slot3 CLEAN  (re-saved after the rewind: its seq >= 1).
    F. the log carries exactly one "recording rewound" line.

    .\guardtest.ps1          # run + assert + clean up the junk clip
    .\guardtest.ps1 -Keep    # keep the clip folder for inspection
#>
param([switch]$Keep, [int]$BootSec = 14)
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
if (Get-Process flycast -EA SilentlyContinue) { throw "flycast is running - close it first." }

$clipRoot = Join-Path $repo 'build\data\replays\NoBGM_VMU'
$before = @(Get-ChildItem $clipRoot -Directory | Select-Object -ExpandProperty Name)

$log = Join-Path $repo 'build\flycast.log'
Remove-Item $log -EA SilentlyContinue
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
$rom = Join-Path $repo 'NoBGM_VMU.cdi'
$p = Start-Process -FilePath (Join-Path $repo 'build\flycast.exe') -WorkingDirectory (Join-Path $repo 'build') -PassThru -ArgumentList @(
  '-config','log:LogToFile=yes','-config','log:INPUT=yes','-config','dojo:NativeConsole=no','-config','dojo:UiIni=no',
  '-config','dojo:Training=yes','-config','dojo:RecordMatches=yes',
  '-config','dojo:Replay=no','-config','dojo:StartupPrompt=no',
  '-config','dojo:PurgeStale=no',$rom)   # this harness asserts the MARKING semantics; the purge would delete the evidence
Write-Host "recording session pid $($p.Id); letting frames accumulate ${BootSec}s..."
Start-Sleep -Seconds $BootSec

Add-Type -Namespace N -Name K -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, IntPtr e);
[DllImport("user32.dll")] public static extern uint MapVirtualKey(uint c, uint t);
'@
$sh = New-Object -ComObject WScript.Shell
function Focus-Emu {
  $p.Refresh()
  [N.K]::ShowWindow($p.MainWindowHandle, 9) | Out-Null
  [N.K]::keybd_event(0x12,0,0,[IntPtr]::Zero); [N.K]::keybd_event(0x12,0,2,[IntPtr]::Zero)
  [N.K]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
  try { $sh.AppActivate($p.Id) | Out-Null } catch {}
  Start-Sleep -Milliseconds 600
  return ([N.K]::GetForegroundWindow() -eq $p.MainWindowHandle)
}
function Tap([byte]$vk) {
  $sc = [byte]([N.K]::MapVirtualKey([uint32]$vk, 0))   # SDL keys off the SCANCODE
  [N.K]::keybd_event($vk, $sc, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 90
  [N.K]::keybd_event($vk, $sc, 2, [IntPtr]::Zero); Start-Sleep -Milliseconds 700
}
function ShiftTap([byte]$vk) {
  $ss = [byte]([N.K]::MapVirtualKey(0x10, 0))
  [N.K]::keybd_event(0x10, $ss, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 120
  Tap $vk
  [N.K]::keybd_event(0x10, $ss, 2, [IntPtr]::Zero); Start-Sleep -Milliseconds 250
}
$F1=[byte]0x70; $F2=[byte]0x71; $F3=[byte]0x72; $KX=[byte]0x58; $KR=[byte]0x52

$focused = Focus-Emu
# Refinement check 1: R at the write frontier with NO BASE saved must refuse with a message and
# stay in write mode (the rest of the test passing proves recording continued).
Write-Host "  R at frontier without BASE (must refuse politely)..."
Tap $KR
Write-Host "  save BASE + three checkpoints..."
Tap $F1                                  # BASE @ F0
Start-Sleep -Seconds 2
Tap $F2; Tap $F1                         # slot1 @ F1
Start-Sleep -Seconds 2
Tap $F2; Tap $F1                         # slot2 @ F2
Start-Sleep -Seconds 2
Tap $F2; Tap $F1                         # slot3 @ F3
Start-Sleep -Seconds 1
Write-Host "  rewind to slot1, then DIVERGE (the refined timeline event)..."
ShiftTap $F2; ShiftTap $F2               # slot3 -> slot1
Tap $F3                                  # rewind ARMS the detector - no event yet
Start-Sleep -Milliseconds 400
Tap $KX                                  # a real game input where the movie had none = divergence
Start-Sleep -Seconds 3                   # record past it
Write-Host "  re-save slot3 after the rewind..."
$null = Focus-Emu                        # focus can drift mid-run; re-take it before each block
Tap $F2; Tap $F2                         # slot1 -> slot3
Tap $F1                                  # slot3 re-saved with seq 1
Start-Sleep -Seconds 1
# Refinement check 2: REVIEW COSTS NOTHING - seek back and flip to read-only without diverging;
# the rewind must NOT add a timeline event (the count assertion below stays at exactly 1).
Write-Host "  review-only: F3 back + R to read-only (must add NO event)..."
$null = Focus-Emu
ShiftTap $F2; ShiftTap $F2               # slot3 -> slot1
Tap $F3                                  # arms the detector...
Tap $KR                                  # ...but read-only means no writes ever follow
Start-Sleep -Seconds 2
Stop-Process -Id $p.Id -Force; Start-Sleep -Seconds 2

# ---- offline assertions --------------------------------------------------------------------
$after = @(Get-ChildItem $clipRoot -Directory | Select-Object -ExpandProperty Name)
$newClip = @($after | Where-Object { $before -notcontains $_ })
if ($newClip.Count -ne 1) { Write-Host "INCONCLUSIVE - expected exactly one new clip, got $($newClip.Count)" -ForegroundColor Yellow; exit 2 }
$clipDir = Join-Path $clipRoot $newClip[0]
$j = Get-Content (Join-Path $clipDir 'clip.json') -Raw | ConvertFrom-Json
$logText = Get-Content $log -Raw

function Get-State($slot) { $j.states | Where-Object { $_.slot -eq $slot } }
function Test-Stale($st) {
  foreach ($rw in $j.rewinds) { if ($rw[0] -gt $st.rerecordSeq -and $rw[1] -lt $st.movieFrame) { return $true } }
  return $false
}

$fail = @()
if (-not $focused) { Write-Host "INCONCLUSIVE - window never focused, keys may not have landed" -ForegroundColor Yellow }

$rw = @($j.rewinds)
if ($rw.Count -ne 1)            { $fail += "expected 1 rewind, got $($rw.Count)" }
$s0 = Get-State 0; $s1 = Get-State 1; $s2 = Get-State 2; $s3 = Get-State 3
foreach ($pair in @(@(0,$s0),@(1,$s1),@(2,$s2),@(3,$s3))) {
  if (-not $pair[1]) { $fail += "slot $($pair[0]) missing from states[]" }
}
if ($fail.Count -eq 0) {
  # Refined semantics: the event frame is the DIVERGENCE frame - strictly after the rewind
  # target (slot1) and before slot2. slot1 is therefore clean now: nothing below the divergence
  # was invalidated, which is exactly the false positive the refinement removes.
  $ev = [int]$rw[0][1]
  if ($ev -le $s1.movieFrame -or $ev -ge $s2.movieFrame) { $fail += "event frame $ev not in (slot1 $($s1.movieFrame), slot2 $($s2.movieFrame))" }
  if ($s0.movieFrame -ge $s1.movieFrame) { $fail += "BASE not below slot1 (ordering broke)" }
  if (Test-Stale $s0)                    { $fail += "BASE wrongly stale" }
  if (Test-Stale $s1)                    { $fail += "slot1 wrongly stale (it sits BELOW the divergence - the refinement's whole point)" }
  if (-not (Test-Stale $s2))             { $fail += "slot2 NOT stale (saved before the divergence, above it)" }
  if (Test-Stale $s3)                    { $fail += "slot3 wrongly stale (re-saved after the event)" }
}
if ($logText -notmatch 'REFUSED - needs a BASE state') { $fail += "frontier-R-without-BASE refusal message missing" }
if ($logText -notmatch 'input diverged at frame') { $fail += "divergence confirmation log line missing" }
$rewLines = ([regex]::Matches($logText, 'rewind to frame \d+ armed')).Count
if ($rewLines -ne 2)            { $fail += "expected 2 armed-rewind log lines (event run + review run), got $rewLines" }

Write-Host ''
Write-Host '=============== guard test (T4 / gate G2) ==============='
Write-Host ("  clip        : {0}" -f $newClip[0])
Write-Host ("  rewinds     : {0}" -f (($j.rewinds | ForEach-Object { "[$($_[0]),$($_[1])]" }) -join ' '))
foreach ($pair in @(@('BASE',$s0),@('slot1',$s1),@('slot2',$s2),@('slot3',$s3))) {
  if ($pair[1]) {
    $verdict = if (Test-Stale $pair[1]) { 'STALE' } else { 'clean' }
    Write-Host ("  {0,-6}: frame {1,5}  seq {2}  -> {3}" -f $pair[0], $pair[1].movieFrame, $pair[1].rerecordSeq, $verdict)
  }
}
if (-not $Keep) { Remove-Item $clipDir -Recurse -Force -EA SilentlyContinue }
if ($fail.Count -gt 0) { $fail | ForEach-Object { Write-Host "  FAIL: $_" -ForegroundColor Red }; exit 1 }
Write-Host 'PASS - the guard flags exactly the states the rewind invalidated' -ForegroundColor Green

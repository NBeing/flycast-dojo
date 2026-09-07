<#
  Regression test for the stranded frame-advance at End of Replay.

  A guard that swallows a key RELEASE latches the hold. The End-of-Replay window takes keyboard
  focus, so gui_keyboard_captured() was true when Space came up: step_held stayed true and mainui
  scrubbed a dead movie forever, logging "TAS SCRUB: 0 frames/s" once a second until the game was
  closed. The same shape would have left a BASE hold armed and overwritten it unattended.

  Seeks to a state shortly BEFORE the movie's end (magnetoNew slot 9 @ 3134 of 3605), holds Space
  so the scrub runs the movie out while the key is down, releases into the End-of-Replay window,
  and asserts the scrub does not keep spinning afterwards.

  Note the auto-seek lands ~2 s in, not after a full boot - a state load skips the boot entirely.

    .\scrubstuck.ps1
#>
param([string]$Clip = 'magnetoNew', [string]$Game = 'NoBGM_VMU', [int]$Slot = 9, [int]$BootSec = 5)
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
if (Get-Process flycast -EA SilentlyContinue) { throw "flycast is running - close it first." }

$clipDir = Join-Path $repo "build\data\replays\$Game\$Clip"
$flyr = Get-ChildItem $clipDir -Filter *.flyr | Select-Object -First 1
if (-not $flyr) { throw "no .flyr in $clipDir" }

$log = Join-Path $repo 'build\flycast.log'
Remove-Item $log -EA SilentlyContinue
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
$proc = Start-Process -FilePath (Join-Path $repo 'build\flycast.exe') -WorkingDirectory (Join-Path $repo 'build') -PassThru -ArgumentList @(
  '-config','log:LogToFile=yes','-config','log:Verbosity=4', '-config','dojo:NativeConsole=no','-config','dojo:UiIni=no',
  '-config','dojo:Replay=yes','-config',"dojo:ReplayFilename=$($flyr.FullName)",'-config','dojo:InputTrace=yes',
  '-config','dojo:Training=yes','-config','dojo:StartupPrompt=no',
  '-config',"dojo:AutoSeekState=$Slot",
  (Join-Path $repo 'NoBGM_VMU.cdi'))
Write-Host "launched pid $($proc.Id); waiting for playback to start..."
Start-Sleep -Seconds $BootSec

Add-Type -Namespace N -Name K -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, IntPtr e);
[DllImport("user32.dll")] public static extern uint MapVirtualKey(uint c, uint t);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
public struct RECT { public int Left, Top, Right, Bottom; }
'@
$sh = New-Object -ComObject WScript.Shell
$proc.Refresh()
[N.K]::ShowWindow($proc.MainWindowHandle, 9) | Out-Null
[N.K]::keybd_event(0x12,0,0,[IntPtr]::Zero); [N.K]::keybd_event(0x12,0,2,[IntPtr]::Zero)
[N.K]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
try { $sh.AppActivate($proc.Id) | Out-Null } catch {}
Start-Sleep -Milliseconds 800
$focused = ([N.K]::GetForegroundWindow() -eq $proc.MainWindowHandle)

# The real sequence from the bug report: Space goes DOWN during playback (so step_held latches),
# the auto-seek lands on a state at the movie's end and the replay finishes WHILE it is held, and
# only then is it released - into a window that has taken keyboard focus.
$vk = 0x20; $scan = [byte]([N.K]::MapVirtualKey([uint32]$vk, 0))   # SDL keys off the SCANCODE
[N.K]::keybd_event($vk, $scan, 0, [IntPtr]::Zero)
Write-Host "  Space held - scrubbing the movie out to its end..."
Start-Sleep -Seconds 30
[N.K]::keybd_event($vk, $scan, 2, [IntPtr]::Zero)
Write-Host "  ...released after End of Replay; watching for a runaway scrub..."
Start-Sleep -Seconds 8

# --- TAS hotkeys must be DEAD on the End-of-Replay screen -------------------------------------
# Click on the game area FIRST. Without this the End-of-Replay window keeps ImGui's keyboard
# capture and the keys never reach the handler at all - which makes the assertion below pass even
# on a build with no gate (verified: it did). Clicking away is what the bug report did in practice.
$r = New-Object N.K+RECT
[N.K]::GetWindowRect($proc.MainWindowHandle, [ref]$r) | Out-Null
[N.K]::SetCursorPos($r.Left + 80, $r.Top + 120) | Out-Null
Start-Sleep -Milliseconds 200
[N.K]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)   # LEFTDOWN
Start-Sleep -Milliseconds 80
[N.K]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)   # LEFTUP
Start-Sleep -Milliseconds 500

Write-Host "  tapping F2 / F3 at End of Replay (must do nothing)..."
foreach ($k in 0x71, 0x72) {   # F2 = next slot, F3 = load state
  $sc = [byte]([N.K]::MapVirtualKey([uint32]$k, 0))
  [N.K]::keybd_event($k, $sc, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 120
  [N.K]::keybd_event($k, $sc, 2, [IntPtr]::Zero); Start-Sleep -Milliseconds 600
}
Start-Sleep -Seconds 2

if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Seconds 2 }
$lines = Get-Content $log
$endIdx = ($lines | Select-String -Pattern 'replay end at frame' | Select-Object -First 1).LineNumber
if (-not $endIdx) { Write-Host "INCONCLUSIVE - replay never reached its end" -ForegroundColor Yellow; exit 2 }
$after   = $lines[$endIdx..($lines.Count - 1)]
$scrub   = @($after | Select-String -Pattern 'TAS SCRUB').Count
$stepHit = @($lines | Select-String -Pattern 'hotkey: STEP').Count
$slotAft = @($after | Select-String -Pattern 'hotkey: SLOT').Count
$loadAft = @($after | Select-String -Pattern 'hotkey: LOADSTATE').Count

Write-Host ''
Write-Host '=============== stuck-scrub test ==============='
$capFree = (@($after | Select-String -Pattern 'capKb=0').Count -gt 0)
Write-Host ("  window focused        : {0}" -f $focused)
Write-Host ("  STEP hotkey seen      : {0}" -f $stepHit)
Write-Host ("  TAS SCRUB after end   : {0}   (expect 0 - the scrub must not spin on a dead movie)" -f $scrub)
# The keys can only PROVE the gate if ImGui was not already swallowing them. Without capKb=0 in
# the trace this part is vacuous - it passes on an ungated build too (measured). Say so rather
# than bank a result that was never earned.
if ($capFree) {
  Write-Host ("  SLOT hotkey after end : {0}   (expect 0 - hotkeys are off at End of Replay)" -f $slotAft)
  Write-Host ("  LOADSTATE after end   : {0}   (expect 0 - same)" -f $loadAft)
} else {
  Write-Host "  hotkey gate           : UNVERIFIED - ImGui held keyboard capture the whole time," -ForegroundColor Yellow
  Write-Host "                          so F2/F3 never reached the handler either way." -ForegroundColor Yellow
}
if (-not $focused) { Write-Host 'INCONCLUSIVE - keys never reached the window' -ForegroundColor Yellow; exit 2 }
if ($scrub -gt 0) { Write-Host 'FAIL - frame advance is still latched after End of Replay' -ForegroundColor Red; exit 1 }
if ($slotAft -gt 0 -or $loadAft -gt 0) { Write-Host 'FAIL - TAS hotkeys still fire at End of Replay' -ForegroundColor Red; exit 1 }
if ($capFree) { Write-Host 'PASS - no runaway scrub, and TAS hotkeys are dead at End of Replay' -ForegroundColor Green }
else          { Write-Host 'PASS - no runaway scrub (hotkey gate unverified this run)' -ForegroundColor Green }

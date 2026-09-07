<#
  Regression repro for the F4-browser crash: with the slot browser open (drawing savestate
  thumbnails), toggling fast-forward tears down and rebuilds the render context. Any ImTextureID
  cached across that becomes a dangling COM pointer and the next AddImage() is an access violation.

  Drives the real emulator: boot a clip that HAS thumbnails, F4 to open the browser, then Tab to
  toggle fast-forward twice, and check the process survived with no [GPF] in the log.

    .\crashrepro.ps1
#>
param(
  [string]$Clip = '2026-08-22T21_40_36Z',
  [string]$Game = 'NoBGM_VMU',
  [int]$BootSec = 22
)
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
if (Get-Process flycast -EA SilentlyContinue) { throw "flycast is already running - close it first." }

$clipDir = Join-Path $repo "build\data\replays\$Game\$Clip"
$flyr = Get-ChildItem $clipDir -Filter *.flyr | Select-Object -First 1
if (-not $flyr) { throw "no .flyr in $clipDir" }
$pngs = @(Get-ChildItem $clipDir -Filter *.state.png)
if ($pngs.Count -eq 0) { throw "$Clip has no thumbnails - pick a clip that does, or the repro proves nothing." }
Write-Host "clip $Clip : $($pngs.Count) thumbnail(s) - good, the browser will draw textures."

$log = Join-Path $repo 'build\flycast.log'
Remove-Item $log -EA SilentlyContinue

$exe = Join-Path $repo 'build\flycast.exe'
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
$args = @(
  '-config', 'log:LogToFile=yes', '-config','dojo:NativeConsole=no','-config','dojo:UiIni=no', '-config', 'log:Verbosity=4', '-config', 'log:RENDERER=yes',
  '-config', 'dojo:Replay=yes',
  '-config', "dojo:ReplayFilename=$($flyr.FullName)",
  '-config', 'dojo:Training=yes',
  '-config', 'dojo:StartupPrompt=no',
  (Join-Path $repo 'NoBGM_VMU.cdi')
)
$proc = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory (Join-Path $repo 'build') -PassThru
Write-Host "launched pid $($proc.Id); waiting ${BootSec}s for boot + playback..."
Start-Sleep -Seconds $BootSec

# SDL reads raw key events, which SendKeys (WM_CHAR-level) does not produce - inject at the
# driver level with keybd_event instead, after genuinely foregrounding the window.
Add-Type -Namespace Native -Name Keys -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
[DllImport("user32.dll")] public static extern uint MapVirtualKey(uint uCode, uint uMapType);
'@

$shell = New-Object -ComObject WScript.Shell

function Focus-Emu() {
  $proc.Refresh()
  $h = $proc.MainWindowHandle
  [Native.Keys]::ShowWindow($h, 9) | Out-Null      # SW_RESTORE
  # Windows refuses SetForegroundWindow from a process that is not itself foreground. Tapping ALT
  # releases that lock for the calling thread, and AppActivate is the belt to that braces.
  [Native.Keys]::keybd_event(0x12, 0, 0, [IntPtr]::Zero)
  [Native.Keys]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
  [Native.Keys]::SetForegroundWindow($h) | Out-Null
  try { $shell.AppActivate($proc.Id) | Out-Null } catch {}
  Start-Sleep -Milliseconds 700
  return ([Native.Keys]::GetForegroundWindow() -eq $h)
}

function Send-Key([byte]$vk, [string]$what) {
  if ($proc.HasExited) { Write-Host "  !! flycast already exited before $what" -ForegroundColor Red; return }
  if (-not (Focus-Emu)) { Write-Host "  (warning: window not foreground)" -ForegroundColor Yellow }
  # SDL keys off the SCANCODE, not the virtual key. Injecting scan=0 makes it read a different
  # key entirely (Tab arrived as R), so derive the real scancode with MapVirtualKey.
  $scan = [byte]([Native.Keys]::MapVirtualKey([uint32]$vk, 0))
  [Native.Keys]::keybd_event($vk, $scan, 0, [IntPtr]::Zero)      # key down
  Start-Sleep -Milliseconds 80
  [Native.Keys]::keybd_event($vk, $scan, 2, [IntPtr]::Zero)      # KEYEVENTF_KEYUP
  Write-Host "  sent $what"
  Start-Sleep -Seconds 3
}

Send-Key 0x73 'F4 (open slot browser -> draws thumbnails)'
Send-Key 0x09 'Tab (fast-forward ON  -> context teardown/rebuild)'
Send-Key 0x09 'Tab (fast-forward OFF -> context teardown/rebuild)'
Start-Sleep -Seconds 3

$alive = -not $proc.HasExited
if ($alive) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Seconds 2 }

$text = if (Test-Path $log) { Get-Content $log -Raw } else { '' }
$gpf      = $text -match '\[GPF\]'
$reinit   = ([regex]::Matches($text, 'DX9 Context initializing')).Count
$fforward = $text -match 'FFORWARD'
$picker   = $text -match 'SLOT_PICKER'

Write-Host ''
Write-Host '=============== crash repro ==============='
Write-Host ("  browser opened      : {0}" -f $picker)
Write-Host ("  fast-forward toggled: {0}" -f $fforward)
Write-Host ("  context (re)inits   : {0}   <- >1 means the teardown actually happened" -f $reinit)
Write-Host ("  GPF in log          : {0}" -f $gpf)
Write-Host ("  survived            : {0}" -f $alive)
if (-not $picker -or -not $fforward) { Write-Host "INCONCLUSIVE - keys did not reach the window" -ForegroundColor Yellow; exit 2 }
if ($reinit -lt 2) { Write-Host "INCONCLUSIVE - no context rebuild, nothing was stressed" -ForegroundColor Yellow; exit 2 }
if ($gpf -or -not $alive) { Write-Host 'FAIL - still crashing' -ForegroundColor Red; exit 1 }
Write-Host 'PASS - survived the context rebuild with the browser open' -ForegroundColor Green

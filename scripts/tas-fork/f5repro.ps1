<#
  Repro rig for "paused + F5 breaks stepping": boot a replay, pause, then F5/Space/F5/Space
  and dump the instrumented gui_open_step/pause state lines.
#>
param([int]$BootSec = 16)
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
if (Get-Process flycast -EA SilentlyContinue) { throw "flycast is running - close it first." }

$log = Join-Path $repo 'build\flycast.log'
Remove-Item $log -EA SilentlyContinue
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
$rom = Join-Path $repo 'NoBGM_VMU.cdi'
$flyr = Get-ChildItem (Join-Path $repo 'build\data\replays\NoBGM_VMU\magnetoNew') -Filter *.flyr | Select-Object -First 1
$p = Start-Process -FilePath (Join-Path $repo 'build\flycast.exe') -WorkingDirectory (Join-Path $repo 'build') -PassThru -ArgumentList @(
  '-config','log:LogToFile=yes','-config','log:INPUT=yes','-config','dojo:NativeConsole=no','-config','dojo:UiIni=no',
  '-config','dojo:Replay=yes','-config',"dojo:ReplayFilename=$($flyr.FullName)",
  '-config','dojo:PianoRoll=yes',
  '-config','dojo:Training=yes','-config','dojo:StartupPrompt=no',$rom)
Write-Host "pid $($p.Id); waiting ${BootSec}s for boot..."
Start-Sleep -Seconds $BootSec

Add-Type -Namespace N -Name K -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, IntPtr e);
[DllImport("user32.dll")] public static extern uint MapVirtualKey(uint c, uint t);
'@
$sh = New-Object -ComObject WScript.Shell
$p.Refresh()
[N.K]::ShowWindow($p.MainWindowHandle, 9) | Out-Null
[N.K]::keybd_event(0x12,0,0,[IntPtr]::Zero); [N.K]::keybd_event(0x12,0,2,[IntPtr]::Zero)
[N.K]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
try { $sh.AppActivate($p.Id) | Out-Null } catch {}
Start-Sleep -Milliseconds 600

function Tap([byte]$vk, [string]$label) {
  $sc = [byte]([N.K]::MapVirtualKey([uint32]$vk, 0))
  Write-Host "  tap $label"
  [N.K]::keybd_event($vk, $sc, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 90
  [N.K]::keybd_event($vk, $sc, 2, [IntPtr]::Zero); Start-Sleep -Milliseconds 900
}
$KP=[byte]0x50; $F5=[byte]0x74; $SPACE=[byte]0x20

Tap $KP    'P (pause)'
Tap $SPACE 'Space (step, UI shown)'
Tap $F5    'F5 (hide)'
Tap $SPACE 'Space (step, UI hidden) #1'
Tap $SPACE 'Space (step, UI hidden) #2'
Tap $F5    'F5 (show)'
Tap $SPACE 'Space (step, UI shown again)'
Start-Sleep -Seconds 2
Stop-Process -Id $p.Id -Force
Start-Sleep -Seconds 1
Write-Host "---- state trail ----"
Select-String 'gui_open_step: state|gui_open_pause: state|TAS_UI|hotkey: STEP|hotkey: PAUSE|replay seek' $log | Select-Object -Last 30 | ForEach-Object { $_.Line }

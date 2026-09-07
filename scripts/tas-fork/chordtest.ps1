<#
  Verifies keyboard chord bindings (Shift+F2 etc.) against the real emulator.

  Two properties matter:
    1. A bound chord fires its OWN action        (Shift+F2 -> previous slot)
    2. The same key without the modifier still fires the plain action, and holding a modifier
       that is NOT part of any binding does not swallow the key. This is the safety property -
       a stray Shift during gameplay must never stop an input registering.

  Writes a throwaway keyboard mapping, drives the emulator with injected scancodes, reads the log,
  then restores whatever mapping was there before.

    .\chordtest.ps1
#>
param([int]$BootSec = 20)
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
if (Get-Process flycast -EA SilentlyContinue) { throw "flycast is running - close it first." }

$mapDir = Join-Path $repo 'build\mappings'
New-Item -ItemType Directory -Force $mapDir | Out-Null
$mapFile = Join-Path $mapDir 'SDL_Keyboard.cfg'
$backup  = "$mapFile.chordtest.bak"
if (Test-Path $mapFile) { Copy-Item $mapFile $backup -Force }

# HID usages: F1=58 F2=59 F3=60. Shift flag = 0x10000 (InputMapping::KEY_MOD_SHIFT).
$SHIFT = 65536
@"
[digital]
bind0 = 59:btn_savestate_slot_next
bind1 = $($SHIFT + 59):btn_savestate_slot_prev
bind2 = 41:btn_menu
bind3 = 58:btn_quick_save
[emulator]
mapping_name = Keyboard
dead_zone = 10
saturation = 100
rumble_power = 100
version = 3
"@ | Set-Content $mapFile -Encoding ASCII

$log = Join-Path $repo 'build\flycast.log'
Remove-Item $log -EA SilentlyContinue
$exe = Join-Path $repo 'build\flycast.exe'
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
$proc = Start-Process -FilePath $exe -WorkingDirectory (Join-Path $repo 'build') -PassThru -ArgumentList @(
  '-config','log:LogToFile=yes','-config','log:Verbosity=4',
  '-config','dojo:Training=yes','-config','dojo:StartupPrompt=no','-config','dojo:UiIni=no',
  (Join-Path $repo 'NoBGM_VMU.cdi'))
Write-Host "launched pid $($proc.Id); waiting ${BootSec}s..."
Start-Sleep -Seconds $BootSec

Add-Type -Namespace Native -Name Keys -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
[DllImport("user32.dll")] public static extern uint MapVirtualKey(uint uCode, uint uMapType);
'@
$shell = New-Object -ComObject WScript.Shell
function Focus-Emu() {
  $proc.Refresh()
  [Native.Keys]::ShowWindow($proc.MainWindowHandle, 9) | Out-Null
  [Native.Keys]::keybd_event(0x12, 0, 0, [IntPtr]::Zero); [Native.Keys]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
  [Native.Keys]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
  try { $shell.AppActivate($proc.Id) | Out-Null } catch {}
  Start-Sleep -Milliseconds 700
}
function Tap([byte]$vk) {
  $scan = [byte]([Native.Keys]::MapVirtualKey([uint32]$vk, 0))   # SDL keys off the SCANCODE
  [Native.Keys]::keybd_event($vk, $scan, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 90
  [Native.Keys]::keybd_event($vk, $scan, 2, [IntPtr]::Zero); Start-Sleep -Milliseconds 500
}
$VK_F1 = 0x70; $VK_F2 = 0x71; $VK_SHIFT = 0x10

Focus-Emu
Write-Host '  F2 alone'                ; Tap $VK_F2
Write-Host '  Shift+F2 (chord)'
[Native.Keys]::keybd_event($VK_SHIFT, [byte]([Native.Keys]::MapVirtualKey(0x10,0)), 0, [IntPtr]::Zero)
Start-Sleep -Milliseconds 250
Tap $VK_F2
[Native.Keys]::keybd_event($VK_SHIFT, [byte]([Native.Keys]::MapVirtualKey(0x10,0)), 2, [IntPtr]::Zero)
Start-Sleep -Milliseconds 400
Write-Host '  F2 alone again'          ; Tap $VK_F2
Write-Host '  Shift+F1 (UNBOUND chord - must still fire plain F1)'
[Native.Keys]::keybd_event($VK_SHIFT, [byte]([Native.Keys]::MapVirtualKey(0x10,0)), 0, [IntPtr]::Zero)
Start-Sleep -Milliseconds 250
Tap $VK_F1
[Native.Keys]::keybd_event($VK_SHIFT, [byte]([Native.Keys]::MapVirtualKey(0x10,0)), 2, [IntPtr]::Zero)
Start-Sleep -Seconds 2

if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Seconds 2 }
$text = Get-Content $log -Raw
if (Test-Path $backup) { Move-Item $backup $mapFile -Force } else { Remove-Item $mapFile -EA SilentlyContinue }
# The emulator can write a full default keyboard mapping when it finds none, which would then
# shadow future changes to the built-in defaults. Leave nothing behind that was not there before.
if (-not (Test-Path $backup) -and (Test-Path $mapFile)) {
  Remove-Item $mapFile -Force
  Write-Host "  (removed a keyboard mapping file the emulator generated during the test)"
}

$next  = ([regex]::Matches($text, 'hotkey: SLOT next')).Count
$prev  = ([regex]::Matches($text, 'SAVESTATE_SLOT_PREV')).Count
$save  = ([regex]::Matches($text, 'hotkey: SAVESTATE slot')).Count
Write-Host ''
Write-Host '=============== chord test ==============='
Write-Host ("  plain F2  -> NEXT fired : {0}   (expect 2)" -f $next)
Write-Host ("  Shift+F2  -> PREV fired : {0}   (expect 1)" -f $prev)
Write-Host ("  Shift+F1 (unbound chord) -> plain F1 SAVESTATE fired : {0}   (expect >=1)" -f $save)
if ($next -eq 2 -and $prev -eq 1 -and $save -ge 1) {
  Write-Host 'PASS - chords fire, plain keys unaffected, unbound modifier falls through' -ForegroundColor Green
} else {
  Write-Host 'FAIL' -ForegroundColor Red; exit 1
}

<#
  Task launcher for Flycast Dojo TAS work.

  Each debugging/working session type is a named PROFILE bundling the right -config flags and
  logging setup, so nobody has to remember the flag zoo. run.ps1 stays the engine underneath.

    .\launch.ps1                 # interactive numbered menu
    .\launch.ps1 combo           # run a profile by name
    .\launch.ps1 desync -Game    # extra run.ps1 switches / -config flags pass through
    .\launch.ps1 help            # list every profile AND the full config-flag reference

  Session logs: run.ps1 archives the previous log to build\logs\<profile>_<timestamp>.log, so each
  archived log is named after the task that produced it (easy to hand to Claude for analysis).
#>
param(
  [Parameter(Position=0)][string]$Task,
  [Parameter(ValueFromRemainingArguments=$true)]$Rest
)

$profiles = [ordered]@{
  'combo' = @{
    Desc  = 'Normal combo session: startup prompt, savestate-verify tripwire on, quiet logs'
    Flags = @('-config','dojo:VerifyState=yes')
  }
  'desync' = @{
    Desc  = 'Desync hunt: add the SERMAP offset dump to the always-on state verify'
    Flags = @('-config','dojo:StateMapLog=yes')
  }
  'avi' = @{
    Desc  = 'AVI/encode debugging: capture timing + ffmpeg lines, no SERMAP spam'
    Flags = @()
  }
  'input' = @{
    Desc  = 'Input debugging: INFO-level INPUT/MAPLE traces, chatty channels muted'
    Flags = @('-config','log:Verbosity=4','-config','log:DYNAREC=no','-config','log:MEMORY=no',
              '-config','log:VMEM=no','-config','log:AICA=no','-config','log:RENDERER=no',
              '-config','log:PVR=no','-config','log:COMMON=no')
  }
  'vanilla' = @{
    Desc  = 'Plain boot straight into Training: no startup prompt'
    Flags = @('-config','dojo:StartupPrompt=no')
  }
  'tasmenu' = @{
    Desc  = 'TAS UX testing: normal boot from the startup prompt (Record/Play/Just Play), full TAS UI + traces'
    Flags = @('-config','dojo:InputTrace=yes')
  }
  'ghost' = @{
    Desc  = 'Ghost-input hunt: input tracer on, pads OUT of menu navigation (drift-proof menus)'
    Flags = @('-config','dojo:InputTrace=yes','-config','dojo:MenuGamepadNav=no')
  }
}

function Show-Reference {
  Write-Host ""
  Write-Host "PROFILES  (.\launch.ps1 <name>)" -ForegroundColor Cyan
  foreach ($k in $profiles.Keys) {
    Write-Host ("  {0,-9} {1}" -f $k, $profiles[$k].Desc)
  }
  Write-Host ""
  Write-Host "CONFIG FLAG REFERENCE  (append any as: -config section:Key=value)" -ForegroundColor Cyan
  Write-Host @"
  dojo:StartupPrompt   (yes)   Record / Play / Just Play chooser at launch
  dojo:VerifyState     (no)    verify every savestate load round-trips (STATE VERIFY + SERMAP log)
  dojo:HoldStepFPS     (60)    hold-Space frame-advance scrub speed
  dojo:HoldStepDelay   (16)    frames Space must be held before auto-repeat begins
  dojo:PostEncode      (cfhd)  ffmpeg mux after F12 stop: cfhd | prores | none
  dojo:ProResProfile   (2)     ProRes tier 0-3 (Proxy/LT/Std/HQ); emu.cfg currently 1
  dojo:ProResQscale    (11)    ProRes size lever, higher = smaller; emu.cfg currently 13
  dojo:KeepAviWav      (0)     keep the intermediate .avi/.wav after a successful mux
  dojo:RecordMatches           auto-record a .flyr (set by the startup prompt, not by hand)
  dojo:Replay / ReplayFilename movie playback (set by the replay browser, not by hand)
  dojo:LastRomPath             last booted ROM (written automatically; prompt/browser fallback)
  log:Verbosity        (3)     1=debug .. 5=error; 4 (INFO) only fires in -EnableLog builds
  log:<CHANNEL>=yes|no         per-channel toggles (INPUT, SAVESTATE, NETWORK, AICA, PVR, ...)
  pvr.rend             (1)     renderer: 1=DX9 (current; correct MvC2 supers), 2=DX11, 0=GL, 4=Vulkan
                               NOTE: AVI capture is wired for DX9 + DX11 only
  rend.Resolution      (2880)  internal render height (6x native = AVI source resolution)

  run.ps1 switches (pass through): -Game (no training)  -List (game list)  -Verbose (log firehose)  -NoLog
"@
  Write-Host ""
}

if ($Task -in @('help','-help','--help','/?')) { Show-Reference; return }

if ([string]::IsNullOrWhiteSpace($Task)) {
  Write-Host ""
  Write-Host "Flycast Dojo TAS launcher - pick a session type:" -ForegroundColor Cyan
  $keys = @($profiles.Keys)
  for ($i = 0; $i -lt $keys.Count; $i++) {
    Write-Host ("  [{0}] {1,-9} {2}" -f ($i + 1), $keys[$i], $profiles[$keys[$i]].Desc)
  }
  Write-Host "  [h] help      full profile + config-flag reference"
  Write-Host "  [q] quit"
  $pick = Read-Host "choice"
  if ($pick -eq 'q' -or [string]::IsNullOrWhiteSpace($pick)) { return }
  if ($pick -eq 'h') { Show-Reference; return }
  $n = 0
  if ([int]::TryParse($pick, [ref]$n) -and $n -ge 1 -and $n -le $keys.Count) { $Task = $keys[$n - 1] }
  else { Write-Host "Unknown choice '$pick'" -ForegroundColor Yellow; return }
}

if (-not $profiles.Contains($Task)) {
  Write-Host "Unknown profile '$Task'. Known: $($profiles.Keys -join ', ')  (or: .\launch.ps1 help)" -ForegroundColor Yellow
  return
}

Write-Host "Launching profile '$Task' - $($profiles[$Task].Desc)" -ForegroundColor Green
$allArgs = @($profiles[$Task].Flags) + @($Rest | ForEach-Object { $_ })
& (Join-Path $PSScriptRoot 'run.ps1') -LogTag $Task @allArgs

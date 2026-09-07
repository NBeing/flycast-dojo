<#
  Launch Flycast Dojo (dojo-7) straight into Training mode, and pop a Git Bash terminal that prints
  the emulator's log live (tail -F). One command = game + log console.

  Examples:
    .\run.ps1                 # boot MvC2 into Training + open the Bash log terminal   <-- default
    .\run.ps1 -Game           # boot MvC2 but NOT into Training
    .\run.ps1 -List           # open the game list instead of booting a ROM
    .\run.ps1 -NoLog          # don't write the log / don't open the log terminal
    .\run.ps1 -Verbose        # full (noisy) log firehose
    .\run.ps1 "D:\other.chd"  # boot a specific ROM (into Training)

  Logs go to build\flycast.log. The MSYS2 MINGW64 runtime DLLs are put on PATH so the exe resolves.
  Quieted to WARNING level unless -Verbose (kills per-frame spam; NOTICE_LOG traces + savestate still show).
#>
param(
  [switch]$Game,     # boot the ROM but not into Training
  [switch]$Train,    # (default behaviour) kept for compatibility
  [switch]$List,     # show the game list instead of booting the ROM
  [switch]$Verbose,
  [switch]$NoLog,
  [switch]$LogWindow,           # ALSO pop the Git Bash tail window (the emulator now owns the
                                # default console, so the tail is for reading past a crash)
  [switch]$NoLogWindow,         # accepted for compatibility; the tail is off by default now
  [string]$LogTag = 'session'   # names this session's archived log (launch.ps1 passes the profile)
)

$buildDir = Join-Path $PSScriptRoot 'build'
$exe = Join-Path $buildDir 'flycast.exe'
if (-not (Test-Path $exe)) { throw "flycast.exe not found. Build it first with .\build.ps1" }

$mingwBin = 'C:\msys64\mingw64\bin'
if (Test-Path $mingwBin) { $env:PATH = "$mingwBin;$env:PATH" }

$rom = Join-Path $PSScriptRoot 'NoBGM_VMU.cdi'
$log = Join-Path $buildDir 'flycast.log'

$flags = @()

# Quiet the firehose unless -Verbose: WARNING level kills the per-frame INFO/DEBUG spam (DYNAREC block
# dispatch, unmapped-memory reads, etc). Our hotkey traces use NOTICE_LOG and savestate messages are
# NOTICE/WARNING, so they still show. -Verbose keeps the full firehose.
if (-not $Verbose) {
  $flags += @('-config', 'log:Verbosity=3')
}

# Log to file, and pop the shared Git Bash log window (logwindow.ps1) that tails it live.
# Before launching, archive the previous session's log to build\logs\<tag>_<timestamp>.log so past
# sessions stay reviewable and named by task (the live log keeps the fixed path the tail watches).
# The tag of the PREVIOUS session is read from a marker file written at its launch.
if (-not $NoLog) {
  $marker = "$log.tag"
  if (Test-Path $log) {
    $logsDir = Join-Path $buildDir 'logs'
    if (-not (Test-Path $logsDir)) { New-Item -ItemType Directory $logsDir | Out-Null }
    $prevTag = if (Test-Path $marker) { (Get-Content $marker -First 1).Trim() } else { 'session' }
    if ([string]::IsNullOrWhiteSpace($prevTag)) { $prevTag = 'session' }
    $stamp = (Get-Item $log).LastWriteTime.ToString('yyyy-MM-dd_HH-mm-ss')
    try { Move-Item $log (Join-Path $logsDir "${prevTag}_$stamp.log") -Force -ErrorAction Stop } catch {}
  }
  Set-Content $marker $LogTag
  $flags += @('-config', 'log:LogToFile=yes')
  # The emulator opens its own console (dojo:NativeConsole, default on) - one window, coloured,
  # docked beside the game. The Bash tail is opt-in for when you want scrollback that survives a
  # crash on screen; the log FILE always has it regardless.
  if ($LogWindow) { & (Join-Path $PSScriptRoot 'logwindow.ps1') }
}

# TESTING-PHASE defaults (roadmap days 1-3): the traces that make input/timeline bugs visible are
# ON by default so every session's flycast.log carries the evidence - input tracer, MvC2 memory
# trace, and savestate/input NOTICE channels. Public-facing quiet-down happens after the phase.
$flags += @('-config','dojo:InputTrace=yes','-config','dojo:MemTrace=yes',
            '-config','log:INPUT=yes','-config','log:NETWORK=yes','-config','log:SAVESTATE=yes')

# Boot into Training by default; -Game skips Training; -List shows the game list.
if (-not $List -and -not $Game) { $flags += @('-config', 'dojo:Training=yes') }

$flags += $args    # user extras (e.g. -config pvr.rend=4) before the ROM

if (-not $List) {
  if (-not (Test-Path $rom)) { throw "ROM not found at $rom (expected the MvC2 .cdi at the repo root)." }
  $flags += $rom
}

# Launch from build\ so flycast.log lands there; & blocks until the game window closes.
Push-Location $buildDir
try { & $exe @flags } finally { Pop-Location }

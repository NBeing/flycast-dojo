<#
.SYNOPSIS
    One-shot Windows dev setup for Flycast Dojo: installs MSYS2, the MinGW64
    toolchain and every dependency, then builds a runnable folder.

.DESCRIPTION
    Run this in a normal PowerShell window on a machine with nothing installed.
    It is idempotent - re-running skips whatever is already in place, so it is
    also a safe way to repair a half-finished setup.

    It does NOT need Administrator unless MSYS2 is being installed to a
    protected location (the default C:\msys64 is fine for a normal user).

.PARAMETER Msys2Root
    Where MSYS2 lives, or will be installed. Default C:\msys64.

.PARAMETER SourceDir
    Existing flycast-dojo checkout to build. If omitted, the script clones a
    fresh one next to itself (or into -CloneTo).

.PARAMETER CloneTo
    Parent directory for a fresh clone. Default: the current directory.

.PARAMETER SkipBuild
    Install the toolchain but stop before building.

.PARAMETER Clean
    Delete the build directory before building.

.EXAMPLE
    .\setup-and-build.ps1
    Installs everything and builds a fresh clone.

.EXAMPLE
    .\setup-and-build.ps1 -SourceDir C:\dev\flycast-dojo
    Builds an existing checkout.

.NOTES
    Do not use WSL for this. WSL produces Linux ELF binaries, not a Windows
    .exe, and has no USB HID passthrough - controllers would not work.
#>
[CmdletBinding()]
param(
    [string] $Msys2Root = 'C:\msys64',
    [string] $SourceDir = '',
    [string] $CloneTo   = '',
    [switch] $SkipBuild,
    [switch] $Clean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-Step { param([string]$Message) Write-Host "==> $Message" -ForegroundColor Magenta }
function Write-Warn { param([string]$Message) Write-Host " warn: $Message" -ForegroundColor Yellow }
function Write-Ok   { param([string]$Message) Write-Host "  ok: $Message" -ForegroundColor Green }

# --- 1. MSYS2 ---------------------------------------------------------------

$bash = Join-Path $Msys2Root 'usr\bin\bash.exe'

if (Test-Path $bash) {
    Write-Ok "MSYS2 already installed at $Msys2Root"
}
else {
    Write-Step "Installing MSYS2 to $Msys2Root"

    # The installer is a Qt Installer Framework binary, which supports a fully
    # unattended install. 'latest' always redirects to the current release.
    $installerUrl = 'https://github.com/msys2/msys2-installer/releases/latest/download/msys2-x86_64-latest.exe'
    $installerPath = Join-Path $env:TEMP 'msys2-installer.exe'

    if (-not (Test-Path $installerPath)) {
        Write-Step "Downloading MSYS2 installer"
        # Progress rendering makes Invoke-WebRequest dramatically slower.
        $prevProgress = $ProgressPreference
        $ProgressPreference = 'SilentlyContinue'
        try   { Invoke-WebRequest -Uri $installerUrl -OutFile $installerPath -UseBasicParsing }
        finally { $ProgressPreference = $prevProgress }
    }
    else {
        Write-Ok "Reusing installer at $installerPath"
    }

    Write-Step "Running unattended install (this takes a few minutes)"
    $proc = Start-Process -FilePath $installerPath -Wait -PassThru -ArgumentList @(
        'in', '--confirm-command', '--accept-messages', '--root', $Msys2Root
    )
    if ($proc.ExitCode -ne 0) {
        throw "MSYS2 installer failed with exit code $($proc.ExitCode)"
    }
    if (-not (Test-Path $bash)) {
        throw "MSYS2 installer reported success but $bash is missing"
    }
    Write-Ok "MSYS2 installed"
}

# Run a command inside the MINGW64 environment.
#   -MSYSTEM MINGW64 selects the toolchain that builds native 64-bit Windows
#     binaries. This is the setting people get wrong by opening the wrong
#     Start-menu shortcut.
#   -CHERE_INVOKING=1 keeps the current directory instead of jumping to $HOME.
function Invoke-Mingw {
    param(
        [Parameter(Mandatory)][string] $Command,
        [string] $WorkingDirectory = $PWD.Path,
        [switch] $AllowFailure
    )
    $env:MSYSTEM = 'MINGW64'
    $env:CHERE_INVOKING = '1'
    # Losing the exit code through the pipeline is the classic way these
    # wrappers silently "succeed", so capture it explicitly. Initialised up
    # front so Set-StrictMode cannot trip on an unassigned variable if the
    # call itself throws.
    $code = 0
    Push-Location $WorkingDirectory
    try {
        & $bash -lc $Command
        $code = $LASTEXITCODE
    }
    finally { Pop-Location }

    if ($code -ne 0 -and -not $AllowFailure) {
        throw "MINGW64 command failed (exit $code): $Command"
    }
    return $code
}

# --- 2. Update the package database -----------------------------------------

Write-Step "Updating MSYS2 packages"
# A core update can replace pacman/msys2-runtime itself and terminate the
# shell mid-run. The documented remedy is simply to run it again, so do the
# first pass tolerantly and the second strictly.
Invoke-Mingw -Command 'pacman -Syuu --noconfirm' -AllowFailure | Out-Null
Invoke-Mingw -Command 'pacman -Syuu --noconfirm' | Out-Null
Write-Ok "packages up to date"

# --- 3. Source tree ---------------------------------------------------------

$repoUrl = 'https://github.com/blueminder/flycast-dojo'

if ($SourceDir -eq '') {
    $parent = if ($CloneTo -ne '') { $CloneTo } else { $PWD.Path }
    if (-not (Test-Path $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    $SourceDir = Join-Path $parent 'flycast-dojo'

    if (Test-Path (Join-Path $SourceDir 'CMakeLists.txt')) {
        Write-Ok "Reusing existing checkout at $SourceDir"
    }
    else {
        Write-Step "Cloning into $SourceDir"
        # --recursive matters: ~9 submodules, and a missing one fails much
        # later with a misleading CMake error.
        Invoke-Mingw -Command "git clone --recursive '$repoUrl' '$(($SourceDir -replace '\\','/'))'" | Out-Null
    }
}
else {
    if (-not (Test-Path (Join-Path $SourceDir 'CMakeLists.txt'))) {
        throw "No CMakeLists.txt in $SourceDir - is that a flycast-dojo checkout?"
    }
    Write-Ok "Building existing checkout at $SourceDir"
}

if ($SkipBuild) {
    Write-Ok "Toolchain ready. Skipping build as requested."
    Write-Host ""
    Write-Host "  To build:  open 'MSYS2 MINGW64' and run" -ForegroundColor Cyan
    Write-Host "             ./shell/windows/build-mingw64.sh" -ForegroundColor Cyan
    return
}

# --- 4. Build ---------------------------------------------------------------

Write-Step "Building (first run compiles ~1000 objects; later runs are incremental)"

$posixSrc = ($SourceDir -replace '\\','/')
$buildArgs = @()
if ($Clean) { $buildArgs += '--clean' }
$argString = $buildArgs -join ' '

Invoke-Mingw -WorkingDirectory $SourceDir `
    -Command "cd '$posixSrc' && ./shell/windows/build-mingw64.sh $argString" | Out-Null

$stage = Join-Path $SourceDir 'artifact\flycast-dojo-win64'
Write-Host ""
Write-Ok "Build complete"
Write-Host ""
Write-Host "  Runnable folder : $stage" -ForegroundColor Cyan
Write-Host "  Source tree     : $SourceDir" -ForegroundColor Cyan
Write-Host ""
Write-Host "  Rebuild after edits, from the MSYS2 MINGW64 shell:" -ForegroundColor Cyan
Write-Host "    cd '$posixSrc' && cmake --build build --target install" -ForegroundColor Cyan
Write-Host ""
Write-Host "  Do NOT delete build/ - incremental rebuilds take seconds." -ForegroundColor DarkGray

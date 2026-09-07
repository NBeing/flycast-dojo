<#
  Dev build for Flycast Dojo (dojo-7 branch) using the MSYS2 MINGW64 toolchain.

  Examples:
    .\build.ps1                # configure on first run, then incremental build -> build\flycast.exe
    .\build.ps1 -EnableLog     # reconfigure with -DENABLE_LOG=ON so INFO_LOG/DEBUG_LOG fire (+ dev console)
    .\build.ps1 -Reconfigure   # force cmake to re-run configure
    .\build.ps1 -Clean         # delete build/ then configure + build from scratch

  Output: build\flycast.exe  (run it with .\run.ps1). NOTE dojo-7 names the target `flycast`,
  not `flycast-dojo` like the legacy master.

  dojo-7 builds WITH Vulkan (its bundled glslang has the gcc <cstdint> fix) and needs no zlib/CMake
  patch, unlike master. We still pass -DCMAKE_POLICY_VERSION_MINIMUM=3.5 for other vendored deps and
  the system-curl flags.
#>
param(
  [switch]$Reconfigure,
  [switch]$EnableLog,
  [switch]$Clean
)
$ErrorActionPreference = 'Stop'

$bash = 'C:\msys64\usr\bin\bash.exe'
if (-not (Test-Path $bash)) { throw "MSYS2 bash not found at $bash. Install MSYS2 or fix this path." }

# Convert this script's Windows dir (C:\a\b) to an MSYS path (/c/a/b)
$winRepo  = $PSScriptRoot
$drive    = $winRepo.Substring(0,1).ToLower()
$msysRepo = "/$drive" + ($winRepo.Substring(2) -replace '\\','/')

$cfg = '-G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ' +
       '-DCPR_FORCE_USE_SYSTEM_CURL=ON -DCPR_FORCE_OPENSSL_BACKEND=ON'
if ($EnableLog) { $cfg += ' -DENABLE_LOG=ON' }

$env:MSYSTEM = 'MINGW64'

if ($Clean) { & $bash -lc "rm -rf '$msysRepo/build'" }

$needConfigure = $Reconfigure -or $EnableLog -or -not (Test-Path (Join-Path $winRepo 'build\build.ninja'))

$steps = "cd '$msysRepo'"
if ($needConfigure) { $steps += " && cmake -B build $cfg" }
$steps += " && cmake --build build"

& $bash -lc $steps
exit $LASTEXITCODE

<#
  T6: the edit funnel, end-to-end (gate G2 for TEXT edits).

  Works on a COPY of the clean-text fixture clip (which guardtest -Keep minted: 4 savestates with
  v2 sidecars, one recorded rewind, known frames), so the fixture itself never drifts.

  Run 1: mint <copy>/movie.tas.txt (dojo:TextRoundTrip).
  Splice: hold P1 Right for one frame at F = slot1.frame - 50 (between BASE and slot1).
  Run 2: dojo:TextApply pushes the edit through Dojo::ApplyEdit.
  Assert offline: TAS EDIT log line; rewinds grew by exactly [seq+1, F]; the guard verdicts
  flip exactly as a rewind to F would (BASE stays clean; slot1/slot2/slot3 all stale, since all
  sit above F and were saved before the edit); the .flyr grew (append persistence).
  Run 3: reload the movie fresh and re-export - the text at frame F must now hold the R,
  proving last-write-wins persistence through a full load cycle.

    .\textguard.ps1
    .\textguard.ps1 -Fixture 2026-08-23T18_16_00Z
#>
param([string]$Fixture = '2026-08-23T18_16_00Z', [string]$Game = 'NoBGM_VMU', [int]$WaitSec = 10)
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
if (Get-Process flycast -EA SilentlyContinue) { throw "flycast is running - close it first." }

$clipRoot = Join-Path $repo "build\data\replays\$Game"
$srcDir = Join-Path $clipRoot $Fixture
if (-not (Test-Path $srcDir)) { throw "fixture clip $Fixture not found - run guardtest.ps1 -Keep to mint one" }
$tmpName = 'textguard_tmp'
$tmpDir = Join-Path $clipRoot $tmpName
if (Test-Path $tmpDir) { Remove-Item $tmpDir -Recurse -Force }
Copy-Item $srcDir $tmpDir -Recurse
# the copied .flyr keeps its name; find it
$flyr = Get-ChildItem $tmpDir -Filter *.flyr | Select-Object -First 1
$flyrSizeBefore = $flyr.Length

$log = Join-Path $repo 'build\flycast.log'
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
$rom = Join-Path $repo 'NoBGM_VMU.cdi'
function Run-Emu([string[]]$extra) {
  Remove-Item $log -EA SilentlyContinue
  $p = Start-Process -FilePath (Join-Path $repo 'build\flycast.exe') -WorkingDirectory (Join-Path $repo 'build') -PassThru -ArgumentList (@(
    '-config','log:LogToFile=yes','-config','dojo:NativeConsole=no','-config','dojo:UiIni=no',
    '-config','dojo:Replay=yes','-config',"dojo:ReplayFilename=$($flyr.FullName)",
    '-config','dojo:Training=yes','-config','dojo:StartupPrompt=no') + $extra + @($rom))
  Start-Sleep -Seconds $WaitSec
  Stop-Process -Id $p.Id -Force -EA SilentlyContinue
  Start-Sleep -Seconds 1
}

# ---- run 1: mint the text export ------------------------------------------------------------
Write-Host "run 1: exporting the copy's movie as text..."
Run-Emu @('-config','dojo:TextRoundTrip=yes')
$txt = Join-Path $tmpDir 'movie.tas.txt'
if (-not (Test-Path $txt)) { throw "run 1 produced no movie.tas.txt" }

# ---- pick F and splice ------------------------------------------------------------------------
$j0 = Get-Content (Join-Path $tmpDir 'clip.json') -Raw | ConvertFrom-Json
$slot1 = $j0.states | Where-Object { $_.slot -eq 1 }
$F = [int]$slot1.movieFrame - 50
$rewBefore = @($j0.rewinds).Count
Write-Host ("splicing: hold P1 Right for one frame at F={0} (slot1 is at {1})" -f $F, $slot1.movieFrame)

# walk the text positionally; split the run containing F
$out = New-Object System.Collections.Generic.List[string]
$cursor = 0; $done = $false
foreach ($line in (Get-Content $txt)) {
  if (-not ($line -like '|*') -or $done -and $false) { }
  if ($line -notlike '|*') { $out.Add($line); continue }
  $body = $line; $rep = 1
  if ($body -match '^(.*\|)\s\*(\d+)$') { $body = $Matches[1]; $rep = [int]$Matches[2] }
  if (-not $done -and $F -ge $cursor -and $F -lt ($cursor + $rep)) {
    $k = $F - $cursor
    if ($k -gt 0) { $out.Add($(if ($k -gt 1) { "$body *$k" } else { $body })) }
    $edited = $body.Substring(0,4) + 'R' + $body.Substring(5)   # bool col 4 (0-based idx 4) = Right
    $out.Add('; textguard edit: frame ' + $F + ' holds P1 Right')
    $out.Add($edited)
    $rest = $rep - $k - 1
    if ($rest -gt 0) { $out.Add($(if ($rest -gt 1) { "$body *$rest" } else { $body })) }
    $done = $true
  } else {
    $out.Add($line)
  }
  $cursor += $rep
}
if (-not $done) { throw "frame $F not found in named lines (fixture not clean-text?)" }
Set-Content (Join-Path $tmpDir 'movie.edit.tas.txt') $out -Encoding ASCII

# ---- run 2: apply through the funnel ---------------------------------------------------------
Write-Host "run 2: applying the edit through Dojo::ApplyEdit..."
Run-Emu @('-config','dojo:TextApply=yes')
$log2 = Get-Content $log -Raw
$applied = [regex]::Match($log2, 'TAS EDIT: applied [^\r\n]*')
$appended = $log2 -match 'TAS EDIT: appended'

# ---- offline assertions ----------------------------------------------------------------------
$j = Get-Content (Join-Path $tmpDir 'clip.json') -Raw | ConvertFrom-Json
function Get-State($slot) { $j.states | Where-Object { $_.slot -eq $slot } }
function Test-Stale($st) {
  foreach ($rw in $j.rewinds) { if ($rw[0] -gt $st.rerecordSeq -and $rw[1] -lt $st.movieFrame) { return $true } }
  return $false
}
$fail = @()
if (-not $applied.Success) { $fail += "no 'TAS EDIT: applied' log line" }
if (-not $appended)        { $fail += "no append-persistence log line" }
$rw = @($j.rewinds)
if ($rw.Count -ne $rewBefore + 1) { $fail += "rewinds count $($rw.Count), expected $($rewBefore + 1)" }
elseif ($rw[-1][1] -ne $F)        { $fail += "new timeline event at frame $($rw[-1][1]), expected $F" }
$s0 = Get-State 0; $s1 = Get-State 1; $s2 = Get-State 2; $s3 = Get-State 3
if (Test-Stale $s0)        { $fail += "BASE wrongly stale (it sits below the edit)" }
if (-not (Test-Stale $s1)) { $fail += "slot1 NOT stale (edit at $F is below its frame $($s1.movieFrame))" }
if (-not (Test-Stale $s2)) { $fail += "slot2 NOT stale" }
if (-not (Test-Stale $s3)) { $fail += "slot3 NOT stale (its seq predates the edit's event)" }
$flyrAfter = (Get-Item $flyr.FullName).Length
if ($flyrAfter -le $flyrSizeBefore) { $fail += ".flyr did not grow ($flyrSizeBefore -> $flyrAfter)" }

# ---- run 3: reload + re-export; the R must survive a full load cycle -------------------------
Write-Host "run 3: reload and re-export - the edit must persist through last-write-wins..."
Remove-Item (Join-Path $tmpDir 'movie.edit.tas.txt') -Force
Run-Emu @('-config','dojo:TextRoundTrip=yes')
$cursor = 0; $charAtF = '?'
foreach ($line in (Get-Content $txt)) {
  if ($line -notlike '|*') { continue }
  $body = $line; $rep = 1
  if ($body -match '^(.*\|)\s\*(\d+)$') { $body = $Matches[1]; $rep = [int]$Matches[2] }
  if ($F -ge $cursor -and $F -lt ($cursor + $rep)) { $charAtF = $body.Substring(4,1); break }
  $cursor += $rep
}
if ($charAtF -ne 'R') { $fail += "after reload, frame $F reads '$charAtF' in the Right column, expected 'R'" }

Write-Host ''
Write-Host '=============== text edit funnel (T6 / gate G2) ==============='
Write-Host ("  edit      : {0}" -f $(if ($applied.Success) { $applied.Value } else { '(none)' }))
Write-Host ("  rewinds   : {0}" -f (($j.rewinds | ForEach-Object { "[$($_[0]),$($_[1])]" }) -join ' '))
foreach ($pair in @(@('BASE',$s0),@('slot1',$s1),@('slot2',$s2),@('slot3',$s3))) {
  if ($pair[1]) {
    $verdict = if (Test-Stale $pair[1]) { 'STALE' } else { 'clean' }
    Write-Host ("  {0,-6}: frame {1,5}  seq {2}  -> {3}" -f $pair[0], $pair[1].movieFrame, $pair[1].rerecordSeq, $verdict)
  }
}
Write-Host ("  .flyr     : {0} -> {1} bytes (append)" -f $flyrSizeBefore, $flyrAfter)
Write-Host ("  reloaded  : frame {0} Right column = '{1}'" -f $F, $charAtF)
Remove-Item $tmpDir -Recurse -Force -EA SilentlyContinue
if ($fail.Count -gt 0) { $fail | ForEach-Object { Write-Host "  FAIL: $_" -ForegroundColor Red }; exit 1 }
Write-Host 'PASS - a text edit stales states exactly as a rewind does, and persists' -ForegroundColor Green

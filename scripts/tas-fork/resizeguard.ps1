<#
  PR3: row DELETE, end-to-end (the resize funnel + full-file rewrite).

  Works on a COPY of the clean-text fixture clip (guardtest -Keep minted it), same as
  textguard, so the fixture never drifts.

  Run 1: mint <copy>/movie.tas.txt (dojo:TextRoundTrip); expand run-length lines to a
         per-frame array -> the reference movie (length L).
  Run 2: TextApply plants a marker frame (P1 Right) at M=515 - the fixture movie is all-
         neutral, and deleting inside an identity run diffs only at the tail, so without a
         marker every shift assertion passes vacuously.
  Run 3: dojo:ResizeProbe=F-F+9 deletes [500, 509] through ApplyEditResize ->
         RewriteReplayFile. Assert: probe + resize + rewrite log lines; one new guard event
         at the first REAL content diff (505: neutral vs the marker 10 up); slots stale iff
         anchored above that event (or already stale in the baseline); .flyr size changed.
  Run 4: reload the mutated file fresh and re-export. The text must now be L-10 frames with
         the marker at M-10 (tail pulled down), frame F-1 untouched, last frame preserved -
         proving the rewritten file round-trips a SHORTER movie.

    .\resizeguard.ps1
    .\resizeguard.ps1 -Fixture 2026-08-23T18_16_00Z
#>
param([string]$Fixture = '2026-08-23T18_16_00Z', [string]$Game = 'NoBGM_VMU', [int]$WaitSec = 10)
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
if (Get-Process flycast -EA SilentlyContinue) { throw "flycast is running - close it first." }

$clipRoot = Join-Path $repo "build\data\replays\$Game"
$srcDir = Join-Path $clipRoot $Fixture
if (-not (Test-Path $srcDir)) { throw "fixture clip $Fixture not found - run guardtest.ps1 -Keep to mint one" }
$tmpName = 'resizeguard_tmp'
$tmpDir = Join-Path $clipRoot $tmpName
if (Test-Path $tmpDir) { Remove-Item $tmpDir -Recurse -Force }
Copy-Item $srcDir $tmpDir -Recurse
$flyr = Get-ChildItem $tmpDir -Filter *.flyr | Select-Object -First 1
$flyrSizeBefore = $flyr.Length

$log = Join-Path $repo 'build\flycast.log'
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
$rom = Join-Path $repo 'NoBGM_VMU.cdi'
function Run-Emu([string[]]$extra) {
  Remove-Item $log -EA SilentlyContinue
  $p = Start-Process -FilePath (Join-Path $repo 'build\flycast.exe') -WorkingDirectory (Join-Path $repo 'build') -PassThru -ArgumentList (@(
    '-config','log:LogToFile=yes','-config','dojo:NativeConsole=no','-config','dojo:UiIni=no',
    '-config','dojo:PurgeStale=no',
    '-config','dojo:Replay=yes','-config',"dojo:ReplayFilename=$($flyr.FullName)",
    '-config','dojo:Training=yes','-config','dojo:StartupPrompt=no') + $extra + @($rom))
  Start-Sleep -Seconds $WaitSec
  Stop-Process -Id $p.Id -Force -EA SilentlyContinue
  Start-Sleep -Seconds 1
}

# expand the run-length text export into one string per frame
function Expand-Movie([string]$path) {
  $frames = New-Object System.Collections.Generic.List[string]
  foreach ($line in (Get-Content $path)) {
    if ($line -notlike '|*') { continue }
    $body = $line; $rep = 1
    if ($body -match '^(.*\|)\s\*(\d+)$') { $body = $Matches[1]; $rep = [int]$Matches[2] }
    for ($i = 0; $i -lt $rep; $i++) { $frames.Add($body) }
  }
  return $frames
}

# ---- run 1: reference export -----------------------------------------------------------------
Write-Host "run 1: exporting the copy's movie as text (reference)..."
Run-Emu @('-config','dojo:TextRoundTrip=yes')
$txt = Join-Path $tmpDir 'movie.tas.txt'
if (-not (Test-Path $txt)) { throw "run 1 produced no movie.tas.txt" }
$ref = Expand-Movie $txt
$L = $ref.Count
Write-Host ("reference: L={0} frames" -f $L)

# ---- splice a marker so the tail-shift assertions BITE ---------------------------------------
# The fixture movie is all-neutral (one *L run) - deleting inside it diffs only at the tail,
# and any "did the tail pull down" check passes vacuously. So: plant one distinctive frame
# (P1 Right) at M, ABOVE the delete range [F, F+9]; after the delete it must sit at M-10.
$F = 500; $M = 515
$body = $ref[0]
$marker = $body.Substring(0,4) + 'R' + $body.Substring(5)
# walk the export positionally (keep header/comment lines - the importer needs them)
$out = New-Object System.Collections.Generic.List[string]
$cursor = 0; $done = $false
foreach ($line in (Get-Content $txt)) {
  if ($line -notlike '|*') { $out.Add($line); continue }
  $b = $line; $rep = 1
  if ($b -match '^(.*\|)\s\*(\d+)$') { $b = $Matches[1]; $rep = [int]$Matches[2] }
  if (-not $done -and $M -ge $cursor -and $M -lt ($cursor + $rep)) {
    $k = $M - $cursor
    if ($k -gt 0) { $out.Add($(if ($k -gt 1) { "$b *$k" } else { $b })) }
    $out.Add('; resizeguard marker: frame ' + $M + ' holds P1 Right')
    $out.Add($marker)
    $rest = $rep - $k - 1
    if ($rest -gt 0) { $out.Add($(if ($rest -gt 1) { "$b *$rest" } else { $b })) }
    $done = $true
  } else { $out.Add($line) }
  $cursor += $rep
}
if (-not $done) { throw "frame $M not found in frame lines" }
Set-Content (Join-Path $tmpDir 'movie.edit.tas.txt') $out -Encoding ASCII

# ---- run 2: apply the marker (TextApply) and re-export in the same run -----------------------
Write-Host "run 2: TextApply plants the marker at $M, then re-exports..."
Run-Emu @('-config','dojo:TextApply=yes','-config','dojo:TextRoundTrip=yes')
$ref = Expand-Movie $txt
if ($ref.Count -ne $L) { throw "marker splice changed the movie length ($($ref.Count) vs $L)" }
if ($ref[$M] -eq $body) { throw "marker did not land at frame $M" }
# post-splice clip.json is the BASELINE for the delete's assertions
$j0 = Get-Content (Join-Path $tmpDir 'clip.json') -Raw | ConvertFrom-Json
$rewBefore = @($j0.rewinds).Count
$flyrSizeBefore = (Get-Item $flyr.FullName).Length
Write-Host ("deleting [{0}, {1}]; marker at {2} must land at {3}" -f $F, ($F + 9), $M, ($M - 10))

# ---- run 3: delete through the resize funnel -------------------------------------------------
Write-Host "run 3: dojo:ResizeProbe deletes 10 frames through ApplyEditResize..."
Run-Emu @('-config',"dojo:ResizeProbe=$F-$($F + 9)")
$log2 = Get-Content $log -Raw
$fail = @()
if ($log2 -notmatch 'TAS RESIZE PROBE: deleting') { $fail += "no probe start line" }
if ($log2 -notmatch 'TAS EDIT: resize from resize probe') { $fail += "no ApplyEditResize log line" }
if ($log2 -notmatch 'rewrote .* - length change persisted') { $fail += "no RewriteReplayFile log line" }
$now = [regex]::Match($log2, 'TAS RESIZE PROBE: movie now (\d+) frames')
if (-not $now.Success) { $fail += "no post-op length line" }
elseif ([int]$now.Groups[1].Value -ne ($L - 10)) { $fail += "post-op length $($now.Groups[1].Value), expected $($L - 10)" }

$j = Get-Content (Join-Path $tmpDir 'clip.json') -Raw | ConvertFrom-Json
function Get-State($slot) { $j.states | Where-Object { $_.slot -eq $slot } }
function Test-Stale($st) {
  foreach ($rw in $j.rewinds) { if ($rw[0] -gt $st.rerecordSeq -and $rw[1] -lt $st.movieFrame) { return $true } }
  return $false
}
# The funnel diffs CONTENT: deleting inside a run of identical frames first differs where
# the shifted movie actually deviates. Expected event frame = first k with ref[k] != ref[k+10].
$expF = $L - 10
for ($k = $F; $k -lt ($L - 10); $k++) { if ($ref[$k] -ne $ref[$k + 10]) { $expF = $k; break } }
Write-Host ("expected first-diff frame: {0}" -f $expF)
$rw = @($j.rewinds)
if ($rw.Count -ne $rewBefore + 1) { $fail += "rewinds count $($rw.Count), expected $($rewBefore + 1)" }
elseif ($rw[-1][1] -ne $expF)     { $fail += "guard event at frame $($rw[-1][1]), expected $expF" }
function Test-StaleIn($st, $rewinds) {
  foreach ($rw in $rewinds) { if ($rw[0] -gt $st.rerecordSeq -and $rw[1] -lt $st.movieFrame) { return $true } }
  return $false
}
foreach ($slot in 0..3) {
  $st = Get-State $slot
  $pre = Test-StaleIn ($j0.states | Where-Object { $_.slot -eq $slot }) @($j0.rewinds)
  $want = $pre -or ([int]$st.movieFrame -gt $expF)   # fixture baseline staleness carries over
  $got = Test-Stale $st
  if ($got -ne $want) { $fail += "slot$slot stale=$got, expected $want (pre=$pre, anchor $($st.movieFrame) vs event $expF)" }
}
$flyrAfter = (Get-Item $flyr.FullName).Length
if ($flyrAfter -eq $flyrSizeBefore) { $fail += ".flyr size unchanged ($flyrSizeBefore) - rewrite didn't happen?" }
Write-Host (".flyr {0} -> {1} bytes (rewrite compacts)" -f $flyrSizeBefore, $flyrAfter)

# ---- run 4: reload fresh; the movie must BE shorter and shifted ------------------------------
Write-Host "run 4: reload the rewritten file and re-export..."
Run-Emu @('-config','dojo:TextRoundTrip=yes')
$after = Expand-Movie $txt
if ($after.Count -ne ($L - 10)) { $fail += "reloaded movie is $($after.Count) frames, expected $($L - 10) - delete did not persist" }
else {
  if ($after[$M - 10] -ne $ref[$M]) { $fail += "marker not at $($M - 10) after reload - tail did not pull down" }
  if ($after[$M] -ne $body)         { $fail += "frame $M still holds the marker - tail did not shift" }
  if ($after[$F - 1] -ne $ref[$F - 1]) { $fail += "frame $($F - 1) changed - delete leaked below its range" }
  if ($after[$after.Count - 1] -ne $ref[$L - 1]) { $fail += "last frame after reload != old last frame" }
}

# ---- verdict ---------------------------------------------------------------------------------
Remove-Item $tmpDir -Recurse -Force -EA SilentlyContinue
if ($fail.Count) {
  Write-Host "`nRESIZEGUARD: FAIL" -ForegroundColor Red
  $fail | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
  exit 1
}
Write-Host "`nRESIZEGUARD: PASS - delete rewrote the .flyr, guards fired, shorter movie round-trips" -ForegroundColor Green
exit 0

[CmdletBinding()]
param([string]$BuildDirectory = 'build/session-14')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $root $BuildDirectory }
$output = Join-Path $root 'evidence/session-14'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$reports = [ordered]@{
    'project-result.json' = 'project-test-data/project-result.json'
    'smoke-result.json' = 'smoke-data/smoke-result.json'
    'media-result.json' = 'media-test-data/media-result.json'
    'timeline-result.json' = 'timeline-test-data/timeline-result.json'
    'playback-result.json' = 'playback-test-data/playback-result.json'
    'timeline-ui-result.json' = 'timeline-ui-test-data/timeline-ui-result.json'
    'audio-title-result.json' = 'audio-title-test-data/audio-title-result.json'
    'export-result.json' = 'export-test-data/export-result.json'
    'recovery-result.json' = 'recovery-test-data/recovery-result.json'
    'performance-result.json' = 'performance-test-data/performance-result.json'
    'effects-result.json' = 'effects-test-data/effects-result.json'
    'advanced-effects-result.json' = 'advanced-effects-test-data/advanced-effects-result.json'
    'sequences-result.json' = 'sequences-test-data/sequences-result.json'
}
foreach ($name in $reports.Keys) {
    $source = Join-Path $build $reports[$name]
    if (-not (Get-Content -LiteralPath $source -Raw | ConvertFrom-Json).passed) { throw "Failed suite report: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $output $name) -Force
}
foreach ($name in @('configure.log','build.log','test.log')) { Copy-Item -LiteralPath (Join-Path $build $name) -Destination (Join-Path $output $name) -Force }
if ((Get-Content -LiteralPath (Join-Path $build 'test.log') -Raw) -notmatch '100% tests passed, 0 tests failed out of 13') { throw 'Full thirteen-suite regression evidence missing.' }
$benchmark = Get-Content -LiteralPath (Join-Path $output 'multicam-benchmark.json') -Raw | ConvertFrom-Json
if (-not $benchmark.passed -or $benchmark.measurements.Count -ne 2) { throw 'CPU and actual D3D11VA synchronized-playback evidence missing.' }
$sequences = Get-Content -LiteralPath (Join-Path $output 'sequences-result.json') -Raw | ConvertFrom-Json
$originals = foreach ($entry in (Get-Content -LiteralPath (Join-Path $root 'evidence/session-0/all-media/inventory.json') -Raw | ConvertFrom-Json).files) {
    $file = Get-Item -LiteralPath $entry.path
    [ordered]@{path=$entry.path;unchanged=($file.Length -eq $entry.sizeBytes -and $file.LastWriteTimeUtc -eq [datetime]$entry.lastWriteTimeUtc)}
}
if (@($originals | Where-Object { -not $_.unchanged }).Count) { throw 'Source size/mtime drift from original inventory.' }
$sources = foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root 'src'),(Join-Path $root 'tests') -Recurse -File) { [ordered]@{path=[IO.Path]::GetRelativePath($root,$file.FullName);sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()} }
[ordered]@{
    passed=$true;collectedAtUtc=[datetime]::UtcNow.ToString('o');version='0.14.0';schemaVersion=6;automatedSuiteCount=13
    focusedChecks=$sequences.checks;worstPixelMeanError=$sequences.worstPixelMeanError;worstAudioRmsError=$sequences.worstAudioRmsError
    benchmark=$benchmark.measurements;originals=$originals;sources=$sources
    executableSha256=(Get-FileHash -LiteralPath (Join-Path $build 'Release/VideoEditor.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
    evidenceLevel='Pinned native Windows build, thirteen automated suites, real-source CPU/D3D11VA synchronized playback, native offscreen controls and independent MP4 video/audio decoding'
    humanVisualAcceptance='not performed';limitations=@('Matching frame rates and 1x nested sources','Four-camera decode preview capped at 360p per camera; exports use originals','No automatic camera sync, real multi-camera recording or perceived AV sync acceptance','Offscreen presentation does not verify Windows display scaling')
} | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath (Join-Path $output 'verification.json') -Encoding utf8
Write-Host "Session 14 evidence: $output/verification.json"

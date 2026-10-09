[CmdletBinding()]
param([string]$BuildDirectory = 'build/session-13')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $root $BuildDirectory }
$output = Join-Path $root 'evidence/session-13'
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
}
$parsed = [ordered]@{}
foreach ($name in $reports.Keys) {
    $source = Join-Path $build $reports[$name]
    $result = Get-Content -LiteralPath $source -Raw | ConvertFrom-Json
    if (-not $result.passed) { throw "Failed report: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $output $name) -Force
    $parsed[$name] = $result
}
foreach ($name in @('configure.log', 'build.log', 'test.log')) {
    $source = Join-Path $build $name
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing build evidence: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $output $name) -Force
}
$ctestLog = Join-Path $build 'Testing/Temporary/LastTest.log'
Copy-Item -LiteralPath $ctestLog -Destination (Join-Path $output 'ctest-detailed.log') -Force
$testLog = Get-Content -LiteralPath (Join-Path $output 'test.log') -Raw
if ($testLog -notmatch '100% tests passed, 0 tests failed out of 12') { throw 'The complete twelve-suite regression result is missing.' }
if ($parsed['smoke-result.json'].version -ne '0.13.0') { throw 'Unexpected application version.' }
$audio = $parsed['audio-title-result.json']
$export = $parsed['export-result.json']
$timelineUi = $parsed['timeline-ui-result.json']
if ($audio.checkCount -lt 60) { throw 'The expected Session 13 audio checks are missing.' }
if ($export.checkCount -lt 150 -or $export.audioRmsError -gt 0.001) { throw 'Independent export audio checks are incomplete or outside tolerance.' }
if ($timelineUi.checkCount -lt 100) { throw 'The native timeline UI check set is incomplete.' }
$reference = $export.exports | Where-Object { $_.passed -and $_.audioMix.masterPeak -gt 0 } | Select-Object -First 1
if (-not $reference) { throw 'No successful reference export with audio mix metrics was found.' }
$artifacts = foreach ($name in @('VideoEditor.exe', 'AudioTitleTests.exe', 'ExportTests.exe', 'TimelineUiTests.exe')) {
    $path = Join-Path $build "Release/$name"
    [ordered]@{ name = $name; sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$sources = foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root 'src'), (Join-Path $root 'tests') -Recurse -File) {
    [ordered]@{ path = [IO.Path]::GetRelativePath($root, $file.FullName); sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$report = [ordered]@{
    passed = $true
    collectedAtUtc = [datetime]::UtcNow.ToString('o')
    version = '0.13.0'
    schemaVersion = 5
    buildDirectory = $build
    automatedSuiteCount = 12
    focusedCheckCounts = [ordered]@{
        audioTitle = $audio.checkCount
        independentExport = $export.checkCount
        timelineUi = $timelineUi.checkCount
    }
    independentExportAudioRmsError = $export.audioRmsError
    referenceMix = [ordered]@{
        frames = $reference.frames
        samples = $reference.samples
        masterPeak = $reference.audioMix.masterPeak
        clippedSamples = $reference.audioMix.clippedSamples
        trackPeaks = $reference.audioMix.trackPeaks
        busPeaks = $reference.audioMix.busPeaks
    }
    audioTestMixSampleError = @($audio.mixes | Where-Object { $null -ne $_.maxSampleError } | ForEach-Object { $_.maxSampleError })
    evidenceLevel = 'Windows x64 MSVC build; twelve automated suites; native WASAPI shared-output API exercise; exact mix, automation, routing, migration, offscreen controls and independent export decode checks.'
    humanVisualAcceptance = 'not reported'
    perceivedAudioAcceptance = 'not reported'
    endToEndOutputLatency = 'not measured; WASAPI initialization requests a 100 ms shared buffer'
    artifacts = $artifacts
    sources = $sources
    limitations = @('Offscreen Qt checks do not establish Windows display scaling or visual polish', 'Host WASAPI API checks do not measure physical output latency or perceived audio quality', 'Optional audio effects, dynamics processing and loudness normalization are deferred')
    failures = @()
}
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $output 'verification.json') -Encoding utf8
Write-Host "Session 13 evidence collected: $output. All twelve suites passed."

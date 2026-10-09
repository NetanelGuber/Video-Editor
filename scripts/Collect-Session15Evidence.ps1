[CmdletBinding()]
param([string]$BuildDirectory = 'build/session-15')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $root $BuildDirectory }
$output = Join-Path $root 'evidence/session-15'
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
    'capabilities-result.json' = 'capability-test-data/capabilities-result.json'
    'encoder-catalog-result.json' = 'encoder-catalog-test-data/encoder-catalog-result.json'
}
foreach ($name in $reports.Keys) {
    $source = Join-Path $build $reports[$name]
    if (-not (Get-Content -LiteralPath $source -Raw | ConvertFrom-Json).passed) { throw "Failed suite report: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $output $name) -Force
}
foreach ($name in @('configure.log','build.log','test.log')) { Copy-Item -LiteralPath (Join-Path $build $name) -Destination (Join-Path $output $name) -Force }
if ((Get-Content -LiteralPath (Join-Path $build 'test.log') -Raw) -notmatch '100% tests passed, 0 tests failed out of 15') { throw 'Full fifteen-suite regression evidence missing.' }
$capabilities = Get-Content -LiteralPath (Join-Path $output 'capabilities-result.json') -Raw | ConvertFrom-Json
$catalog = Get-Content -LiteralPath (Join-Path $output 'encoder-catalog-result.json') -Raw | ConvertFrom-Json
$sampleDir = Join-Path $output 'samples'
New-Item -ItemType Directory -Path $sampleDir -Force | Out-Null
$samples = foreach ($entry in @($capabilities.exports) + @($catalog.exports)) {
    if (-not $entry.passed) { throw 'Failed representative export.' }
    $source = Get-Item -LiteralPath $entry.path
    $destination = Join-Path $sampleDir $source.Name
    Copy-Item -LiteralPath $source.FullName -Destination $destination -Force
    $metadata = $destination + '-ffprobe.json'
    $entry.metadata | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $metadata -Encoding utf8
    [ordered]@{path=[IO.Path]::GetRelativePath($root,$destination);metadata=[IO.Path]::GetRelativePath($root,$metadata);encoder=$entry.actualEncoder;target=$entry.compatibilityProfile;sha256=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$originals = foreach ($entry in (Get-Content -LiteralPath (Join-Path $root 'evidence/session-0/all-media/inventory.json') -Raw | ConvertFrom-Json).files) {
    $file = Get-Item -LiteralPath $entry.path
    [ordered]@{path=$entry.path;unchanged=($file.Length -eq $entry.sizeBytes -and $file.LastWriteTimeUtc -eq [datetime]$entry.lastWriteTimeUtc)}
}
if (@($originals | Where-Object { -not $_.unchanged }).Count) { throw 'Source size/mtime drift from original inventory.' }
$sources = foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root 'src'),(Join-Path $root 'tests') -Recurse -File) { [ordered]@{path=[IO.Path]::GetRelativePath($root,$file.FullName);sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()} }
$concept = Get-Item -LiteralPath (Join-Path $root 'docs/session-15-concept.md')
$encoderConcept = Get-Item -LiteralPath (Join-Path $root 'docs/session-15-encoder-concept.md')
[ordered]@{
    passed=$true;collectedAtUtc=[datetime]::UtcNow.ToString('o');version='0.15.1';schemaVersion=8;automatedSuiteCount=15
    focusedChecks=$capabilities.checkCount;catalogChecks=$catalog.checkCount;videoEncoderCount=$catalog.videoEncoderCount
    concept=[ordered]@{path='docs/session-15-concept.md';completedBeforeImplementation=$true;writtenAtUtc=$concept.LastWriteTimeUtc.ToString('o');sha256=(Get-FileHash -LiteralPath $concept.FullName -Algorithm SHA256).Hash.ToLowerInvariant();chronologyEvidence='Concept-writing tool call completed before the first source edit in this chat'}
    encoderConcept=[ordered]@{path='docs/session-15-encoder-concept.md';completedBeforeImplementation=$true;writtenAtUtc=$encoderConcept.LastWriteTimeUtc.ToString('o');sha256=(Get-FileHash -LiteralPath $encoderConcept.FullName -Algorithm SHA256).Hash.ToLowerInvariant();chronologyEvidence='Expanded concept-writing tool call completed before the first every-encoder source edit in this chat'}
    targetGpu=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,DriverDate)
    encoderCatalog=$catalog.catalog;encoderProbes=$capabilities.encoderProbes;deviceProbes=$capabilities.deviceProbes;smallHevcHardwareProbe=$capabilities.smallHevcHardwareProbe
    samples=$samples;originals=$originals;sources=$sources
    executableSha256=(Get-FileHash -LiteralPath (Join-Path $build 'Release/VideoEditor.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
    capabilityHelperSha256=(Get-FileHash -LiteralPath (Join-Path $build 'Release/ExportCapabilityProbe.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
    evidenceLevel='Pinned native Windows build, fifteen automated suites, every compiled video encoder classified at representative settings, actual target-PC encoder/device probes, native offscreen controls, independent FFprobe metadata and FFmpeg decoding'
    humanVisualAcceptance='not performed';limitations=@('Physical playback devices not tested; profiles describe file properties','Windows visual scaling, subjective quality and perceived AV sync unmeasured','Short probes do not certify long hardware sessions or every configuration','SDR compositor; higher bit-depth output does not create HDR or additional precision','Packaging and redistribution review remain Session 16')
} | ConvertTo-Json -Depth 60 | Set-Content -LiteralPath (Join-Path $output 'verification.json') -Encoding utf8
Write-Host "Session 15 evidence: $output/verification.json"

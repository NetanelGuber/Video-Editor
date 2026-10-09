[CmdletBinding()]
param([string]$BuildDirectory = 'build/session-15.2')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $root $BuildDirectory }
$output = Join-Path $root 'evidence/session-15.2'
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
    'workspace-result.json' = 'workspace-test-data/workspace-result.json'
    'export-tabs-result.json' = 'export-tabs-test-data/export-tabs-result.json'
}
foreach ($name in $reports.Keys) {
    $source = Join-Path $build $reports[$name]
    if (-not (Get-Content -LiteralPath $source -Raw | ConvertFrom-Json).passed) { throw "Failed suite: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $output $name) -Force
}
if ((Get-Content -LiteralPath (Join-Path $build 'test.log') -Raw) -notmatch '100% tests passed, 0 tests failed out of 18') { throw 'Full eighteen-suite regression evidence missing.' }
foreach ($name in @('configure.log','build.log','test.log')) { Copy-Item -LiteralPath (Join-Path $build $name) -Destination (Join-Path $output $name) -Force }
Copy-Item -LiteralPath (Join-Path $build 'final-focused-tests.log') -Destination (Join-Path $output 'final-focused-tests.log') -Force
$images = Join-Path $output 'screenshots'; New-Item -ItemType Directory -Path $images -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $build 'export-tabs-test-data') -Filter '*.png' | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $images -Force }
$originals = foreach ($entry in (Get-Content -LiteralPath (Join-Path $root 'evidence/session-0/all-media/inventory.json') -Raw | ConvertFrom-Json).files) {
    $file = Get-Item -LiteralPath $entry.path
    [ordered]@{path=$entry.path;unchanged=($file.Length -eq $entry.sizeBytes -and $file.LastWriteTimeUtc -eq [datetime]$entry.lastWriteTimeUtc)}
}
if (@($originals | Where-Object { -not $_.unchanged }).Count) { throw 'Original source-media size/mtime drift.' }
$sources = foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root 'src'),(Join-Path $root 'tests') -Recurse -File) { [ordered]@{path=[IO.Path]::GetRelativePath($root,$file.FullName);sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash} }
$concept = Join-Path $root 'docs/session-15.2-concept.md'
$acceptancePath = Join-Path $output 'manual-acceptance.json'
$acceptance = if (Test-Path -LiteralPath $acceptancePath) { Get-Content -LiteralPath $acceptancePath -Raw | ConvertFrom-Json } else { $null }
[ordered]@{
    passed=$true;version='0.15.4';schemaVersion=11;collectedAtUtc=[datetime]::UtcNow.ToString('o');automatedSuiteCount=18
    exportTabChecks=(Get-Content -LiteralPath (Join-Path $output 'export-tabs-result.json') -Raw | ConvertFrom-Json).checkCount
    concept=[ordered]@{path='docs/session-15.2-concept.md';completedBeforeImplementation=$true;sha256=(Get-FileHash -LiteralPath $concept -Algorithm SHA256).Hash;chronologyEvidence='Concept-writing tool call completed before first source edit in this chat'}
    executableSha256=(Get-FileHash -LiteralPath (Join-Path $build 'Release/VideoEditor.exe') -Algorithm SHA256).Hash
    sources=@($sources);originals=@($originals)
    evidenceLevel='Pinned native Windows Release build; eighteen automated production/model/media/export suites; independent export inspection/decode; offscreen widget layout captures'
    userAcceptance=$acceptance
    nativeVisualReview=if ($acceptance) { 'Session accepted by user; individual Windows appearance-check outcomes not separately reported. No agent computer use performed.' } else { 'Pending focused user review; no computer use authorized' }
    limits=@($(if ($acceptance) { 'Other Windows display scales/screens remain untested' } else { 'Windows appearance and display scaling pending user review' }),'Physical playback devices and long hardware stability retain existing limits','BT.709 SDR and 8-bit composition; 10-bit storage does not provide HDR','Audio mixer remains 48 kHz stereo','Packaging/redistribution remains Session 16')
} | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath (Join-Path $output 'verification.json') -Encoding utf8
Write-Host "Session 15.2 evidence: $output/verification.json"

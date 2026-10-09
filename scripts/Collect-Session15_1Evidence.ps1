[CmdletBinding()]
param([string]$BuildDirectory = 'build/session-15.1')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $root $BuildDirectory }
$output = Join-Path $root 'evidence/session-15.1'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$previous = Get-Content -LiteralPath (Join-Path $output 'verification.json') -Raw | ConvertFrom-Json
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
}
foreach ($name in $reports.Keys) {
    $source = Join-Path $build $reports[$name]
    if (-not (Get-Content -LiteralPath $source -Raw | ConvertFrom-Json).passed) { throw "Failed suite: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $output $name) -Force
}
if ((Get-Content -LiteralPath (Join-Path $build 'test.log') -Raw) -notmatch '100% tests passed, 0 tests failed out of 16') { throw 'Full sixteen-suite regression evidence missing.' }
foreach ($name in @('configure.log','build.log','test.log')) { Copy-Item -LiteralPath (Join-Path $build $name) -Destination (Join-Path $output $name) -Force }
$images = Join-Path $output 'screenshots'
New-Item -ItemType Directory -Path $images -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $build 'workspace-test-data') -Filter '*.png' | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $images -Force }
$changes = foreach ($entry in $previous.changedExistingFiles) {
    [ordered]@{path=$entry.path;before=$entry.before;after=(Get-FileHash -LiteralPath (Join-Path $root $entry.path) -Algorithm SHA256).Hash}
}
$behaviorDirectories = @('src/project','src/playback')
$behaviorFiles = @('src/timeline/Timeline.cpp','src/export/Capabilities.cpp','src/export/EncoderConfig.cpp','src/export/EncoderControls.cpp','src/export/ExportWorker.cpp')
foreach ($entry in $previous.sources) {
    $relative = $entry.path.Replace('\','/')
    $protected = $relative -in $behaviorFiles
    foreach ($directory in $behaviorDirectories) { if ($relative.StartsWith($directory + '/')) { $protected = $true } }
    if ($protected -and (Get-FileHash -LiteralPath (Join-Path $root $entry.path) -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Unexpected behavior change since recorded verification: $relative" }
}
$originals = foreach ($entry in (Get-Content -LiteralPath (Join-Path $root 'evidence/session-0/all-media/inventory.json') -Raw | ConvertFrom-Json).files) {
    $file = Get-Item -LiteralPath $entry.path
    [ordered]@{path=$entry.path;unchanged=($file.Length -eq $entry.sizeBytes -and $file.LastWriteTimeUtc -eq [datetime]$entry.lastWriteTimeUtc)}
}
if (@($originals | Where-Object { -not $_.unchanged }).Count) { throw 'Original source-media size/mtime drift.' }
$sources = foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root 'src'),(Join-Path $root 'tests') -Recurse -File) { [ordered]@{path=[IO.Path]::GetRelativePath($root,$file.FullName);sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash} }
$native = Join-Path $output 'native-review.json'
[ordered]@{
    passed=$true;version='0.15.2';schemaVersion=9;collectedAtUtc=[datetime]::UtcNow.ToString('o');automatedSuiteCount=16
    workspaceChecks=(Get-Content -LiteralPath (Join-Path $output 'workspace-result.json') -Raw | ConvertFrom-Json).checkCount
    userAcceptance=Get-Content -LiteralPath (Join-Path $output 'manual-acceptance.json') -Raw | ConvertFrom-Json
    executableSha256=(Get-FileHash -LiteralPath (Join-Path $build 'Release/VideoEditor.exe') -Algorithm SHA256).Hash
    unchangedBehavior=@('project model/schema/persistence','playback/rendering','timeline model/command semantics','capability rules/encoders/export worker')
    changedExistingFiles=@($changes);sources=@($sources);originals=@($originals)
    nativeVisualReview=if(Test-Path -LiteralPath $native){Get-Content -LiteralPath $native -Raw | ConvertFrom-Json}else{'Not recorded'}
    evidenceLevel='Pinned native Windows build; sixteen production/model/media/export regression suites; source hashes; native visual review and user acceptance separately identified'
    limits=@('Other display scales/screens untested','Subjective appearance and perceived AV sync unmeasured','Physical playback devices and long hardware stability retain existing limits','Simple/Advanced export tabs remain Session 15.2')
} | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath (Join-Path $output 'verification.json') -Encoding utf8
Write-Host "Session 15.1 evidence: $output/verification.json"

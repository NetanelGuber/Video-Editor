[CmdletBinding()]
param([string]$BuildDirectory = 'build/session-10')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $root $BuildDirectory }
$output = Join-Path $root 'evidence/session-10'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$reports = @{
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
}
foreach ($name in $reports.Keys) {
    $source = Join-Path $build $reports[$name]
    if (-not (Get-Content -LiteralPath $source -Raw | ConvertFrom-Json).passed) { throw "Failed report: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $output $name) -Force
}
foreach ($name in @('configure.log', 'build.log', 'test.log')) {
    Copy-Item -LiteralPath (Join-Path $build $name) -Destination (Join-Path $output $name) -Force
}
if (Test-Path -LiteralPath (Join-Path $build 'performance-final.log')) {
    Copy-Item -LiteralPath (Join-Path $build 'performance-final.log') -Destination (Join-Path $output 'performance-final.log') -Force
}
foreach ($name in @('offline-timeline.png', 'recovery-dialog.png')) {
    Copy-Item -LiteralPath (Join-Path $build "recovery-test-data/$name") -Destination (Join-Path $output $name) -Force
}
foreach ($name in @('4k-export-first.png', '4k-reference-first.png', '4k-export-ffprobe.json')) {
    Copy-Item -LiteralPath (Join-Path $build "performance-test-data/$name") -Destination (Join-Path $output $name) -Force
}
$failures = [Collections.Generic.List[string]]::new()
if (-not (Test-Path -LiteralPath (Join-Path $output 'benchmark/summary.json'))) { $failures.Add('Run Measure-Session10Playback.ps1 before collecting evidence.') }
else {
    $benchmarks = Get-Content -LiteralPath (Join-Path $output 'benchmark/summary.json') -Raw | ConvertFrom-Json
    if ($benchmarks.samples.Count -ne 45 -or @($benchmarks.samples | Where-Object { -not $_.report.passed }).Count) { $failures.Add('Incomplete or failed native benchmark corpus.') }
}
if ((Get-Content -LiteralPath (Join-Path $output 'smoke-result.json') -Raw | ConvertFrom-Json).version -ne '0.10.0') { $failures.Add('Unexpected application version.') }
$lock = Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$runtimes = foreach ($name in @('avformat-63.dll', 'avcodec-63.dll', 'avutil-61.dll', 'swscale-10.dll', 'swresample-7.dll')) {
    $actual = (Get-FileHash -LiteralPath (Join-Path $build "Release/$name") -Algorithm SHA256).Hash
    $expected = (Get-FileHash -LiteralPath (Join-Path $root "$($lock.packages[1].prefix)/bin/$name") -Algorithm SHA256).Hash
    if ($actual -ne $expected) { $failures.Add("Runtime differs from pin: $name") }
    [ordered]@{ name=$name; sha256=$actual.ToLowerInvariant(); matchesPinnedFile=($actual -eq $expected) }
}
$originals = foreach ($entry in (Get-Content -LiteralPath (Join-Path $root 'evidence/session-0/all-media/inventory.json') -Raw | ConvertFrom-Json).files) {
    $file = Get-Item -LiteralPath $entry.path
    $unchanged = $file.Length -eq $entry.sizeBytes -and $file.LastWriteTimeUtc -eq [datetime]$entry.lastWriteTimeUtc
    if (-not $unchanged) { $failures.Add("Original size/mtime changed: $($entry.path)") }
    [ordered]@{ path=$entry.path; sizeBytes=$file.Length; lastWriteTimeUtc=$file.LastWriteTimeUtc.ToString('o'); matchesSession0=$unchanged }
}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root 'scripts') -Filter '*.ps1' -File) {
    $parseTokens = $null; $parseErrors = $null
    [Management.Automation.Language.Parser]::ParseFile($file.FullName, [ref]$parseTokens, [ref]$parseErrors) | Out-Null
    foreach ($parseError in $parseErrors) { $failures.Add("Script syntax: $($file.Name): $parseError") }
}
$markdown = @((Join-Path $root 'README.md'), (Join-Path $root 'plan.md'), (Join-Path $root 'fixtures/README.md')) + @(Get-ChildItem -LiteralPath (Join-Path $root 'docs') -Filter '*.md' -File | Select-Object -ExpandProperty FullName)
foreach ($file in $markdown) {
    foreach ($match in [regex]::Matches((Get-Content -LiteralPath $file -Raw), '(?<!!)\[[^\]]+\]\(([^)]+)\)')) {
        $target = $match.Groups[1].Value.Split('#')[0]
        if (-not $target -or $target -match '^https?://') { continue }
        $resolved = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $file) $target))
        if ($resolved -eq (Join-Path $output 'verification.json')) { continue }
        if (-not (Test-Path -LiteralPath $resolved)) { $failures.Add("Broken documentation link in ${file}: $target") }
    }
}
$artifacts = foreach ($name in @('VideoEditor.exe', 'PerformanceTests.exe', 'editor_project.lib', 'editor_media.lib', 'editor_timeline_ui.lib')) {
    [ordered]@{ name=$name; sha256=(Get-FileHash -LiteralPath (Join-Path $build "Release/$name") -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$sources = foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root 'src'), (Join-Path $root 'tests') -Recurse -File) {
    [ordered]@{ path=[IO.Path]::GetRelativePath($root, $file.FullName); sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$report = [ordered]@{
    passed=($failures.Count -eq 0); collectedAtUtc=[datetime]::UtcNow.ToString('o'); version='0.10.0'; buildDirectory=$build
    automatedSuites=@('project-persistence', 'application-shell', 'media-import-inspection', 'timeline-editing', 'playback-decode-render', 'timeline-interface', 'audio-title-basics', 'sequence-export', 'project-recovery-relink', 'performance-4k-cache')
    performanceCheckCount=(Get-Content -LiteralPath (Join-Path $output 'performance-result.json') -Raw | ConvertFrom-Json).checkCount
    previewFpsLimit=30; standardVideoQueueMiB=32; lowMemoryVideoQueueMiB=4; mediaAidMemoryMiB=8; diskAidCacheMiB=256
    proxyGeneration='Deferred by measured need gate; original playback meets agreed synthetic 4K targets'
    recoveryCheckCount=(Get-Content -LiteralPath (Join-Path $output 'recovery-result.json') -Raw | ConvertFrom-Json).checkCount
    autosaveSeconds=120; retainedBackups=2; schemaVersion=3
    evidenceLevel='Native build/ten suites, target RX 9070 decode/FBO benchmarks, edited 4K CPU/D3D11VA preview, independent 4K original export; no computer use or human perception claims'
    artifacts=$artifacts; sources=$sources; originals=$originals; deployedFfmpeg=$runtimes
    scriptsParsed=$true; documentationLinksChecked=$true
    limitations=@('latest two-minute snapshot is recovery boundary', 'no power-loss or storage corruption simulation', 'Windows appearance/display scaling unverified', 'synthetic 4K coverage; real phone/HDR and slow network storage remain unverified', 'metadata compatibility is not cryptographic source identity')
    failures=$failures.ToArray()
}
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $output 'verification.json') -Encoding utf8
if ($failures.Count) { throw ($failures -join "`n") }
Write-Host "Session 10 evidence collected: $output. All ten suites passed."


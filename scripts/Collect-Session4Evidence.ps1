[CmdletBinding()]
param([string]$BuildDirectory = 'build/session-4')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $root $BuildDirectory }
$output = Join-Path $root 'evidence/session-4'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$reports = @{
    'project-result.json' = 'project-test-data/project-result.json'
    'smoke-result.json' = 'smoke-data/smoke-result.json'
    'media-result.json' = 'media-test-data/media-result.json'
    'timeline-result.json' = 'timeline-test-data/timeline-result.json'
}
foreach ($name in $reports.Keys) {
    $source = Join-Path $build $reports[$name]
    $report = Get-Content -LiteralPath $source -Raw | ConvertFrom-Json
    if (-not $report.passed) { throw "Cannot collect a failed report: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $output $name) -Force
}
foreach ($name in @('configure.log','build.log','test.log')) {
    Copy-Item -LiteralPath (Join-Path $build $name) -Destination (Join-Path $output $name) -Force
}
$failures = [Collections.Generic.List[string]]::new()
if ((Get-Content -LiteralPath (Join-Path $output 'smoke-result.json') -Raw | ConvertFrom-Json).version -ne '0.4.0') {
    $failures.Add('Unexpected application version in smoke report.')
}
$originals = foreach ($entry in (Get-Content -LiteralPath (Join-Path $root 'evidence/session-0/all-media/inventory.json') -Raw | ConvertFrom-Json).files) {
    $file = Get-Item -LiteralPath $entry.path
    $unchanged = $file.Length -eq $entry.sizeBytes -and $file.LastWriteTimeUtc -eq [datetime]$entry.lastWriteTimeUtc
    if (-not $unchanged) { $failures.Add("Original size/mtime changed: $($entry.path)") }
    [ordered]@{ path=$entry.path; sizeBytes=$file.Length; lastWriteTimeUtc=$file.LastWriteTimeUtc.ToString('o'); matchesSession0=$unchanged }
}
$lock = Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$runtimes = foreach ($name in @('avformat-63.dll','avcodec-63.dll','avutil-61.dll','swscale-10.dll','swresample-7.dll')) {
    $actual = (Get-FileHash -LiteralPath (Join-Path $build "Release/$name") -Algorithm SHA256).Hash
    $expected = (Get-FileHash -LiteralPath (Join-Path $root "$($lock.packages[1].prefix)/bin/$name") -Algorithm SHA256).Hash
    if ($actual -ne $expected) { $failures.Add("Deployed runtime differs from pin: $name") }
    [ordered]@{ name=$name; sha256=$actual.ToLowerInvariant(); matchesPinnedFile=($actual -eq $expected) }
}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root 'scripts') -Filter '*.ps1' -File) {
    $parseTokens = $null; $parseErrors = $null
    [Management.Automation.Language.Parser]::ParseFile($file.FullName, [ref]$parseTokens, [ref]$parseErrors) | Out-Null
    foreach ($parseError in $parseErrors) { $failures.Add("Script syntax: $($file.Name): $parseError") }
}
$markdown = @((Join-Path $root 'README.md'), (Join-Path $root 'plan.md'), (Join-Path $root 'fixtures/README.md')) + @(Get-ChildItem -LiteralPath (Join-Path $root 'docs') -Filter '*.md' -File | Select-Object -ExpandProperty FullName)
foreach ($file in $markdown) {
    $content = Get-Content -LiteralPath $file -Raw
    foreach ($match in [regex]::Matches($content, '(?<!!)\[[^\]]+\]\(([^)]+)\)')) {
        $target = $match.Groups[1].Value.Split('#')[0]
        if (-not $target -or $target -match '^https?://') { continue }
        $resolved = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $file) $target))
        if ($resolved -eq (Join-Path $output 'verification.json')) { continue }
        if (-not (Test-Path -LiteralPath $resolved)) { $failures.Add("Broken documentation link in ${file}: $target") }
    }
}
$artifacts = foreach ($name in @('VideoEditor.exe','TimelineTests.exe','editor_timeline.lib')) {
    [ordered]@{ name=$name; sha256=(Get-FileHash -LiteralPath (Join-Path $build "Release/$name") -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$report = [ordered]@{
    passed=($failures.Count -eq 0)
    collectedAtUtc=[datetime]::UtcNow.ToString('o')
    version='0.4.0'
    buildDirectory=$build
    buildEvidence='clean pinned MSVC/CMake Release configure and build; subsequent incremental UI text update'
    automatedSuites=@('project-persistence','application-shell (offscreen)','media-import-inspection (offscreen and real sources)','timeline-editing (native model/filesystem)')
    timelineCheckCount=(Get-Content -LiteralPath (Join-Path $output 'timeline-result.json') -Raw | ConvertFrom-Json).checkCount
    fixtureSha256=(Get-FileHash -LiteralPath (Join-Path $root 'fixtures/projects/editing-v2.veproject') -Algorithm SHA256).Hash.ToLowerInvariant()
    artifacts=$artifacts
    originals=$originals
    deployedFfmpeg=$runtimes
    scriptsParsed=$true
    documentationLinksChecked=$true
    humanAcceptance='not required for model-only Session 4; no visual acceptance claimed'
    limitations=@('no interactive timeline controls (Session 6)','no playback/rendering/GPU validation (Session 5)','source endpoints round to integer ticks; no hidden sub-tick anchor','history snapshots have not been performance benchmarked')
    failures=$failures.ToArray()
}
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $output 'verification.json') -Encoding utf8
if ($failures.Count) { throw ($failures -join "`n") }
Write-Host "Session 4 evidence collected: $output. All four suites passed; original size/mtime and pinned FFmpeg deployment checks pass."

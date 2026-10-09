[CmdletBinding()]
param([string]$BuildDirectory = 'build/session-12')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $root $BuildDirectory }
$output = Join-Path $root 'evidence/session-12'
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
    'effects-result.json' = 'effects-test-data/effects-result.json'
    'advanced-effects-result.json' = 'advanced-effects-test-data/advanced-effects-result.json'
}
foreach ($name in $reports.Keys) {
    $source = Join-Path $build $reports[$name]
    if (-not (Get-Content -LiteralPath $source -Raw | ConvertFrom-Json).passed) { throw "Failed report: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $output $name) -Force
}
foreach ($name in @('configure.log', 'build.log', 'test.log')) {
    if (Test-Path -LiteralPath (Join-Path $build $name)) { Copy-Item -LiteralPath (Join-Path $build $name) -Destination (Join-Path $output $name) -Force }
}
Copy-Item -LiteralPath (Join-Path $build 'Testing/Temporary/LastTest.log') -Destination (Join-Path $output 'ctest-detailed.log') -Force
foreach ($name in @('color-dialog.png', 'speed-dialog.png', 'mask-dialog.png', 'chromaKey-dialog.png', 'lut-dialog.png')) {
    Copy-Item -LiteralPath (Join-Path $build "advanced-effects-test-data/$name") -Destination (Join-Path $output $name) -Force
}
$failures = [Collections.Generic.List[string]]::new()
if ((Get-Content -LiteralPath (Join-Path $output 'smoke-result.json') -Raw | ConvertFrom-Json).version -ne '0.12.0') { $failures.Add('Unexpected application version.') }
if ((Get-Content -LiteralPath (Join-Path $build 'test.log') -Raw) -notmatch '100% tests passed, 0 tests failed out of 12') { $failures.Add('Complete twelve-suite regression evidence is missing.') }
$lock = Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$runtimes = foreach ($name in @('avformat-63.dll', 'avcodec-63.dll', 'avutil-61.dll', 'swscale-10.dll', 'swresample-7.dll', 'avfilter-12.dll', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6OpenGL.dll', 'Qt6OpenGLWidgets.dll')) {
    $actual = (Get-FileHash -LiteralPath (Join-Path $build "Release/$name") -Algorithm SHA256).Hash
    $package = if ($name.StartsWith('Qt')) { $lock.packages[0] } else { $lock.packages[1] }
    $expected = (Get-FileHash -LiteralPath (Join-Path $root "$($package.prefix)/bin/$name") -Algorithm SHA256).Hash
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
$artifacts = foreach ($name in @('VideoEditor.exe', 'AdvancedEffectsTests.exe', 'EffectsTests.exe', 'editor_project.lib', 'editor_playback.lib', 'editor_export.lib', 'editor_timeline.lib', 'editor_timeline_ui.lib')) {
    [ordered]@{ name=$name; sha256=(Get-FileHash -LiteralPath (Join-Path $build "Release/$name") -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$sources = foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root 'src'), (Join-Path $root 'tests') -Recurse -File) {
    [ordered]@{ path=[IO.Path]::GetRelativePath($root, $file.FullName); sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$advanced = Get-Content -LiteralPath (Join-Path $output 'advanced-effects-result.json') -Raw | ConvertFrom-Json
$session12AcceptancePath = Join-Path $output 'manual-acceptance.json'
$session12Accepted = (Test-Path -LiteralPath $session12AcceptancePath) -and (Get-Content -LiteralPath $session12AcceptancePath -Raw | ConvertFrom-Json).passed
$report = [ordered]@{
    passed=($failures.Count -eq 0); collectedAtUtc=[datetime]::UtcNow.ToString('o'); version='0.12.0'; schemaVersion=4; buildDirectory=$build
    automatedSuiteCount=12; advancedCheckCount=$advanced.checks; worstExportMeanRgbError=$advanced.worstExportMeanRgbError
    newEffectTypes=@('color', 'lut', 'speed', 'mask', 'chromaKey'); audioPolicy='Preserve pitch, user-selected'
    audioMeasurements=$advanced.audioMeasurements; speedRange=@(0.125,8); maximumSpeedKeys=256
    maximumConcurrentVideoLayers=8; maximumEffectsPerClip=64
    evidenceLevel='Native build/twelve suites, analytical pixel/time references, production Qt offscreen dialogs/preview, independent MP4/AAC and spectral pitch/transient measurements. No computer use or human visual acceptance.'
    artifacts=$artifacts; sources=$sources; originals=$originals; deployedRuntimes=$runtimes
    scriptsParsed=$true; documentationLinksChecked=$true; userVisualAcceptance='not separately reported'
    sessionCompletion=$(if ($session12Accepted) { 'User-authorized complete' } else { 'Pending user acceptance' })
    limitations=@('Encoded 8-bit SDR creative correction; no professional color management', 'pitch-preserving time stretch has perceptual artifacts and approximate ramp transient timing', 'late seeks decode retimed audio from clip start and may exceed prior seek target', 'Windows scaling, perceived AV sync, real HDR/log and sustained effects-heavy 4K unverified', 'queue limits exclude codec/effect/filter allocations', 'original preservation checked by size and mtime')
    failures=$failures.ToArray()
}
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $output 'verification.json') -Encoding utf8
if ($failures.Count) { throw ($failures -join "`n") }
Write-Host "Session 12 evidence collected: $output. All twelve suites passed."

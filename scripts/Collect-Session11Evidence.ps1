[CmdletBinding()]
param([string]$BuildDirectory = 'build/session-11')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $root $BuildDirectory }
$output = Join-Path $root 'evidence/session-11'
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
}
foreach ($name in $reports.Keys) {
    $source = Join-Path $build $reports[$name]
    if (-not (Get-Content -LiteralPath $source -Raw | ConvertFrom-Json).passed) { throw "Failed report: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $output $name) -Force
}
foreach ($name in @('configure.log', 'build.log', 'test.log', 'effects-final.log')) {
    if (Test-Path -LiteralPath (Join-Path $build $name)) { Copy-Item -LiteralPath (Join-Path $build $name) -Destination (Join-Path $output $name) -Force }
}
foreach ($name in @('effects-dialog.png', 'ffprobe-effects.json')) {
    Copy-Item -LiteralPath (Join-Path $build "effects-test-data/$name") -Destination (Join-Path $output $name) -Force
}
$failures = [Collections.Generic.List[string]]::new()
if ((Get-Content -LiteralPath (Join-Path $output 'smoke-result.json') -Raw | ConvertFrom-Json).version -ne '0.11.0') { $failures.Add('Unexpected application version.') }
if ((Get-Content -LiteralPath (Join-Path $build 'test.log') -Raw) -notmatch '100% tests passed, 0 tests failed out of 11') { $failures.Add('Complete eleven-suite regression evidence is missing.') }
$lock = Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$runtimes = foreach ($name in @('avformat-63.dll', 'avcodec-63.dll', 'avutil-61.dll', 'swscale-10.dll', 'swresample-7.dll', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6OpenGL.dll', 'Qt6OpenGLWidgets.dll')) {
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
$artifacts = foreach ($name in @('VideoEditor.exe', 'EffectsTests.exe', 'PerformanceTests.exe', 'editor_project.lib', 'editor_playback.lib', 'editor_export.lib', 'editor_timeline.lib', 'editor_timeline_ui.lib')) {
    [ordered]@{ name=$name; sha256=(Get-FileHash -LiteralPath (Join-Path $build "Release/$name") -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$sources = foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root 'src'), (Join-Path $root 'tests') -Recurse -File) {
    [ordered]@{ path=[IO.Path]::GetRelativePath($root, $file.FullName); sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$report = [ordered]@{
    passed=($failures.Count -eq 0); collectedAtUtc=[datetime]::UtcNow.ToString('o'); version='0.11.0'; schemaVersion=4; buildDirectory=$build
    automatedSuites=@('project-persistence', 'application-shell', 'media-import-inspection', 'timeline-editing', 'playback-decode-render', 'timeline-interface', 'audio-title-basics', 'sequence-export', 'project-recovery-relink', 'performance-4k-cache', 'effects-keyframes-fades')
    effectsCheckCount=(Get-Content -LiteralPath (Join-Path $output 'effects-result.json') -Raw | ConvertFrom-Json).checks
    effectTypes=@('transform', 'crop', 'opacity', 'composite', 'videoFade'); curves=@('hold', 'linear', 'eased')
    trimSplitPolicy='Content-relative animation; split retains only original outer-edge fades'
    maximumConcurrentVideoLayers=8; maximumEffectsPerClip=64; previewFpsLimit=30
    standardVideoQueueMiB=32; lowMemoryVideoQueueMiB=4; perLayerPreviewQueueFloorMiB=1; perLayerExportQueueMiB=64
    evidenceLevel='Native build/eleven suites, deterministic model and analytical raster references, production Qt offscreen dialogs/multilayer playback, independent H.264 frame/metadata inspection and CPU/D3D11VA synthetic 4K regression. No computer use or human visual acceptance.'
    artifacts=$artifacts; sources=$sources; originals=$originals; deployedRuntimes=$runtimes
    scriptsParsed=$true; documentationLinksChecked=$true
    limitations=@('Windows interaction/display scaling and perceived cadence unverified', 'raster SDR compositing; no professional color management', 'real phone/HDR sources, sustained multilayer 4K and slow storage unmeasured', 'converted queue budgets exclude codec references and composition images', 'brief pipeline loading at video membership changes', 'original preservation checked by size and mtime against Session 0')
    failures=$failures.ToArray()
}
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $output 'verification.json') -Encoding utf8
if ($failures.Count) { throw ($failures -join "`n") }
Write-Host "Session 11 evidence collected: $output. All eleven suites passed."

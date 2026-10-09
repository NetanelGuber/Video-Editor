[CmdletBinding()]
param([string]$BuildDirectory = 'build/1.0.1')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$lock = Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $root $BuildDirectory }
$ctest = Join-Path (Split-Path -Parent $lock.buildTools.cmake.path) 'ctest.exe'
if (-not (Test-Path -LiteralPath (Join-Path $build 'Release/VideoEditor.exe'))) { throw 'Build the application with scripts/Build.ps1 first.' }
& (Join-Path $PSScriptRoot 'Prepare-Session3Tests.ps1')
& (Join-Path $PSScriptRoot 'Prepare-Session5Tests.ps1')
& (Join-Path $PSScriptRoot 'Prepare-Session7Tests.ps1')
$previousCache = $env:VIDEO_EDITOR_CACHE_DIR
try {
    $env:VIDEO_EDITOR_CACHE_DIR = Join-Path $build 'isolated-media-cache'
    & $ctest --test-dir $build -C Release --output-on-failure 2>&1 | Tee-Object -FilePath (Join-Path $build 'test.log')
} finally { $env:VIDEO_EDITOR_CACHE_DIR = $previousCache }
if ($LASTEXITCODE -ne 0) { throw 'Application/project verification failed; see test.log, project-test-data, and smoke-data/logs.' }
$result = Get-Content -LiteralPath (Join-Path $build 'smoke-data/smoke-result.json') -Raw | ConvertFrom-Json
if (-not $result.passed) { throw 'Smoke report indicates failure.' }
$projectResult = Get-Content -LiteralPath (Join-Path $build 'project-test-data/project-result.json') -Raw | ConvertFrom-Json
if (-not $projectResult.passed) { throw 'Project persistence report indicates failure.' }
$mediaResultPath = Join-Path $build 'media-test-data/media-result.json'
if (Test-Path -LiteralPath (Join-Path $build 'Release/MediaTests.exe')) {
    $mediaResult = Get-Content -LiteralPath $mediaResultPath -Raw | ConvertFrom-Json
    if (-not $mediaResult.passed) { throw 'Media inspection/import report indicates failure.' }
    Write-Host "Media inspection/import/relink/cancellation checks passed. Report: $mediaResultPath"
}
Write-Host "Project persistence and offscreen application lifecycle checks passed. Reports: $build\project-test-data\project-result.json and $build\smoke-data\smoke-result.json"
$timelineResultPath = Join-Path $build 'timeline-test-data/timeline-result.json'
$timelineResult = Get-Content -LiteralPath $timelineResultPath -Raw | ConvertFrom-Json
if (-not $timelineResult.passed) { throw 'Timeline editing report indicates failure.' }
Write-Host "Deterministic timeline and undo/redo checks passed. Report: $timelineResultPath"
$playbackResultPath = Join-Path $build 'playback-test-data/playback-result.json'
$playbackResult = Get-Content -LiteralPath $playbackResultPath -Raw | ConvertFrom-Json
if (-not $playbackResult.passed) { throw 'Playback/decode report indicates failure.' }
Write-Host "Playback/decode checks passed. Report: $playbackResultPath"
$timelineUiResultPath = Join-Path $build 'timeline-ui-test-data/timeline-ui-result.json'
if (-not (Get-Content -LiteralPath $timelineUiResultPath -Raw | ConvertFrom-Json).passed) { throw 'Timeline interface report indicates failure.' }
Write-Host "Offscreen timeline mouse/drop/keyboard, live viewer and save/reopen checks passed. Report: $timelineUiResultPath"
$audioTitlePath = Join-Path $build 'audio-title-test-data/audio-title-result.json'
if (-not (Get-Content -LiteralPath $audioTitlePath -Raw | ConvertFrom-Json).passed) { throw 'Audio/title report indicates failure.' }
Write-Host "Sample-exact audio mix and offscreen title/audio properties checks passed. Report: $audioTitlePath"
$exportPath = Join-Path $build 'export-test-data/export-result.json'
if (-not (Get-Content -LiteralPath $exportPath -Raw | ConvertFrom-Json).passed) { throw 'Sequence export report indicates failure.' }
Write-Host "Independent export metadata/frame/audio checks, atomic cancellation/retry and offscreen export controls passed. Report: $exportPath"
$recoveryPath = Join-Path $build 'recovery-test-data/recovery-result.json'
if (-not (Get-Content -LiteralPath $recoveryPath -Raw | ConvertFrom-Json).passed) { throw 'Recovery/relink report indicates failure.' }
Write-Host "Forced termination/restart, production recovery dialogs, two-backup retention and folder relinking passed. Report: $recoveryPath"
$performancePath = Join-Path $build 'performance-test-data/performance-result.json'
if (-not (Get-Content -LiteralPath $performancePath -Raw | ConvertFrom-Json).passed) { throw 'Session 10 performance/cache report indicates failure.' }
Write-Host "4K edited preview/export, cache invalidation and memory/cancellation checks passed. Report: $performancePath"
$effectsPath = Join-Path $build 'effects-test-data/effects-result.json'
if (-not (Get-Content -LiteralPath $effectsPath -Raw | ConvertFrom-Json).passed) { throw 'Effects/keyframes/fades report indicates failure.' }
Write-Host "Effect stack/keyframe/fade boundaries, native dialogs, multilayer preview and independent exports passed. Report: $effectsPath"
$advancedPath = Join-Path $build 'advanced-effects-test-data/advanced-effects-result.json'
if (-not (Get-Content -LiteralPath $advancedPath -Raw | ConvertFrom-Json).passed) { throw 'Color/speed/mask report indicates failure.' }
Write-Host "Color/LUT/mask/chroma references, speed remapping, pitch-preserving audio, native controls and independent exports passed. Report: $advancedPath"
$sequencesPath = Join-Path $build 'sequences-test-data/sequences-result.json'
if (-not (Get-Content -LiteralPath $sequencesPath -Raw | ConvertFrom-Json).passed) { throw 'Nested/multicamera report indicates failure.' }
Write-Host "Nested source mapping/cycles, camera switching, native controls and independent video/audio exports passed. Report: $sequencesPath"
$capabilityPath = Join-Path $build 'capability-test-data/capabilities-result.json'
if (-not (Get-Content -LiteralPath $capabilityPath -Raw | ConvertFrom-Json).passed) { throw 'Export capability/profile report indicates failure.' }
Write-Host "Runtime export discovery, exact driver probes, immediate conflict controls and independent profile outputs passed. Report: $capabilityPath"
$catalogPath = Join-Path $build 'encoder-catalog-test-data/encoder-catalog-result.json'
if (-not (Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json).passed) { throw 'Every-encoder catalog report indicates failure.' }
Write-Host "Every bundled encoder, native controls, production UI adaptation, persistence and independent codec/audio exports passed. Report: $catalogPath"
$workspacePath = Join-Path $build 'workspace-test-data/workspace-result.json'
if (-not (Get-Content -LiteralPath $workspacePath -Raw | ConvertFrom-Json).passed) { throw 'Workspace organization report indicates failure.' }
Write-Host "Production workspace menus, Inspector edits, media states, narrow layout, dialog fallback and export feedback passed. Report: $workspacePath"
$tabsPath = Join-Path $build 'export-tabs-test-data/export-tabs-result.json'
if (-not (Get-Content -LiteralPath $tabsPath -Raw | ConvertFrom-Json).passed) { throw 'Simple/Advanced export report indicates failure.' }
Write-Host "Production export tabs, exact FPS, stale warnings, audio bitrate, persistence and independent exports passed. Report: $tabsPath"

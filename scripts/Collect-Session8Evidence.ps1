[CmdletBinding()]
param([string]$BuildDirectory = 'build/session-8')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $root $BuildDirectory }
$output = Join-Path $root 'evidence/session-8'
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
}
foreach ($name in $reports.Keys) {
    $source = Join-Path $build $reports[$name]
    if (-not (Get-Content -LiteralPath $source -Raw | ConvertFrom-Json).passed) { throw "Failed report: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $output $name) -Force
}
foreach ($name in @('configure.log','build.log','test.log')) {
    Copy-Item -LiteralPath (Join-Path $build $name) -Destination (Join-Path $output $name) -Force
}
$failures = [Collections.Generic.List[string]]::new()
if ((Get-Content -LiteralPath (Join-Path $output 'smoke-result.json') -Raw | ConvertFrom-Json).version -ne '0.8.0') { $failures.Add('Unexpected application version.') }
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
    if ($actual -ne $expected) { $failures.Add("Runtime differs from pin: $name") }
    [ordered]@{ name=$name; sha256=$actual.ToLowerInvariant(); matchesPinnedFile=($actual -eq $expected) }
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
$artifacts = foreach ($name in @('VideoEditor.exe','TimelineUiTests.exe','editor_timeline_ui.lib','editor_timeline.lib','editor_playback.lib','AudioTitleTests.exe','editor_export.lib','ExportTests.exe')) {
    [ordered]@{ name=$name; sha256=(Get-FileHash -LiteralPath (Join-Path $build "Release/$name") -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$sources = foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root 'src'), (Join-Path $root 'tests') -Recurse -File) {
    [ordered]@{ path=[IO.Path]::GetRelativePath($root, $file.FullName); sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
Copy-Item -LiteralPath (Join-Path $build 'audio-title-test-data/styled-title.png') -Destination (Join-Path $output 'styled-title.png') -Force
foreach ($name in @('ffprobe-reference.json','export-title.png','reference-title.png')) {
    Copy-Item -LiteralPath (Join-Path $build ('export-test-data/' + $name)) -Destination (Join-Path $output $name) -Force
}
$exportReport = Get-Content -LiteralPath (Join-Path $output 'export-result.json') -Raw | ConvertFrom-Json
$renderedOutputs = foreach ($path in @($exportReport.exports | Where-Object passed | Select-Object -ExpandProperty path | Sort-Object -Unique)) {
    $file = Get-Item -LiteralPath $path
    [ordered]@{ path=$file.FullName; sizeBytes=$file.Length; sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$humanAcceptance = 'pending user Windows and separate-player playback/sync checks; no computer-use automation or visual acceptance claimed'
$acceptancePath = Join-Path $output 'manual-acceptance.json'
if (Test-Path -LiteralPath $acceptancePath) {
    $acceptance = Get-Content -LiteralPath $acceptancePath -Raw | ConvertFrom-Json
    if ($acceptance.passed -and $acceptance.session -eq 8 -and $acceptance.version -eq '0.8.0') {
        $humanAcceptance = 'User-reported Session 8 acceptance for 0.8.0; individual checklist outcomes were not separately reported; no agent computer use'
    }
}
$report = [ordered]@{
    passed=($failures.Count -eq 0); collectedAtUtc=[datetime]::UtcNow.ToString('o'); version='0.8.0'; buildDirectory=$build
    automatedSuites=@('project-persistence','application-shell','media-import-inspection','timeline-editing','playback-decode-render','timeline-interface','audio-title-basics','sequence-export')
    exportCheckCount=(Get-Content -LiteralPath (Join-Path $output 'export-result.json') -Raw | ConvertFrom-Json).checkCount
    audioTitleCheckCount=(Get-Content -LiteralPath (Join-Path $output 'audio-title-result.json') -Raw | ConvertFrom-Json).checkCount
    timelineUiCheckCount=(Get-Content -LiteralPath (Join-Path $output 'timeline-ui-result.json') -Raw | ConvertFrom-Json).checkCount
    humanAcceptance=$humanAcceptance
    artifacts=$artifacts; sources=$sources; renderedOutputs=$renderedOutputs; originals=$originals; deployedFfmpeg=$runtimes
    scriptsParsed=$true; documentationLinksChecked=$true
    limitations=@('one active video source; no video compositing','48 kHz stereo preview mix with final hard clipping; up to 64 simultaneous audio sources','Qt title rasterizer with bounded 1280x720 preview; unavailable fonts use platform fallback','edits pause playback and preserve playhead','no linked AV edits or multi-clip drag','8-bit BT.709 SDR CPU export; HDR and color management unverified','AAC tail can pad by up to 1023 samples; changed fps extends coverage by less than one output frame','local MP4 preset without fast-start metadata; enabled effects rejected until Session 11','forced process death can leave an orphan temporary file; normal cancellation preserves destination','long-session/large-project performance remains Session 10')
    failures=$failures.ToArray()
}
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $output 'verification.json') -Encoding utf8
if ($failures.Count) { throw ($failures -join "`n") }
Write-Host "Session 8 evidence collected: $output. Automated checks passed. $humanAcceptance"


[CmdletBinding()]
param([string]$BuildDirectory = 'build/session-5', [string]$CleanBuildDirectory = 'build/session-5-clean')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $root $BuildDirectory }
$clean = if ([IO.Path]::IsPathRooted($CleanBuildDirectory)) { $CleanBuildDirectory } else { Join-Path $root $CleanBuildDirectory }
$output = Join-Path $root 'evidence/session-5'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$reports = @{'project-result.json'='project-test-data/project-result.json'; 'smoke-result.json'='smoke-data/smoke-result.json';
    'media-result.json'='media-test-data/media-result.json'; 'timeline-result.json'='timeline-test-data/timeline-result.json'; 'playback-result.json'='playback-test-data/playback-result.json'}
foreach ($name in $reports.Keys) {
    $source = Join-Path $build $reports[$name]
    $report = Get-Content -LiteralPath $source -Raw | ConvertFrom-Json
    if (-not $report.passed) { throw "Cannot collect failed report: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $output $name) -Force
    $cleanReport = Get-Content -LiteralPath (Join-Path $clean $reports[$name]) -Raw | ConvertFrom-Json
    if (-not $cleanReport.passed) { throw "Clean-build suite failed: $name" }
}
foreach ($name in @('configure.log','build.log','test.log')) {
    Copy-Item -LiteralPath (Join-Path $build $name) -Destination (Join-Path $output $name) -Force
    Copy-Item -LiteralPath (Join-Path $clean $name) -Destination (Join-Path $output "clean-$name") -Force
}
$failures = [Collections.Generic.List[string]]::new()
$originals = foreach ($entry in (Get-Content -LiteralPath (Join-Path $root 'evidence/session-0/all-media/inventory.json') -Raw | ConvertFrom-Json).files) {
    $file = Get-Item -LiteralPath $entry.path
    $matches = $file.Length -eq $entry.sizeBytes -and $file.LastWriteTimeUtc -eq [datetime]$entry.lastWriteTimeUtc
    if (-not $matches) { $failures.Add("Original size/mtime changed: $($entry.path)") }
    [ordered]@{path=$entry.path; matchesSession0=$matches; sizeBytes=$file.Length; lastWriteTimeUtc=$file.LastWriteTimeUtc.ToString('o')}
}
$lock = Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$runtimes = foreach ($name in @('avformat-63.dll','avcodec-63.dll','avutil-61.dll','swscale-10.dll','swresample-7.dll','Qt6OpenGL.dll','Qt6OpenGLWidgets.dll')) {
    $actual = (Get-FileHash -LiteralPath (Join-Path $build "Release/$name") -Algorithm SHA256).Hash
    $package = if ($name -like 'Qt6*') { $lock.packages[0] } else { $lock.packages[1] }
    $expected = (Get-FileHash -LiteralPath (Join-Path $root "$($package.prefix)/bin/$name") -Algorithm SHA256).Hash
    if ($actual -ne $expected) { $failures.Add("Runtime differs from pinned file: $name") }
    [ordered]@{name=$name; sha256=$actual.ToLowerInvariant(); matchesPinnedFile=($actual -eq $expected)}
}
$artifacts = foreach ($name in @('VideoEditor.exe','PlaybackTests.exe','PlaybackBenchmark.exe','editor_playback.lib')) {
    [ordered]@{name=$name; sha256=(Get-FileHash -LiteralPath (Join-Path $build "Release/$name") -Algorithm SHA256).Hash.ToLowerInvariant()}
}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root 'scripts') -Filter '*.ps1' -File) {
    $parseTokens = $null; $parseErrors = $null
    [Management.Automation.Language.Parser]::ParseFile($file.FullName, [ref]$parseTokens, [ref]$parseErrors) | Out-Null
    foreach ($parseError in $parseErrors) { $failures.Add("Script syntax: $($file.Name): $parseError") }
}
$markdown = @((Join-Path $root 'README.md'),(Join-Path $root 'plan.md'),(Join-Path $root 'fixtures/README.md')) +
    @(Get-ChildItem -LiteralPath (Join-Path $root 'docs') -Filter '*.md' -File | Select-Object -ExpandProperty FullName)
foreach ($file in $markdown) {
    foreach ($match in [regex]::Matches((Get-Content -LiteralPath $file -Raw), '(?<!!)\[[^\]]+\]\(([^)]+)\)')) {
        $target = $match.Groups[1].Value.Split('#')[0]
        if (-not $target -or $target -match '^https?://') { continue }
        $resolved = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $file) $target))
        if ($resolved -eq (Join-Path $output 'verification.json')) { continue }
        if (-not (Test-Path -LiteralPath $resolved)) { $failures.Add("Broken link in ${file}: $target") }
    }
}
$benchmark = Get-Content -LiteralPath (Join-Path $output 'benchmark/summary.json') -Raw | ConvertFrom-Json
foreach ($sample in $benchmark.samples) { if (-not $sample.passed) { $failures.Add("Benchmark failed: $($sample.path)") } }
if (-not $benchmark.stability.passed) { $failures.Add('Twenty-second stability benchmark failed.') }
$encode = Get-Content -LiteralPath (Join-Path $output 'encoders/capabilities.json') -Raw | ConvertFrom-Json
$smoke = Get-Content -LiteralPath (Join-Path $output 'smoke-result.json') -Raw | ConvertFrom-Json
if ($smoke.version -ne '0.5.0') { $failures.Add('Unexpected app version.') }
$acceptancePath = Join-Path $output 'manual-acceptance.json'
$humanAcceptance = 'Pending user checklist in docs/session-5.md; no computer-use automation'
if (Test-Path -LiteralPath $acceptancePath) {
    $acceptance = Get-Content -LiteralPath $acceptancePath -Raw | ConvertFrom-Json
    if ($acceptance.passed -and $acceptance.session -eq 5 -and $acceptance.version -eq '0.5.0') {
        $humanAcceptance = 'User accepted Session 5; individual checklist outcomes not separately reported. See manual-acceptance.json; no computer-use automation.'
    }
}
[ordered]@{passed=($failures.Count -eq 0); collectedAtUtc=[datetime]::UtcNow.ToString('o'); version='0.5.0';
    buildDirectory=$build; cleanBuildDirectory=$clean; automatedSuites=@('project-persistence','application-shell','media-import-inspection','timeline-editing','playback-decode-render');
    playbackCheckCount=(Get-Content -LiteralPath (Join-Path $output 'playback-result.json') -Raw | ConvertFrom-Json).checkCount;
    buildEvidence='Pinned Release build plus independent clean configure/build and all five suites in both directories';
    renderingEvidence='Actual AMD D3D11VA and hardware OpenGL offscreen FBO; visible interaction not agent-verified';
    audioEvidence='Real default WASAPI endpoint in automated offscreen pause/resume test; audibility/perceived sync not agent-verified';
    encoders=$encode.encoders | ForEach-Object { [ordered]@{name=$_.name; usable=$_.usable} };
    humanAcceptance=$humanAcceptance;
    originals=$originals; artifacts=$artifacts; deployedRuntimes=$runtimes; scriptsParsed=$true; documentationLinksChecked=$true;
    limitations=@('single active video/audio preview; no title/effect evaluation yet','1280x720 SDR preview; CPU transfer/conversion after D3D11VA',
        'cuts can buffer while pipeline restarts','short benchmarks and synthetic 4K/rotation/VFR; no real phone/HDR acceptance','no offline export evaluator yet'); failures=$failures.ToArray()} |
    ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $output 'verification.json') -Encoding utf8
if ($failures.Count) { throw ($failures -join "`n") }
Write-Host "Session 5 evidence collected: $output. $humanAcceptance"

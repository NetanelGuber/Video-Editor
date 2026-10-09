[CmdletBinding()]
param([string]$BuildDirectory = 'build/session-3-clean')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $root $BuildDirectory }
$output = Join-Path $root 'evidence/session-3'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$reports = @{
    'project-result.json' = 'project-test-data/project-result.json'
    'smoke-result.json' = 'smoke-data/smoke-result.json'
    'media-result.json' = 'media-test-data/media-result.json'
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
New-Item -ItemType Directory -Path (Join-Path $output 'samples') -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $build 'media-test-data') -Filter 'sample-*' -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $output 'samples') -Force
}
$mediaLog = Get-ChildItem -LiteralPath (Join-Path $build 'media-test-data/logs') -File | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
Copy-Item -LiteralPath $mediaLog.FullName -Destination (Join-Path $output 'media-application.log') -Force
$shellLog = Get-ChildItem -LiteralPath (Join-Path $build 'smoke-data/logs') -File | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
Copy-Item -LiteralPath $shellLog.FullName -Destination (Join-Path $output 'application.log') -Force

$failures = [Collections.Generic.List[string]]::new()
$originals = foreach ($entry in (Get-Content -LiteralPath (Join-Path $root 'evidence/session-0/all-media/inventory.json') -Raw | ConvertFrom-Json).files) {
    $file = Get-Item -LiteralPath $entry.path
    $unchanged = $file.Length -eq $entry.sizeBytes -and $file.LastWriteTimeUtc -eq [datetime]$entry.lastWriteTimeUtc
    if (-not $unchanged) { $failures.Add("Original size/mtime changed: $($entry.path)") }
    [ordered]@{ path=$entry.path; sizeBytes=$file.Length; lastWriteTimeUtc=$file.LastWriteTimeUtc.ToString('o'); matchesSession0=$unchanged }
}
$hashes = foreach ($entry in (Get-Content -LiteralPath (Join-Path $root 'evidence/session-0/test-set/inventory.json') -Raw | ConvertFrom-Json).files) {
    $hash = (Get-FileHash -LiteralPath $entry.path -Algorithm SHA256).Hash.ToLowerInvariant()
    $unchanged = $hash -eq $entry.sha256
    if (-not $unchanged) { $failures.Add("Original sample hash changed: $($entry.path)") }
    [ordered]@{ path=$entry.path; sha256=$hash; matchesSession0=$unchanged }
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
        $resolvedTarget = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $file) $target))
        # The verification document itself is written immediately below.
        if ($resolvedTarget -eq (Join-Path $output 'verification.json')) { continue }
        if (-not (Test-Path -LiteralPath $resolvedTarget)) { $failures.Add("Broken documentation link in ${file}: $target") }
    }
}
$humanAcceptance = 'pending; user must perform docs/session-3.md checklist'
$acceptancePath = Join-Path $output 'manual-acceptance.json'
if (Test-Path -LiteralPath $acceptancePath) {
    $acceptance = Get-Content -LiteralPath $acceptancePath -Raw | ConvertFrom-Json
    $testedVersion = (Get-Content -LiteralPath (Join-Path $build 'smoke-data/smoke-result.json') -Raw | ConvertFrom-Json).version
    if ($acceptance.passed -and $acceptance.version -eq $testedVersion) {
        $humanAcceptance = "accepted by the user on $($acceptance.date); see evidence/session-3/manual-acceptance.json"
    }
}
$report = [ordered]@{
    passed=($failures.Count -eq 0)
    collectedAtUtc=[datetime]::UtcNow.ToString('o')
    buildDirectory=$build
    executableSha256=(Get-FileHash -LiteralPath (Join-Path $build 'Release/VideoEditor.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
    originals=$originals
    selectedOriginalHashes=$hashes
    deployedFfmpeg=$runtimes
    scriptsParsed=$true
    documentationLinksChecked=$true
    humanWindowsAcceptance=$humanAcceptance
    failures=$failures.ToArray()
}
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $output 'verification.json') -Encoding utf8
if ($failures.Count) { throw ($failures -join "`n") }
Write-Host "Session 3 evidence collected: $output. Original size/mtime and six SHA-256 checks pass; deployed FFmpeg matches pinned files."

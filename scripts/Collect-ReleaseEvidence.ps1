#requires -Version 7.0
[CmdletBinding()]
param([string]$BuildDirectory = 'build/1.0.1', [string]$PackageTestDirectory = 'build/1.0.1-package-test')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
function Absolute([string]$path) {
    if ([IO.Path]::IsPathRooted($path)) { [IO.Path]::GetFullPath($path) }
    else { [IO.Path]::GetFullPath((Join-Path $root $path)) }
}
$build = Absolute $BuildDirectory
$packageTest = Absolute $PackageTestDirectory
$version = (Get-Content -LiteralPath (Join-Path $build 'generated/BuildInfo.h') |
    Select-String '^#define EDITOR_VERSION "([^"]+)"').Matches.Groups[1].Value
if (-not $version) { throw 'Missing generated application version.' }
$output = Join-Path $root "evidence/release-$version"
if (Test-Path -LiteralPath $output) { throw 'Release evidence already exists; preserve the previous record.' }
$testLog = Get-Content -LiteralPath (Join-Path $build 'test.log') -Raw
if ($testLog -notmatch '100% tests passed, 0 tests failed out of (\d+)') { throw 'A passing full regression is required.' }
$suites = [int]$Matches[1]
$package = Get-Content -LiteralPath (Join-Path $packageTest 'package-result.json') -Raw | ConvertFrom-Json
if (-not $package.passed -or $package.version -ne $version) { throw 'Passing package verification with the same version is required.' }
if ((Get-FileHash -LiteralPath $package.archive -Algorithm SHA256).Hash -ne $package.archiveSha256) { throw 'Verified archive changed.' }
$folders = @(Get-ChildItem -LiteralPath $packageTest -Directory | Where-Object {
    Test-Path -LiteralPath (Join-Path $_.FullName 'build/package-manifest.json')
})
if ($folders.Count -ne 1) { throw 'Exactly one verified package is required.' }
$manifestPath = Join-Path $folders[0].FullName 'build/package-manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.version -ne $version -or $manifest.applicationLicense -ne 'GPL-3.0-or-later' -or
    $manifest.externalDistributionApproved -ne $false) { throw 'Draft package identity/license/distribution status differs.' }
$exe = Get-Item -LiteralPath (Join-Path $build 'Release/VideoEditor.exe')
if ($exe.VersionInfo.ProductVersion -ne $version) { throw 'Executable resource version differs.' }
$reports = @(Get-ChildItem -LiteralPath $build -Directory | Where-Object Name -Match '-test-data$|^smoke-data$' |
    ForEach-Object { Get-ChildItem -LiteralPath $_.FullName -Filter '*-result.json' -File })
foreach ($report in $reports) {
    if (-not (Get-Content -LiteralPath $report.FullName -Raw | ConvertFrom-Json).passed) { throw "Failed report: $($report.Name)" }
}
New-Item -ItemType Directory -Path $output | Out-Null
foreach ($report in $reports) { Copy-Item -LiteralPath $report.FullName -Destination (Join-Path $output $report.Name) }
foreach ($log in @('configure.log','build.log','test.log')) {
    Copy-Item -LiteralPath (Join-Path $build $log) -Destination (Join-Path $output $log)
}
Copy-Item -LiteralPath (Join-Path $packageTest 'package-result.json') -Destination (Join-Path $output 'package-result.json')
Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $output 'package-manifest.json')
Copy-Item -LiteralPath (Join-Path $folders[0].FullName 'build/ffmpeg-runtime.json') -Destination (Join-Path $output 'ffmpeg-runtime.json')
[ordered]@{
    passed = $true
    version = $version
    verifiedUtc = [DateTime]::UtcNow.ToString('o')
    archiveName = [IO.Path]::GetFileName($package.archive)
    archiveBytes = (Get-Item -LiteralPath $package.archive).Length
    archiveSha256 = $package.archiveSha256
    applicationExeSha256 = (Get-FileHash -LiteralPath $exe.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    applicationLicense = 'GPL-3.0-or-later'
    regressionSuites = $suites
    packagedWorkflowChecks = $package.workflow.checks.Count
    payloadFiles = $package.payloadFiles
    binaryImportResolutions = $package.binaryImports.Count
    releaseState = 'private-draft'
    externalDistributionApproved = $false
    evidenceLevel = $package.evidenceLevel
    limits = @($package.limits) + @('Unsigned binary', 'Exact dependency corresponding-source/build material pending')
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'verification.json') -Encoding utf8
Write-Host "Release evidence retained: $output"

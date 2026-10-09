#requires -Version 7.0
[CmdletBinding()]
param([string]$BuildDirectory='build/session-16', [string]$PackageTestDirectory='build/session-16-release-test')
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
function Absolute([string]$p) { if ([IO.Path]::IsPathRooted($p)) { $p } else { Join-Path $root $p } }
$build=Absolute $BuildDirectory; $packageTest=Absolute $PackageTestDirectory
$output=Join-Path $root 'evidence/session-16'; New-Item -ItemType Directory -Path $output -Force | Out-Null
$testLog=Get-Content -LiteralPath (Join-Path $build 'test.log') -Raw
if ($testLog -notmatch '100% tests passed, 0 tests failed out of 19') { throw 'A passing nineteen-suite regression is required.' }
$package=Get-Content -LiteralPath (Join-Path $packageTest 'package-result.json') -Raw | ConvertFrom-Json
if (-not $package.passed) { throw 'Passing extracted-package verification is required.' }
$reports=@(Get-ChildItem -LiteralPath $build -Directory | ForEach-Object { Get-ChildItem -LiteralPath $_.FullName -Filter '*-result.json' -File })
foreach ($report in $reports) {
    $result=Get-Content -LiteralPath $report.FullName -Raw | ConvertFrom-Json
    if ($report.Directory.Name -notmatch '-test-data$|smoke-data$') { continue }
    if (-not $result.passed) { throw "Failed report: $($report.FullName)" }
    Copy-Item -LiteralPath $report.FullName -Destination (Join-Path $output $report.Name) -Force
}
foreach ($file in @('configure.log','build.log','test.log')) { Copy-Item -LiteralPath (Join-Path $build $file) -Destination (Join-Path $output $file) -Force }
Copy-Item -LiteralPath (Join-Path $packageTest 'package-result.json') -Destination (Join-Path $output 'package-result.json') -Force
foreach ($log in Get-ChildItem -LiteralPath $packageTest -Filter '*.txt' -File) { Copy-Item -LiteralPath $log.FullName -Destination (Join-Path $output $log.Name) -Force }
$archive=$package.archive
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $package.archiveSha256) { throw 'Archive changed after verification.' }
$folder=Get-ChildItem -LiteralPath $packageTest -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'build/package-manifest.json') } | Select-Object -First 1
Copy-Item -LiteralPath (Join-Path $folder.FullName 'build/package-manifest.json') -Destination (Join-Path $output 'package-manifest.json') -Force
Copy-Item -LiteralPath (Join-Path $folder.FullName 'build/ffmpeg-runtime.json') -Destination (Join-Path $output 'ffmpeg-runtime.json') -Force
$inventory=Get-Content -LiteralPath (Join-Path $root 'evidence/session-0/all-media/inventory.json') -Raw | ConvertFrom-Json
$originals=@($inventory.files)
if ($originals.Count -eq 0) { throw 'No original-footage inventory records found.' }
$originalEvidence=foreach ($file in $originals) {
    $current=Get-Item -LiteralPath $file.path
    if ($current.Length -ne $file.sizeBytes -or $current.LastWriteTimeUtc -ne [datetime]$file.lastWriteTimeUtc) { throw "Original source size/mtime changed: $($file.path)" }
    @{path=$file.path; bytes=$current.Length; lastWriteTimeUtc=$current.LastWriteTimeUtc.ToString('o'); sizeAndMtimeUnchanged=$true}
}
[ordered]@{passed=$true; version=$package.version; verifiedUtc=[DateTime]::UtcNow.ToString('o'); archive=$archive; archiveSha256=$package.archiveSha256;
    regressionSuites=19; packagedWorkflowChecks=$package.workflow.checks.Count; payloadFiles=$package.payloadFiles;
    originals=$originalEvidence; externalDistributionApproved=$false; humanVisualAcceptance='pending'; cleanOsOrNewWindowsAccount='unverified';
    evidenceLevel=$package.evidenceLevel; limits=$package.limits
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'verification.json') -Encoding utf8
Write-Host "Session 16 evidence retained: $output"


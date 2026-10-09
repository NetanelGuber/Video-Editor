[CmdletBinding()]
param([string]$ToolsDirectory = (Join-Path $PSScriptRoot '../.tools'))
$ErrorActionPreference = 'Stop'
$lock = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../dependencies.lock.json') -Raw | ConvertFrom-Json
$cmake = $lock.buildTools.cmake.path
if (-not (Test-Path -LiteralPath $cmake)) { throw "Pinned CMake is missing: $cmake" }
New-Item -ItemType Directory -Path $ToolsDirectory -Force | Out-Null
$toolRoot = (Resolve-Path -LiteralPath $ToolsDirectory).Path
$downloads = Join-Path $toolRoot 'downloads'
New-Item -ItemType Directory -Path $downloads -Force | Out-Null
foreach ($package in $lock.packages) {
    $archive = Join-Path $downloads $package.archiveName
    if (-not (Test-Path -LiteralPath $archive)) {
        Invoke-WebRequest -Uri $package.url -OutFile $archive -TimeoutSec 120
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $package.sha256) {
        throw "Checksum mismatch for $archive. Existing files were left for inspection."
    }
    $destination = Join-Path $toolRoot $package.extractDirectory
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    & $cmake -E chdir $destination $cmake -E tar xf $archive
    if ($LASTEXITCODE -ne 0) { throw "Could not extract $archive" }
    Write-Host "Verified and extracted $($package.name) $($package.version)"
}
Write-Host 'Dependencies are project-local. No PATH, shell profile, or global configuration was changed.'

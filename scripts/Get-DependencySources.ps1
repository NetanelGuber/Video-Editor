#requires -Version 7.0
[CmdletBinding()]
param([string]$OutputDirectory = '.tools/source-releases', [switch]$Offline)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$output = if ([IO.Path]::IsPathRooted($OutputDirectory)) { [IO.Path]::GetFullPath($OutputDirectory) } else { [IO.Path]::GetFullPath((Join-Path $root $OutputDirectory)) }
$inventory = Get-Content -LiteralPath (Join-Path $root 'docs/notices/dependency-sources.json') -Raw | ConvertFrom-Json
$lock = Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Path $output -Force | Out-Null
foreach ($source in $inventory.sources) {
    $archive = Join-Path $output $source.archive
    if (-not (Test-Path -LiteralPath $archive)) {
        if ($Offline) { throw "Missing source archive: $archive" }
        $partial = "$archive.partial"
        if (Test-Path -LiteralPath $partial) { throw "An earlier download exists: $partial. Inspect it before retrying." }
        Invoke-WebRequest -Uri $source.url -OutFile $partial
        if ((Get-Item -LiteralPath $partial).Length -ne $source.bytes -or (Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $source.sha256) { throw "Source download differs from the pin: $partial" }
        Move-Item -LiteralPath $partial -Destination $archive
    }
    if ((Get-Item -LiteralPath $archive).Length -ne $source.bytes -or (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $source.sha256) { throw "Source archive differs from the pin: $archive" }
    # Extract to a new directory each time. Never overwrite a source tree a user may have edited.
    $extract = Join-Path $output ('review-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $extract | Out-Null
    Push-Location $extract
    try {
        & $lock.buildTools.cmake.path -E tar xzf $archive
        if ($LASTEXITCODE -ne 0) { throw "Source extraction failed: $archive" }
    } finally { Pop-Location }
    [pscustomobject]@{ component=$source.component; revision=$source.revision; sha256=$source.sha256; archive=$archive; sourceDirectory=(Join-Path $extract $source.directory) }
}
Write-Warning 'These two archives do not constitute complete corresponding source for the supplier FFmpeg build. See docs/dependency-source-review.md.'

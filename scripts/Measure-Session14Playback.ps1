[CmdletBinding()]
param([string]$BuildDirectory = 'build/session-14')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $root $BuildDirectory }
$output = Join-Path $root 'evidence/session-14'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$oldPlatform = $env:QT_QPA_PLATFORM
$benchmarkLog = Join-Path $output 'benchmark.log'
"Starting synchronized playback measurement at $([datetime]::UtcNow.ToString('o'))" | Set-Content -LiteralPath $benchmarkLog -Encoding utf8
try {
    $env:QT_QPA_PLATFORM = 'offscreen'
    & (Join-Path $build 'Release/SequenceTests.exe') $root $output --measure 2>&1 | Tee-Object -FilePath $benchmarkLog -Append
    if ($LASTEXITCODE -ne 0) { throw 'Target-PC synchronized playback measurement failed.' }
    'Passed; detailed camera timing and memory results are in multicam-benchmark.json.' | Tee-Object -FilePath $benchmarkLog -Append
} finally { $env:QT_QPA_PLATFORM = $oldPlatform }

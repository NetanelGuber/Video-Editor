[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$lock = Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$ffmpeg = Join-Path $root "$($lock.packages[1].prefix)/bin/ffmpeg.exe"
$output = Join-Path $root 'fixtures/generated/session-7'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$soundtrack = Join-Path $output 'soundtrack.wav'
if (-not (Test-Path -LiteralPath $soundtrack)) {
    & $ffmpeg -hide_banner -loglevel error -nostdin -y -f lavfi -i 'sine=frequency=440:sample_rate=48000:duration=12' -ac 2 -c:a pcm_s16le $soundtrack
    if ($LASTEXITCODE -ne 0) { throw 'Could not prepare soundtrack test media.' }
}
Write-Host "Session 7 soundtrack: $soundtrack"

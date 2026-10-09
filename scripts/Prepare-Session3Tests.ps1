[CmdletBinding()]
param([string]$Directory = 'fixtures/generated/session-3')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$destination = if ([IO.Path]::IsPathRooted($Directory)) { $Directory } else { Join-Path $root $Directory }
New-Item -ItemType Directory -Path (Join-Path $destination 'import/subfolder') -Force | Out-Null
$lock = Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$ffmpeg = Join-Path $root "$($lock.packages[1].prefix)/bin/ffmpeg.exe"
$video = Join-Path $destination 'import/subfolder/short-video.mp4'
& $ffmpeg -v error -y -f lavfi -i 'testsrc2=size=320x180:rate=30' -f lavfi -i 'sine=frequency=440:sample_rate=48000' -t 2 -c:v libx264 -threads 2 -pix_fmt yuv420p -c:a aac $video
if ($LASTEXITCODE -ne 0) { throw 'Cannot generate short-video.mp4' }
$audio = Join-Path $destination 'import/audio-only.wav'
& $ffmpeg -v error -y -f lavfi -i 'sine=frequency=880:sample_rate=48000' -t 2 -ac 2 -c:a pcm_s16le $audio
if ($LASTEXITCODE -ne 0) { throw 'Cannot generate audio-only.wav' }
[IO.File]::WriteAllText((Join-Path $destination 'import/corrupt.mp4'), 'This is intentionally invalid media.')
[IO.File]::WriteAllText((Join-Path $destination 'import/ignored.txt'), 'Folder import should skip this file.')
Write-Host "Prepared disposable media for Session 3: $destination"

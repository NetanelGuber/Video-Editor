[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$lock = Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$ffmpeg = Join-Path $root "$($lock.packages[1].prefix)/bin/ffmpeg.exe"
$output = Join-Path $root 'fixtures/generated/session-5'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$sync = Join-Path $output 'flash-beep.mp4'
if (-not (Test-Path -LiteralPath $sync)) {
    & $ffmpeg -hide_banner -loglevel error -nostdin -y -f lavfi -i 'testsrc2=size=640x360:rate=60:duration=12' -f lavfi -i 'aevalsrc=if(lt(mod(t\,1)\,0.1)\,0.2*sin(2*PI*880*t)\,0):s=48000:d=12' -vf "drawbox=x=0:y=0:w=120:h=120:color=white:t=fill:enable='lt(mod(t,1),0.1)'" -c:v libx264 -preset fast -crf 18 -pix_fmt yuv420p -g 120 -c:a aac -shortest $sync
    if ($LASTEXITCODE -ne 0) { throw 'Could not prepare flash/beep test media.' }
}
Write-Host "Session 5 test media: $sync. White square and beep occur together every second."

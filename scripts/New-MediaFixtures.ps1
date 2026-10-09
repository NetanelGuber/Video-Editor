[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$FFmpeg,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../fixtures/generated')
)
$ErrorActionPreference = 'Stop'
$encoder = (Get-Command $FFmpeg -ErrorAction Stop).Source
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
function Invoke-Fixture([string]$Name, [string[]]$Arguments) {
    $target = Join-Path $OutputDirectory $Name
    if (Test-Path -LiteralPath $target) { throw "Fixture already exists: $target. Choose a fresh output directory." }
    & $encoder -hide_banner -loglevel error -nostdin @Arguments $target
    if ($LASTEXITCODE -ne 0) { throw "Fixture generation failed: $Name" }
    Write-Host "Generated $Name"
}
Invoke-Fixture 'synthetic-4k-h264-aac.mp4' @('-f','lavfi','-i','testsrc2=size=3840x2160:rate=30:duration=2','-f','lavfi','-i','sine=frequency=440:sample_rate=48000:duration=2','-c:v','libx264','-preset','ultrafast','-crf','28','-pix_fmt','yuv420p','-c:a','aac','-b:a','128k','-movflags','+faststart','-shortest')
Invoke-Fixture 'synthetic-4k-hevc10-aac.mp4' @('-f','lavfi','-i','testsrc2=size=3840x2160:rate=30:duration=2','-f','lavfi','-i','sine=frequency=880:sample_rate=48000:duration=2','-c:v','libx265','-preset','ultrafast','-crf','30','-pix_fmt','yuv420p10le','-x265-params','log-level=error:pools=4','-tag:v','hvc1','-c:a','aac','-shortest')
Invoke-Fixture 'synthetic-vfr-h264.mp4' @('-f','lavfi','-i','testsrc2=size=320x180:rate=60:duration=3','-vf','select=if(lt(t\,1)\,not(mod(n\,2))\,not(mod(n\,3)))','-fps_mode','vfr','-c:v','libx264','-preset','ultrafast','-pix_fmt','yuv420p','-video_track_timescale','60000')
Invoke-Fixture 'synthetic-rotation90-h264.mp4' @('-display_rotation:v:0','90','-i',(Join-Path $OutputDirectory 'synthetic-vfr-h264.mp4'),'-map','0:v:0','-c','copy')
[ordered]@{
    schemaVersion=1; generatedAtUtc=[DateTime]::UtcNow.ToString('o')
    ffmpegPath=$encoder; ffmpegVersion=((& $encoder -version | Select-Object -First 1) -join '')
    ffmpegSha256=(Get-FileHash -LiteralPath $encoder -Algorithm SHA256).Hash.ToLowerInvariant()
    generatorScriptSha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
    provenance='FFmpeg testsrc2 and sine generators; not user footage; 10-bit fixture is SDR'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'generation.json') -Encoding utf8
Write-Host 'All fixtures are generated patterns; HEVC 10-bit is SDR, not an HDR reference.'

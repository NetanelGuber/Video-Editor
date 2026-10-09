[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$lock = Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$bin = Join-Path $root "$($lock.packages[1].prefix)/bin"
$output = Join-Path $root 'evidence/session-5/encoders'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$sample = (Get-Content -LiteralPath (Join-Path $root 'evidence/session-0/test-set/inventory.json') -Raw | ConvertFrom-Json).files[2].path
$jobs = @(
    @{encoder='libx264'; path=$sample; pixel='yuv420p'; name='h264-cpu'},
    @{encoder='h264_amf'; path=$sample; pixel='nv12'; name='h264-amf'},
    @{encoder='hevc_amf'; path=$sample; pixel='nv12'; name='hevc-amf'},
    @{encoder='av1_amf'; path=$sample; pixel='nv12'; name='av1-amf'},
    @{encoder='hevc_amf'; path=(Join-Path $root 'fixtures/generated/pinned/synthetic-4k-hevc10-aac.mp4'); pixel='p010le'; name='hevc10-amf'}
)
$reports = foreach ($job in $jobs) {
    $file = Join-Path $output "$($job.name).mp4"
    $stderr = Join-Path $output "$($job.name).stderr.log"
    $timer = [Diagnostics.Stopwatch]::StartNew()
    & (Join-Path $bin 'ffmpeg.exe') -hide_banner -loglevel warning -nostdin -y -i $job.path -vf fps=30 -t 1 -an -c:v $job.encoder -pix_fmt $job.pixel $file 2> $stderr
    $exit = $LASTEXITCODE
    $timer.Stop()
    $probe = $null; $decoded = $false
    if ($exit -eq 0) {
        $probeText = & (Join-Path $bin 'ffprobe.exe') -v error -show_streams -show_format -of json $file
        if ($LASTEXITCODE -eq 0) { $probe = ($probeText -join "`n") | ConvertFrom-Json }
        & (Join-Path $bin 'ffmpeg.exe') -v error -nostdin -i $file -f null NUL 2> (Join-Path $output "$($job.name).decode.log")
        $decoded = $LASTEXITCODE -eq 0
    }
    [ordered]@{name=$job.name; requestedEncoder=$job.encoder; pixelFormat=$job.pixel; input=$job.path; output=$file; exitCode=$exit; elapsedMs=$timer.ElapsedMilliseconds; usable=($exit -eq 0 -and $decoded -and $null -ne $probe); independentlyDecoded=$decoded; metadata=$probe}
}
[ordered]@{collectedAtUtc=[datetime]::UtcNow.ToString('o'); targetGpu=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,DriverDate); encoders=@($reports); evidenceLevel='Actual target-PC FFmpeg encode, metadata probe and CPU decode; no export feature or separate-player visual acceptance'} |
    ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $output 'capabilities.json') -Encoding utf8
$reports | ForEach-Object { [pscustomobject]$_ } | Select-Object name,usable,elapsedMs | Format-Table
if (-not $reports[0].usable) { throw 'CPU H.264 baseline encoder failed.' }

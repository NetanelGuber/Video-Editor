[CmdletBinding()]
param(
    [Parameter(Mandatory)][string[]]$Path,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../evidence/session-0/media'),
    [string]$FFprobe = 'ffprobe',
    [switch]$Recurse,
    [switch]$InspectTiming,
    [switch]$InspectFullTiming,
    [switch]$Hash
)

$ErrorActionPreference = 'Stop'
$probeCommand = (Get-Command $FFprobe -ErrorAction Stop).Source
$extensions = @('.mp4', '.mov', '.mkv', '.avi', '.webm', '.m4v', '.mts', '.m2ts')
$files = @($Path | ForEach-Object {
    $item = Get-Item -LiteralPath $_
    if ($item.PSIsContainer) {
        Get-ChildItem -LiteralPath $item.FullName -File -Recurse:$Recurse |
            Where-Object Extension -In $extensions
    } else { $item }
} | Sort-Object FullName -Unique)
if ($files.Count -eq 0) { throw 'No media files found at the supplied paths.' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$pixelFormatJson = & $probeCommand -v error -show_pixel_formats -of json
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect FFprobe pixel formats.' }
$pixelFormats = ($pixelFormatJson | ConvertFrom-Json).pixel_formats
$records = @()
for ($index = 0; $index -lt $files.Count; $index++) {
    $file = $files[$index]
    $id = 'media-{0:d3}' -f ($index + 1)
    $raw = & $probeCommand -v error -show_format -show_streams -of json $file.FullName
    if ($LASTEXITCODE -ne 0) { throw "FFprobe failed for $($file.FullName)." }
    $raw | Set-Content -LiteralPath (Join-Path $OutputDirectory "$id.probe.json") -Encoding utf8
    $probe = $raw | ConvertFrom-Json
    $videos = @($probe.streams | Where-Object codec_type -EQ video | ForEach-Object {
        $stream = $_
        $pixelFormat = $pixelFormats | Where-Object name -EQ $stream.pix_fmt | Select-Object -First 1
        [ordered]@{
            index = $stream.index; codec = $stream.codec_name; profile = $stream.profile
            width = $stream.width; height = $stream.height; pixelFormat = $stream.pix_fmt
            componentBitDepth = @($pixelFormat.components | ForEach-Object bit_depth | Sort-Object -Unique)
            declaredRawBitDepth = $stream.bits_per_raw_sample
            nominalFrameRate = $stream.r_frame_rate; averageFrameRate = $stream.avg_frame_rate
            timeBase = $stream.time_base; frameCount = $stream.nb_frames
            colorRange = $stream.color_range; colorSpace = $stream.color_space
            colorTransfer = $stream.color_transfer; colorPrimaries = $stream.color_primaries
            rotationTag = $stream.tags.rotate
            rotationSideData = @($stream.side_data_list | Where-Object { $null -ne $_.rotation } | ForEach-Object rotation)
        }
    })
    $timing = @()
    if ($InspectTiming) {
        foreach ($video in $videos) {
            # Decode only the first 5 seconds; frame timestamps avoid packet decode-order confusion.
            $frameJson = & $probeCommand -v error -select_streams "$($video.index)" -read_intervals '%+5' -show_frames -show_entries frame=best_effort_timestamp_time -of json $file.FullName
            if ($LASTEXITCODE -ne 0) { throw "Frame timing probe failed for $($file.FullName)." }
            $frameJson | Set-Content -LiteralPath (Join-Path $OutputDirectory "$id.stream-$($video.index).timing.json") -Encoding utf8
            $timestamps = @(($frameJson | ConvertFrom-Json).frames | Where-Object { $null -ne $_.best_effort_timestamp_time } | ForEach-Object { [double]::Parse($_.best_effort_timestamp_time, [Globalization.CultureInfo]::InvariantCulture) })
            $deltas = @(for ($n = 1; $n -lt $timestamps.Count; $n++) { $timestamps[$n] - $timestamps[$n - 1] })
            $stats = $deltas | Measure-Object -Minimum -Maximum -Average
            $tickParts = $video.timeBase.Split('/')
            $tick = [double]$tickParts[0] / [double]$tickParts[1]
            # Allow container tick rounding without hiding dropped-frame intervals
            # when a container's tick is as coarse as its nominal frame duration.
            $tolerance = [Math]::Min($tick, $stats.Average * 0.25) + 0.000002
            $classification = 'insufficient timestamps'
            if ($deltas.Count -gt 1) {
                $classification = if ($stats.Minimum -le 0) { 'non-monotonic timestamps observed' }
                    elseif (($stats.Maximum - $stats.Minimum) -gt $tolerance) { 'variable intervals observed in first 5 seconds' }
                    else { 'constant intervals within time-base rounding in first 5 seconds' }
            }
            $timing += [ordered]@{streamIndex=$video.index; scope='first 5 seconds only'; frameCount=$timestamps.Count; intervalCount=$deltas.Count; minSeconds=$stats.Minimum; maxSeconds=$stats.Maximum; averageSeconds=$stats.Average; toleranceSeconds=$tolerance; classification=$classification}
        }
    }
    $fullTiming = @()
    if ($InspectFullTiming) {
        foreach ($video in $videos) {
            $packetJson = & $probeCommand -v error -select_streams "$($video.index)" -show_packets -show_entries packet=pts -of json $file.FullName
            if ($LASTEXITCODE -ne 0) { throw "Packet timing probe failed for $($file.FullName)." }
            $packetJson | Set-Content -LiteralPath (Join-Path $OutputDirectory "$id.stream-$($video.index).packets.json") -Encoding utf8
            $pts = @(($packetJson | ConvertFrom-Json).packets | Where-Object { $null -ne $_.pts } | ForEach-Object { [long]$_.pts } | Sort-Object)
            $deltas = @(for ($n = 1; $n -lt $pts.Count; $n++) { $pts[$n] - $pts[$n - 1] })
            $stats = $deltas | Measure-Object -Minimum -Maximum -Average
            $tolerance = [Math]::Min(1.00001, $stats.Average * 0.25)
            $classification = 'insufficient timestamps'
            if ($deltas.Count -gt 1) {
                $classification = if ($stats.Minimum -le 0) { 'duplicate presentation timestamps observed' }
                    elseif (($stats.Maximum - $stats.Minimum) -gt $tolerance) { 'variable presentation intervals observed across full file' }
                    else { 'constant presentation intervals within time-base rounding across full file' }
            }
            $fullTiming += [ordered]@{streamIndex=$video.index; scope='all video packet PTS sorted into presentation order; not a full decode'; packetCount=$pts.Count; intervalCount=$deltas.Count; minTicks=$stats.Minimum; maxTicks=$stats.Maximum; averageTicks=$stats.Average; timeBase=$video.timeBase; toleranceTicks=$tolerance; classification=$classification}
        }
    }
    $records += [ordered]@{
        id=$id; path=$file.FullName; sizeBytes=$file.Length
        lastWriteTimeUtc=$file.LastWriteTimeUtc.ToString('o')
        sha256=$(if ($Hash) { (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() } else { $null })
        container=$probe.format.format_name; durationSeconds=$probe.format.duration
        video=$videos
        audio=@($probe.streams | Where-Object codec_type -EQ audio | ForEach-Object { [ordered]@{index=$_.index; codec=$_.codec_name; profile=$_.profile; sampleRate=$_.sample_rate; channels=$_.channels; channelLayout=$_.channel_layout; timeBase=$_.time_base} })
        timing=$timing; fullTiming=$fullTiming; rawProbeFile="$id.probe.json"
    }
    Write-Host "Probed $($file.Name)"
}
[ordered]@{schemaVersion=1; collectedAtUtc=[DateTime]::UtcNow.ToString('o'); ffprobePath=$probeCommand; ffprobeVersion=((& $probeCommand -version | Select-Object -First 1) -join ''); files=$records} |
    ConvertTo-Json -Depth 15 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'inventory.json') -Encoding utf8
Write-Host "Saved $($records.Count) records to $OutputDirectory"

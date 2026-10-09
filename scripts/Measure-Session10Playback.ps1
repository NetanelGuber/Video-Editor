[CmdletBinding()]
param([string]$BuildDirectory = 'build/session-10', [string]$BaselineBuildDirectory = 'build/session-9')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $root $BuildDirectory }
$baseline = if ([IO.Path]::IsPathRooted($BaselineBuildDirectory)) { $BaselineBuildDirectory } else { Join-Path $root $BaselineBuildDirectory }
$output = Join-Path $root 'evidence/session-10/benchmark'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$inventory = Get-Content -LiteralPath (Join-Path $root 'evidence/session-0/test-set/inventory.json') -Raw | ConvertFrom-Json
$samples = @($inventory.files.path) + @('synthetic-4k-h264-aac.mp4','synthetic-4k-hevc10-aac.mp4','synthetic-vfr-h264.mp4','synthetic-rotation90-h264.mp4' | ForEach-Object { Join-Path $root "fixtures/generated/pinned/$_" })
$reports = [Collections.Generic.List[object]]::new()
function Invoke-PlaybackMeasurement([string]$Executable, [string]$Sample, [string]$Decode, [string]$Presentation, [int]$Duration, [string]$Name) {
    $path = Join-Path $output "$Name.json"
    $start = [Diagnostics.ProcessStartInfo]::new($Executable)
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.RedirectStandardError = $true; $start.RedirectStandardOutput = $true
    $start.Environment['QT_QPA_PLATFORM'] = 'windows'
    $start.Environment['VIDEO_EDITOR_CACHE_DIR'] = Join-Path $build 'isolated-media-cache'
    foreach ($argument in @($Sample,$Decode,$Presentation,[string]$Duration,$path)) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($start)
    $stderr = $process.StandardError.ReadToEndAsync(); $stdout = $process.StandardOutput.ReadToEndAsync()
    if (-not $process.WaitForExit(60000)) { $process.Kill(); throw "Benchmark timeout: $Name" }
    $stderr.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $output "$Name.stderr.log") -Encoding utf8
    $stdout.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $output "$Name.stdout.log") -Encoding utf8
    if ($process.ExitCode -ne 0) { throw "Benchmark failed: $Name (exit $($process.ExitCode))" }
    $report = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if (-not $report.passed) { throw "Failed report: $path" }
    $reports.Add([ordered]@{name=$Name;report=$report})
    $process.Dispose()
    Write-Host "$Name : startup $($report.startupMs) ms, seek $($report.seekMs) ms, presented $($report.presented), dropped $($report.dropped)"
}
for ($n = 0; $n -lt $samples.Count; $n++) {
    $sample = $samples[$n]; $duration = if ($n -ge 6) { 1800 } else { 5000 }
    foreach ($decode in @('cpu','d3d11')) {
        Invoke-PlaybackMeasurement (Join-Path $build 'Release/PlaybackBenchmark.exe') $sample $decode cpu $duration "current-$n-$decode"
        # Measure the actual first complete release on the same current target PC as the comparison.
        Invoke-PlaybackMeasurement (Join-Path $baseline 'Release/PlaybackBenchmark.exe') $sample $decode cpu $duration "baseline-$n-$decode"
    }
}
foreach ($n in @(0,2,6,7)) {
    Invoke-PlaybackMeasurement (Join-Path $build 'Release/PlaybackBenchmark.exe') $samples[$n] d3d11 gl $(if ($n -ge 6) {1800} else {5000}) "current-$n-d3d11-gl"
}
Invoke-PlaybackMeasurement (Join-Path $build 'Release/PlaybackBenchmark.exe') $samples[0] cpu cpu 20000 'current-stability-20s'
[ordered]@{
    collectedAtUtc=[datetime]::UtcNow.ToString('o'); baselineVersion='0.9.0'; currentVersion='0.10.0'
    method='Same production decoder/CPU raster harness as Session 5, all six real and four synthetic sources, sequential runs. GL uses hardware offscreen FBO. Edited monitor measurements are in performance-result.json.'
    samples=$reports.ToArray(); historicalBaseline='evidence/session-5/benchmark/summary.json'
} | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath (Join-Path $output 'summary.json') -Encoding utf8
Write-Host "Session 10 benchmark evidence: $output"

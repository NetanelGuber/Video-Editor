[CmdletBinding()]
param([string]$BuildDirectory = 'build/session-5')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $root $BuildDirectory }
$output = Join-Path $root 'evidence/session-5/benchmark'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$inventory = Get-Content -LiteralPath (Join-Path $root 'evidence/session-0/test-set/inventory.json') -Raw | ConvertFrom-Json
$samples = @($inventory.files[0].path, $inventory.files[2].path,
    (Join-Path $root 'fixtures/generated/pinned/synthetic-4k-h264-aac.mp4'),
    (Join-Path $root 'fixtures/generated/pinned/synthetic-4k-hevc10-aac.mp4'))
$reports = [Collections.Generic.List[object]]::new()
$gpuSupported = $false
$gpuNotes = [Collections.Generic.List[string]]::new()
$index = 0
foreach ($sample in $samples) {
    foreach ($decode in @('cpu','d3d11')) {
        foreach ($presentation in @('cpu','gl')) {
            $index++
            $name = 'sample-{0:d2}-{1}-{2}' -f $index,$decode,$presentation
            $path = Join-Path $output "$name.json"
            $start = [Diagnostics.ProcessStartInfo]::new((Join-Path $build 'Release/PlaybackBenchmark.exe'))
            $start.UseShellExecute = $false
            $start.CreateNoWindow = $true
            $start.RedirectStandardError = $true
            $start.RedirectStandardOutput = $true
            $start.Environment['QT_QPA_PLATFORM'] = 'windows'
            $duration = if ($sample -like '*synthetic-*') { '1800' } else { '5000' }
            foreach ($argument in @($sample,$decode,$presentation,$duration,$path)) { $start.ArgumentList.Add($argument) }
            $process = [Diagnostics.Process]::Start($start)
            $stderr = $process.StandardError.ReadToEndAsync()
            $stdout = $process.StandardOutput.ReadToEndAsync()
            $gpu = [Collections.Generic.List[object]]::new()
            while (-not $process.HasExited) {
                    try {
                        # Wildcard instances can disappear between samples; retain the valid counters.
                        $gpuErrors = @()
                        $counters = Get-Counter -Counter '\GPU Engine(*)\Utilization Percentage' -ErrorAction SilentlyContinue -ErrorVariable gpuErrors
                        foreach ($gpuError in $gpuErrors) { if (-not $gpuNotes.Contains($gpuError.Exception.Message)) { $gpuNotes.Add($gpuError.Exception.Message) } }
                        $engines = @($counters.CounterSamples | Where-Object { $_.InstanceName -match "^pid_$($process.Id)_" -and $_.Status -eq 0 })
                        if ($engines.Count) { $gpuSupported = $true }
                        foreach ($engine in $engines) {
                            $gpu.Add([ordered]@{timeUtc=$counters.Timestamp.ToUniversalTime().ToString('o'); instance=$engine.InstanceName; percent=$engine.CookedValue})
                        }
                    } catch { if (-not $gpuNotes.Contains($_.Exception.Message)) { $gpuNotes.Add($_.Exception.Message) }; Start-Sleep -Milliseconds 200 }
            }
            $process.WaitForExit()
            $stderr.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $output "$name.stderr.log") -Encoding utf8
            $stdout.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $output "$name.stdout.log") -Encoding utf8
            if ($process.ExitCode -ne 0) { throw "Playback benchmark failed: $path, exit $($process.ExitCode)" }
            $report = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
            $report | Add-Member -NotePropertyName gpuSamples -NotePropertyValue $gpu.ToArray()
            $report | Add-Member -NotePropertyName gpuCountersAvailable -NotePropertyValue ($gpu.Count -gt 0)
            $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $path -Encoding utf8
            $reports.Add($report)
            Write-Host "$name : $($report.decoder.path), startup $($report.startupMs) ms, seek $($report.seekMs) ms, presented $($report.presented), dropped $($report.dropped), renderer $($report.glRenderer)"
            $process.Dispose()
        }
    }
}
$stabilityPath = Join-Path $output 'stability-20s.json'
& (Join-Path $build 'Release/PlaybackBenchmark.exe') $samples[0] cpu cpu 20000 $stabilityPath 2> (Join-Path $output 'stability.stderr.log')
if ($LASTEXITCODE -ne 0) { throw '20-second playback stability benchmark failed.' }
[ordered]@{collectedAtUtc=[datetime]::UtcNow.ToString('o'); gpuCountersAvailable=$gpuSupported; gpuCounterNotes=$gpuNotes.ToArray(); samples=$reports.ToArray(); stability=(Get-Content -LiteralPath $stabilityPath -Raw | ConvertFrom-Json)} |
    ConvertTo-Json -Depth 14 | Set-Content -LiteralPath (Join-Path $output 'summary.json') -Encoding utf8
Write-Host "Session 5 playback benchmarks: $output"

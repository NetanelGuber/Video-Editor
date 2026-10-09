#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Archive, [string]$BuildDirectory = 'build/1.0.0', [string]$OutputDirectory = 'build/1.0.0-package-test')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$lock = Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
function Absolute([string]$p) { if ([IO.Path]::IsPathRooted($p)) { [IO.Path]::GetFullPath($p) } else { [IO.Path]::GetFullPath((Join-Path $root $p)) } }
$archivePath = Absolute $Archive; $output = Absolute $OutputDirectory; $build = Absolute $BuildDirectory
if (Test-Path -LiteralPath $output) { throw 'Choose a new -OutputDirectory: verification always uses a fresh extraction and profile.' }
New-Item -ItemType Directory -Path $output -Force | Out-Null
$expectedHash = (Get-Content -LiteralPath "$archivePath.sha256" -Raw).Split(' ')[0].Trim()
if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $expectedHash) { throw 'Archive checksum mismatch.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archiveObject = [IO.Compression.ZipFile]::OpenRead($archivePath)
try { foreach ($entry in $archiveObject.Entries) {
    $target = [IO.Path]::GetFullPath((Join-Path $output $entry.FullName))
    if (-not $target.StartsWith($output.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Archive path escapes extraction root.' }
} } finally { $archiveObject.Dispose() }
[IO.Compression.ZipFile]::ExtractToDirectory($archivePath, $output)
$folders = @(Get-ChildItem -LiteralPath $output -Directory)
if ($folders.Count -ne 1) { throw 'Archive must have exactly one top-level package folder.' }
$package = $folders[0].FullName
$manifest = Get-Content -LiteralPath (Join-Path $package 'build/package-manifest.json') -Raw | ConvertFrom-Json
$expectedFiles = @($manifest.files.path) + 'build/package-manifest.json'
$actualFiles = @(Get-ChildItem -LiteralPath $package -Recurse -File | ForEach-Object { [IO.Path]::GetRelativePath($package,$_.FullName).Replace('\','/') })
if (Compare-Object $expectedFiles $actualFiles) { throw 'Extracted payload differs from manifest file list.' }
foreach ($file in $manifest.files) {
    $path = Join-Path $package $file.path
    if ((Get-Item -LiteralPath $path).Length -ne $file.bytes -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256) { throw "Payload mismatch: $($file.path)" }
}
if ($actualFiles -match '(Test.*\.exe$|Tests\.exe$|\.pdb$|\.lib$|(Qt6Test|WebEngine|WebView|Qml|Quick).*\.(dll|exe)$)') { throw 'Developer/test/web payload leaked into release.' }
$dumpbin = Join-Path (Split-Path -Parent $lock.buildTools.msvc.path) 'dumpbin.exe'
$imports = @()
foreach ($binary in Get-ChildItem -LiteralPath $package -Recurse -File | Where-Object Extension -in @('.dll','.exe')) {
    $headers = & $dumpbin /headers $binary.FullName
    if ($LASTEXITCODE -ne 0 -or ($headers -join "`n") -notmatch '8664 machine \(x64\)') { throw "Invalid x64 binary: $($binary.Name)" }
    $details = & $dumpbin /dependents $binary.FullName
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect imports: $($binary.Name)" }
    foreach ($line in $details) { if ($line -match '^\s+([a-zA-Z0-9_.-]+\.dll)\s*$') {
        $dll = $Matches[1]; $local = Join-Path $package $dll; $system = Join-Path $env:WINDIR "System32/$dll"
        $resolution = if (Test-Path -LiteralPath $local) { 'package' } elseif ($dll -match '^(api-ms-|ext-ms-)') { 'Windows API contract' } elseif (Test-Path -LiteralPath $system) { 'Windows system/driver' } else { throw "Unresolved runtime import $dll in $($binary.Name)" }
        if ($dll -match '^(Qt6|avcodec|avformat|avutil|avfilter|swscale|swresample|msvcp\d|vcruntime\d|concrt\d)' -and $resolution -ne 'package') { throw "Developer dependency escaped package: $dll" }
        $imports += @{binary=[IO.Path]::GetRelativePath($package,$binary.FullName); dependency=$dll; resolution=$resolution}
    } }
}
$profile = Join-Path $output 'fresh-profile'; New-Item -ItemType Directory -Path $profile | Out-Null
function Run-Isolated([string]$exe, [string[]]$arguments, [string]$label, [int]$timeout = 240000) {
    $info = [Diagnostics.ProcessStartInfo]::new(); $info.FileName=$exe; $info.WorkingDirectory=$package
    $info.UseShellExecute=$false; $info.CreateNoWindow=$true; $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
    foreach ($argument in $arguments) { $info.ArgumentList.Add($argument) }
    $info.Environment['PATH'] = "$env:WINDIR\System32;$env:WINDIR"
    foreach ($key in @('QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QTDIR','QML2_IMPORT_PATH','QML_IMPORT_PATH')) { $info.Environment.Remove($key) | Out-Null }
    $info.Environment['QT_QPA_PLATFORM']='offscreen'; $info.Environment['QT_PLUGIN_PATH']=$package
    $info.Environment['VIDEO_EDITOR_CACHE_DIR']=Join-Path $profile 'cache'
    $info.Environment['APPDATA']=Join-Path $profile 'roaming'; $info.Environment['LOCALAPPDATA']=Join-Path $profile 'local'
    $process = [Diagnostics.Process]::new(); $process.StartInfo=$info
    if (-not $process.Start()) { throw "Cannot launch $label" }
    $stdout=$process.StandardOutput.ReadToEndAsync(); $stderr=$process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($timeout)) { $process.Kill($true); $process.WaitForExit(); throw "$label timed out" }
    $text=$stdout.GetAwaiter().GetResult(); $errors=$stderr.GetAwaiter().GetResult()
    $text | Set-Content -LiteralPath (Join-Path $output "$label.stdout.txt") -Encoding utf8
    $errors | Set-Content -LiteralPath (Join-Path $output "$label.stderr.txt") -Encoding utf8
    $exit=$process.ExitCode; $process.Dispose(); if ($exit -ne 0) { throw "$label failed ($exit). See $output/$label.stderr.txt" }
    return $text
}
Run-Isolated (Join-Path $package 'VideoEditor.exe') @('--smoke-test','--data-dir',(Join-Path $profile 'shell'),'--project-fixtures',(Join-Path $root 'fixtures/projects')) 'packaged-shell' | Out-Null
$loadedInventory = (Run-Isolated (Join-Path $package 'ExportCapabilityProbe.exe') @('--inventory') 'packaged-inventory') | ConvertFrom-Json
$recorded = Get-Content -LiteralPath (Join-Path $package 'build/ffmpeg-runtime.json') -Raw | ConvertFrom-Json
if (($loadedInventory | ConvertTo-Json -Depth 30 -Compress) -ne ($recorded | ConvertTo-Json -Depth 30 -Compress)) { throw 'Packaged encoder/runtime inventory drift.' }
# Only the test harness is temporarily placed beside the exact extracted release DLLs.
# It has no Qt Test dependency. It is never included in the shipped ZIP.
$harness = Join-Path $package 'PackageWorkflowTests.exe'
Copy-Item -LiteralPath (Join-Path $build 'Release/PackageWorkflowTests.exe') -Destination $harness
try { Run-Isolated $harness @((Join-Path $package 'examples/flash-beep.mp4'), (Join-Path $profile 'workflow')) 'packaged-workflow' | Out-Null }
finally { Remove-Item -LiteralPath $harness -ErrorAction SilentlyContinue }
$workflow = Get-Content -LiteralPath (Join-Path $profile 'workflow/workflow-result.json') -Raw | ConvertFrom-Json
if (-not $workflow.passed) { throw 'Packaged workflow failed.' }
foreach ($module in $workflow.modules) {
    $name = [IO.Path]::GetFileName($module)
    if ($name -match '^(Qt6|avcodec|avformat|avutil|avfilter|swscale|swresample|msvcp\d|vcruntime\d|concrt\d).+\.dll$' -and
        -not $module.StartsWith($package + '\',[StringComparison]::OrdinalIgnoreCase)) { throw "Loaded dependency from outside package: $module" }
}
$ffmpeg = Join-Path $root "$($lock.packages[1].prefix)/bin"
$exports=@()
foreach ($filename in @('simple.mp4','advanced.mkv')) {
    $media = Join-Path $workflow.runDirectory $filename
    $probe = & (Join-Path $ffmpeg 'ffprobe.exe') -v error -show_streams -show_format -of json $media
    if ($LASTEXITCODE -ne 0) { throw "Independent inspection failed: $filename" }
    $metadata = $probe | ConvertFrom-Json
    $video = $metadata.streams | Where-Object codec_type -eq 'video'; $audio = $metadata.streams | Where-Object codec_type -eq 'audio'
    if ($video.codec_name -ne 'h264' -or $video.width -ne 320 -or $video.height -ne 180 -or $video.r_frame_rate -ne '60/1' -or $audio.codec_name -ne 'aac') { throw "Export metadata differs: $filename" }
    & (Join-Path $ffmpeg 'ffmpeg.exe') -v error -nostdin -i $media -f null NUL 2>&1 | Set-Content (Join-Path $output "$filename-decode.txt")
    if ($LASTEXITCODE -ne 0) { throw "Full independent decode failed: $filename" }
    $exports += @{file=$filename; metadata=$metadata; sha256=(Get-FileHash -LiteralPath $media -Algorithm SHA256).Hash}
}
# Test harness removal and execution must leave the package payload byte-exact.
foreach ($file in $manifest.files) { if ((Get-FileHash -LiteralPath (Join-Path $package $file.path) -Algorithm SHA256).Hash -ne $file.sha256) { throw "Package modified by execution: $($file.path)" } }
[ordered]@{passed=$true; version=$manifest.version; archive=$archivePath; archiveSha256=$expectedHash; verifiedUtc=[DateTime]::UtcNow.ToString('o');
    payloadFiles=$manifest.files.Count; binaryImports=$imports; workflow=$workflow; exports=$exports;
    evidenceLevel='Fresh ZIP extraction on target PC; restricted PATH; exact packaged DLLs and actual module paths; offscreen production UI; isolated settings/recovery/cache';
    limits=@('No separate Windows account or clean OS/VM','Native Windows platform plugin inventoried but not visually exercised','Physical playback devices and subjective AV sync unverified');
    externalDistributionApproved=$false
} | ConvertTo-Json -Depth 35 | Set-Content -LiteralPath (Join-Path $output 'package-result.json') -Encoding utf8
Write-Host "Packaged first workflow and dependency verification passed: $output/package-result.json"


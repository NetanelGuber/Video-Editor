#requires -Version 7.0
[CmdletBinding()]
param([string]$BuildDirectory = 'build/1.0.0', [string]$OutputDirectory = 'out/release')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$lock = Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
function Absolute([string]$path) { if ([IO.Path]::IsPathRooted($path)) { [IO.Path]::GetFullPath($path) } else { [IO.Path]::GetFullPath((Join-Path $root $path)) } }
$build = Absolute $BuildDirectory
$output = Absolute $OutputDirectory
$version = (Get-Content -LiteralPath (Join-Path $build 'generated/BuildInfo.h') | Select-String '^#define EDITOR_VERSION "([^"]+)"').Matches.Groups[1].Value
if (-not $version) { throw 'Build first using Build.ps1.' }
$name = "VideoEditor-$version-windows-x64"
$stage = Join-Path $output $name
$zip = "$stage.zip"
if ((Test-Path -LiteralPath $stage) -or (Test-Path -LiteralPath $zip)) { throw 'Package output already exists. Choose a new -OutputDirectory; existing artifacts are preserved.' }
New-Item -ItemType Directory -Path $stage -Force | Out-Null
$origins = @{}
function Copy-Payload([string]$source, [string]$relative) {
    $destination = Join-Path $stage $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
    $origins[$relative.Replace('\','/')] = (Resolve-Path -LiteralPath $source).Path
}
foreach ($exe in @('VideoEditor.exe','ExportCapabilityProbe.exe')) { Copy-Payload (Join-Path $build "Release/$exe") $exe }
if ((Get-Item (Join-Path $stage 'VideoEditor.exe')).VersionInfo.ProductVersion -ne $version) { throw 'Executable resource version differs from generated build version.' }
$qt = Join-Path $root $lock.packages[0].prefix
$ffmpeg = Join-Path $root $lock.packages[1].prefix
# The exact pinned deploy tool supports --no-patchqt (not the spelling in newer online docs).
$deployment = & (Join-Path $qt 'bin/windeployqt.exe') --release --no-translations --no-compiler-runtime --no-patchqt --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler --skip-plugin-types generic,networkinformation,tls --json (Join-Path $stage 'VideoEditor.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
$deployment | Set-Content -LiteralPath (Join-Path $output "$name-qt-deployment.json") -Encoding utf8
foreach ($row in ($deployment | ConvertFrom-Json).files) {
    $target = Join-Path $row.target (Split-Path -Leaf $row.source)
    $relative = [IO.Path]::GetRelativePath($stage, $target).Replace('\','/')
    if ($relative.StartsWith('../')) { throw 'Qt deployment escaped package directory.' }
    $origins[$relative] = $row.source
}
Copy-Payload (Join-Path $qt 'plugins/platforms/qoffscreen.dll') 'platforms/qoffscreen.dll'
foreach ($dll in @('avutil-61','avformat-63','avcodec-63','swscale-10','swresample-7','avfilter-12')) { Copy-Payload (Join-Path $ffmpeg "bin/$dll.dll") "$dll.dll" }
$crt = Join-Path $lock.buildTools.visualStudio.path "VC/Redist/MSVC/$($lock.buildTools.msvc.toolsetDirectoryVersion)/x64/Microsoft.VC145.CRT"
foreach ($dll in Get-ChildItem -LiteralPath $crt -Filter '*.dll' -File) { Copy-Payload $dll.FullName $dll.Name }
@"
[Paths]
Prefix=.
Plugins=.
"@ | Set-Content -LiteralPath (Join-Path $stage 'qt.conf') -Encoding ascii
# qt.conf keeps the unmodified Qt DLLs relocatable. DXC is optional for D3D12;
# this editor uses QPainter/OpenGL and D3D11VA, so it is deliberately omitted.
foreach ($doc in @('quick-start.md','supported-formats.md','release-license-review.md','dependency-source-review.md','ffmpeg-supplier-source-request.md')) { Copy-Payload (Join-Path $root "docs/$doc") "docs/$doc" }
Copy-Payload (Join-Path $root 'LICENSE') 'LICENSE'
Copy-Payload (Join-Path $root 'COPYRIGHT.md') 'COPYRIGHT.md'
Copy-Payload (Join-Path $root 'dependencies.lock.json') 'build/dependencies.lock.json'
Copy-Payload (Join-Path $root 'src/export/presets.json') 'build/export-presets.json'
Copy-Payload (Join-Path $root 'evidence/session-0/dependencies/qtbase-sbom.spdx.json') 'notices/qtbase-sbom.spdx.json'
Copy-Payload (Join-Path $ffmpeg 'LICENSE') 'notices/FFmpeg-GPL.txt'
Copy-Payload (Join-Path $ffmpeg 'README.txt') 'notices/FFmpeg-supplier-README.txt'
Copy-Payload (Join-Path $lock.buildTools.visualStudio.path 'Licenses/1033/Redist.txt') 'notices/Microsoft-Redist.txt'
foreach ($notice in Get-ChildItem -LiteralPath (Join-Path $root 'docs/notices') -File) { Copy-Payload $notice.FullName "notices/$($notice.Name)" }
# Preserve the relative links used by the repository's source review document.
foreach ($notice in @('dependency-sources.json','qt-runtime-source-coverage.json','qt-runtime-attributions.txt','ffmpeg-external-review.json')) {
    Copy-Payload (Join-Path $root "docs/notices/$notice") "docs/notices/$notice"
}
Copy-Payload (Join-Path $root 'evidence/source-review-1.0.0/dependency-source-review.json') 'evidence/source-review-1.0.0/dependency-source-review.json'
Copy-Payload (Join-Path $root 'fixtures/generated/session-5/flash-beep.mp4') 'examples/flash-beep.mp4'
@"
Video Editor $version - Windows x64 - DRAFT RELEASE PACKAGE

Extract the entire ZIP to a writable folder and open VideoEditor.exe.
Read docs/quick-start.md and docs/supported-formats.md.
Keep every DLL, ExportCapabilityProbe.exe, qt.conf and the plugin folders together.
No developer tools, administrator access or PATH changes are required to run.
Windows 11 x64 supplies UCRT and system/graphics DLLs. MSVC runtime is included.
This archive is unsigned. It creates no file associations or shortcuts.
Projects/media are separate from the package. Settings/logs/cache use your profile.

Application source: https://github.com/NetanelGuber/Video-Editor/tree/v$version
Application license: GPL-3.0-or-later (see LICENSE and COPYRIGHT.md).
Dependency source URLs, revisions and hashes: notices/dependency-sources.json.
Microsoft runtime DLLs have separate terms: notices/Microsoft-VC14-Runtime.txt.
This draft package is for local review. Public binary distribution remains
pending exact dependency source/build material; see docs/release-license-review.md.
build/package-manifest.json inventories every payload file except itself.
"@ | Set-Content -LiteralPath (Join-Path $stage 'README.txt') -Encoding utf8
$runtime = & (Join-Path $stage 'ExportCapabilityProbe.exe') --inventory
if ($LASTEXITCODE -ne 0) { throw 'Packaged runtime inventory failed.' }
$compiled = $runtime | ConvertFrom-Json
if ($compiled.version -notlike "$($lock.packages[1].version)*" -or $compiled.license -notmatch 'GPL') { throw 'Packaged FFmpeg identity differs from the pin.' }
$runtime | Set-Content -LiteralPath (Join-Path $stage 'build/ffmpeg-runtime.json') -Encoding utf8
foreach ($flag in @('-version','-buildconf','-L','-encoders','-decoders','-formats','-filters','-hwaccels')) {
    $inventory = & (Join-Path $ffmpeg 'bin/ffmpeg.exe') $flag 2>&1
    if ($LASTEXITCODE -ne 0) { throw "FFmpeg inventory failed: $flag" }
    $inventory | Set-Content -LiteralPath (Join-Path $stage "build/ffmpeg$flag.txt") -Encoding utf8
}
$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName | ForEach-Object {
    $relative = [IO.Path]::GetRelativePath($stage, $_.FullName).Replace('\','/')
    $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $origin = $origins[$relative]
    if ($_.Extension -in @('.dll','.exe')) {
        if (-not $origin -or $hash -ne (Get-FileHash -LiteralPath $origin -Algorithm SHA256).Hash.ToLowerInvariant()) { throw "Unrecorded or modified binary: $relative" }
    }
    [ordered]@{path=$relative; bytes=$_.Length; sha256=$hash; source=$origin; fileVersion=$_.VersionInfo.FileVersion}
})
$sourcePaths = @('src','scripts','tests','docs','fixtures/projects','CMakeLists.txt','dependencies.lock.json','README.md','LICENSE','COPYRIGHT.md','.gitignore') | ForEach-Object { Join-Path $root $_ }
$sources = @(Get-ChildItem -LiteralPath $sourcePaths -Recurse -File | Sort-Object FullName | ForEach-Object {
    @{path=[IO.Path]::GetRelativePath($root,$_.FullName).Replace('\','/'); sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
[ordered]@{schemaVersion=1; product='Video Editor'; version=$version; architecture='windows-x64'; configuration='Release'; createdUtc=[DateTime]::UtcNow.ToString('o');
    distribution='draft-local-review'; externalDistributionApproved=$false; applicationLicense='GPL-3.0-or-later'; sourceUrl="https://github.com/NetanelGuber/Video-Editor/tree/v$version"; dependencies=$lock; files=$files; sourceInventory=$sources;
    runtimePolicy='Qt/FFmpeg/MSVC app-local; UCRT/Windows/graphics drivers supplied by Windows 11 x64';
    manifestExclusion='build/package-manifest.json'; qtDeployment='unmodified pinned DLLs; qt.conf relative prefix';
    evidenceLimits=@('No fresh Windows account or separate machine','Unsigned','Exact dependency corresponding source/build material unresolved for public binary distribution')
} | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath (Join-Path $stage 'build/package-manifest.json') -Encoding utf8
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($stage,$zip,[IO.Compression.CompressionLevel]::Optimal,$true)
$zipHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
"$zipHash  $([IO.Path]::GetFileName($zip))" | Set-Content -LiteralPath "$zip.sha256" -Encoding ascii
Write-Host "Created draft release package: $zip"


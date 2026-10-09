#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$QtSourceDirectory,
    [Parameter(Mandatory)][string]$FFmpegSourceDirectory,
    [string]$PackageManifest = 'evidence/release-1.0.0/package-manifest.json',
    [string]$OutputDirectory = 'out/source-review'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
function Absolute([string]$path) { if ([IO.Path]::IsPathRooted($path)) { [IO.Path]::GetFullPath($path) } else { [IO.Path]::GetFullPath((Join-Path $root $path)) } }
$qtSource = Absolute $QtSourceDirectory; $ffSource = Absolute $FFmpegSourceDirectory
$output = Absolute $OutputDirectory
New-Item -ItemType Directory -Path $output -Force | Out-Null
$manifest = Get-Content -LiteralPath (Absolute $PackageManifest) -Raw | ConvertFrom-Json
$sbom = Get-Content -LiteralPath (Join-Path $root 'evidence/session-0/dependencies/qtbase-sbom.spdx.json') -Raw | ConvertFrom-Json
$packages = @{}; foreach ($package in $sbom.packages) { $packages[$package.SPDXID] = $package }
$selected = [Collections.Generic.HashSet[string]]::new()
$qtFiles = @(foreach ($file in $manifest.files | Where-Object { $_.source -like '*\.tools\qt\*' -and $_.path -like '*.dll' }) {
    if ((Get-FileHash -LiteralPath $file.source -Algorithm SHA256).Hash -ne $file.sha256) { throw "Qt binary differs from release evidence: $($file.path)" }
    $supplier = @($sbom.files | Where-Object { (Split-Path -Leaf $_.fileName) -eq (Split-Path -Leaf $file.path) })
    if ($supplier.Count -ne 1) { throw "Ambiguous/missing Qt SPDX file: $($file.path)" }
    $expected = ($supplier[0].checksums | Where-Object algorithm -eq 'SHA1').checksumValue
    $signature = Get-AuthenticodeSignature -LiteralPath $file.source
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'QT Company') { throw "Unexpected Qt signature: $($file.path)" }
    $bytes = [IO.File]::ReadAllBytes($file.source)
    $pe = [BitConverter]::ToInt32($bytes,60); $optional = $pe + 24
    if ([BitConverter]::ToUInt32($bytes,$pe) -ne 0x4550 -or [BitConverter]::ToUInt16($bytes,$optional) -ne 0x20b) { throw 'Expected a PE32+ Qt binary.' }
    $security = $optional + 144
    $certificateOffset = [BitConverter]::ToUInt32($bytes,$security)
    $certificateSize = [BitConverter]::ToUInt32($bytes,$security+4)
    if ($certificateSize -eq 0 -or $certificateOffset -le $security+8 -or $certificateOffset+$certificateSize -ne $bytes.Length) { throw 'Unexpected Qt certificate layout.' }
    # Reconstruct the unsigned supplier input in memory only. Shipped DLLs remain untouched.
    $unsigned = [byte[]]::new($certificateOffset)
    [Array]::Copy($bytes,$unsigned,$unsigned.Length)
    [Array]::Clear($unsigned,$security,8); [Array]::Clear($unsigned,$optional+64,4)
    $unsignedSha1 = [Convert]::ToHexString([Security.Cryptography.SHA1]::HashData($unsigned)).ToLowerInvariant()
    if ($unsignedSha1 -ne $expected) { throw "Qt code does not match the supplier SPDX file: $($file.path)" }
    foreach ($relation in $sbom.relationships | Where-Object { $_.relationshipType -eq 'CONTAINS' -and $_.relatedSpdxElement -eq $supplier[0].SPDXID }) { [void]$selected.Add($relation.spdxElementId) }
    [ordered]@{ path=$file.path; sha256=$file.sha256; spdxFile=$supplier[0].SPDXID; unsignedSha1=$unsignedSha1; expectedSha1=$expected; signature='Valid'; signer=$signature.SignerCertificate.Subject; matchesSupplier=$true }
})
if ($qtFiles.Count -ne 11) { throw "Expected the 11 Qt runtime files in release 1.0.0; found $($qtFiles.Count). Review changed scope." }
$queue = [Collections.Generic.Queue[string]]::new(); foreach ($id in @($selected)) { $queue.Enqueue($id) }
while ($queue.Count) {
    $id = $queue.Dequeue()
    foreach ($relation in $sbom.relationships | Where-Object { $_.relationshipType -eq 'DEPENDS_ON' -and $_.spdxElementId -eq $id }) {
        if ($selected.Add($relation.relatedSpdxElement)) { $queue.Enqueue($relation.relatedSpdxElement) }
    }
}
$qtCoverage = @(foreach ($id in $selected | Sort-Object) {
    $package = $packages[$id]
    if (-not $package) { throw "Missing SPDX dependency: $id" }
    $attribution = $null; $relative = $null
    if ($package.comment -match '/src_dir/qtbase/([^\r\n]+/qt_attribution.json)') {
        $relative = $Matches[1].Trim()
        $attributions = @(Get-Content -LiteralPath (Join-Path $qtSource $relative) -Raw | ConvertFrom-Json -AsHashtable)
        if ($package.comment -notmatch 'Entry index: (\d+)') { throw 'Missing attribution entry index.' }
        $index = [int]$Matches[1]
        # A single JSON object and a JSON array both occur in Qt's source tree.
        $attribution = $attributions[$index]
        if (-not $attribution['Id']) { throw "Missing Qt attribution: $relative entry $index" }
        foreach ($licenseFile in @($attribution['LicenseFile']) + @($attribution['LicenseFiles']) + @($attribution['CopyrightFile'])) {
            if ($licenseFile -and -not (Test-Path -LiteralPath (Join-Path (Split-Path -Parent (Join-Path $qtSource $relative)) $licenseFile))) { throw "Missing attribution material: $relative / $licenseFile" }
        }
    }
    [ordered]@{ name=$package.name; spdxId=$id; supplierLicense=$package.licenseDeclared; downloadLocation=$package.downloadLocation; sourceAttributionPath=$relative; sourceAttribution=$attribution }
})
$qtCoverage | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath (Join-Path $output 'qt-runtime-source-coverage.json') -Encoding utf8
$crt = @(foreach ($file in $manifest.files | Where-Object { $_.source -like '*\VC\Redist\*' }) {
    if ((Get-FileHash -LiteralPath $file.source -Algorithm SHA256).Hash -ne $file.sha256) { throw "Microsoft binary differs from release evidence: $($file.path)" }
    $signature = Get-AuthenticodeSignature -LiteralPath $file.source
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Microsoft') { throw "Unexpected Microsoft signature: $($file.path)" }
    [ordered]@{path=$file.path; sha256=$file.sha256; signature='Valid'; origin=$file.source; listedDirectory='VC/Redist (excluding debug_nonredist)'}
})
$ffPrefix = Join-Path $root $manifest.dependencies.packages[1].prefix
$ffFiles = @(foreach ($file in $manifest.files | Where-Object { $_.source -like '*\.tools\ffmpeg\*' -and $_.path -like '*.dll' }) {
    if ((Get-FileHash -LiteralPath $file.source -Algorithm SHA256).Hash -ne $file.sha256) { throw "FFmpeg binary differs from release evidence: $($file.path)" }
    [ordered]@{path=$file.path; sha256=$file.sha256; fileVersion=$file.fileVersion; matchesRelease=$true}
})
if ($ffFiles.Count -ne 6) { throw "Expected six shipped FFmpeg DLLs; found $($ffFiles.Count). Review changed scope." }
$readme = Get-Content -LiteralPath (Join-Path $ffPrefix 'README.txt')
$versions = @{}; $inVersions = $false
foreach ($line in $readme) {
    if ($line -match "^release-full external libraries' versions:") { $inVersions=$true; continue }
    if ($inVersions -and $line -match '^(\S+)\s+(.*)$') { $versions[$Matches[1].ToLowerInvariant()]=$Matches[2].Trim() }
}
$aliases = @{ avisynth='avisynthplus'; libaom='aom'; libaribb24='aribb24'; libaribcaption='aribcaption'; libbs2b='bs2b'; libcodec2='codec2'; libdav1d='dav1d'; libdavs2='davs2'; libdvdnav='dvdnav'; libdvdread='dvdread'; libflite='flite'; libfreetype='freetype'; libfribidi='fribidi'; libgsm='gsm'; libharfbuzz='harfbuzz'; liblc3='lc3'; liblensfun='lensfun'; libmp3lame='lame'; liboapv='openapv'; libopenjpeg='openjpeg2'; libopenmpt='openmpt'; libopus='opus'; libqrencode='qrencode'; libquirc='quirc'; librav1e='rav1e'; librist='rist'; librubberband='rubberband'; libshine='shine'; libsnappy='snappy'; libspeex='speex'; libsrt='srt'; libsvtav1='svt-av1'; libsvtjpegxs='svt-jpeg-xs'; libtheora='libtheora'; libtwolame='twolame'; libuavs3d='uavs3d'; libvidstab='vidstab'; libvmaf='vmaf'; 'libvo-amrwbenc'='vo-amrwbenc'; libvorbis='vorbis'; libvpl='vpl'; libvpx='vpx'; libvvenc='vvenc'; libx264='x264'; libx265='x265'; libxavs2='xavs2'; libxevd='xevd'; libxeve='xeve'; libxvid='xvid'; libzimg='zimg'; libzmq='zeromq'; libzvbi='zvbi'; ladspa='ladspa-sdk'; openal='openal-soft'; opencl='opencl-headers'; sdl2='sdl'; vulkan='vulkan-loader'; whisper='whisper.cpp'; libcdio='libcdio-paranoia' }
$hardware = @('cuda-llvm','cuvid','d3d11va','d3d12va','dxva2','mediafoundation','nvdec','nvenc','vaapi')
$deviceOnly = @('libcaca','libcdio','openal','sdl2')
$runtime = Get-Content -LiteralPath (Join-Path $root 'evidence/release-1.0.0/ffmpeg-runtime.json') -Raw | ConvertFrom-Json
$review = @([regex]::Matches($runtime.configuration, '--enable-([^\s]+)') | ForEach-Object {
    $component=$_.Groups[1].Value
    if ($component -notin @('gpl','version3','shared')) { [pscustomobject]@{component=$component; configurationFlag=$_.Value} }
} | Sort-Object component)
$ffReview = @(foreach ($row in $review) {
    $component=$row.component; $key=if ($aliases.ContainsKey($component)) { $aliases[$component] } else { $component }
    $version=$versions[$key]
    $scope=if ($component -in $hardware) { 'Hardware/compiler feature; not a separately identified linked library' } elseif ($component -in $deviceOnly) { 'Direct integration is confined to unshipped avdevice/ffplay; transitive linkage still requires supplier link inputs' } else { 'Enabled external integration in shipped FFmpeg libraries' }
    [ordered]@{component=$component; configurationFlag=$row.configurationFlag; supplierVersion=$version; scope=$scope; sourceStatus=if ($component -in $hardware) { 'Review applicable headers/runtime dependencies separately' } elseif ($version) { 'Supplier version identified; exact patched source/build/notices not yet established' } else { 'Supplier version missing; exact source/build/notices not established' }; externalDistributionApproved=$false }
})
$ffReview | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output 'ffmpeg-source-coverage.json') -Encoding utf8
# Confirm the actual source integration sites rather than treating every enabled flag as shipped code.
$deviceEvidence = @(foreach ($pair in @(@('libcaca','CONFIG_CACA_OUTDEV'),@('libcdio','CONFIG_LIBCDIO_INDEV'),@('openal','CONFIG_OPENAL_INDEV'))) {
    $matches=Select-String -LiteralPath (Join-Path $ffSource 'libavdevice/Makefile') -SimpleMatch $pair[1]
    if (-not $matches) { throw "Missing expected source integration: $($pair[0])" }
    [ordered]@{component=$pair[0];source='libavdevice/Makefile';lines=@($matches | ForEach-Object Line)}
})
$report = [ordered]@{
    schemaVersion=1; reviewedUtc=[DateTime]::UtcNow.ToString('o'); release='1.0.0'; completeCorrespondingSource=$false; externalDistributionApproved=$false
    qtFiles=$qtFiles; qtDependencyCount=$qtCoverage.Count; qtSourceRevision='5a1194b2d368e2d72c230205c464b04a2f23eccd'; microsoftFiles=$crt
    ffmpegSourceRevision='946fcce07b6dcd0331c8cc609192aeff5e1924f8'; ffmpegFiles=$ffFiles; ffmpegConfiguredIntegrationCount=$ffReview.Count
    ffmpegMissingSupplierVersions=@($ffReview | Where-Object { -not $_.supplierVersion -and $_.component -notin $hardware -and $_.component -notin $deviceOnly } | ForEach-Object component)
    deviceIntegrationEvidence=$deviceEvidence
    limits=@('Qt normalized SHA1 verifies the unsigned SPDX input; archive and shipped binaries have independent SHA256 pins','Version labels alone do not establish supplier patches, transitive linkage or build scripts','No fresh dependency rebuild performed by this review','Source links must remain available when binary downloads are public')
}
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $output 'dependency-source-review.json') -Encoding utf8
Write-Host "Verified $($qtFiles.Count) Qt binary identities, $($qtCoverage.Count) Qt dependency records and $($crt.Count) unmodified Microsoft files. FFmpeg corresponding source remains incomplete."

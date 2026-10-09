[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$lock = Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$cmake = $lock.buildTools.cmake.path
$qt = Join-Path $root $lock.packages[0].prefix
$ffmpeg = Join-Path $root $lock.packages[1].prefix
$source = Join-Path $root 'evidence/session-0/toolchain-check'
$build = Join-Path $root 'build/session-0-dependency-check'
$log = Join-Path $root 'evidence/session-0/dependencies/toolchain-check.txt'
$version = (& $cmake --version | Select-Object -First 1)
if ($version -ne "cmake version $($lock.buildTools.cmake.version)") { throw "CMake version drift: $version" }
$compilerVersion = (Get-Item -LiteralPath $lock.buildTools.msvc.path).VersionInfo.FileVersion
if ($compilerVersion -notlike "$($lock.buildTools.msvc.compilerVersion)*") { throw "MSVC version drift: $compilerVersion" }
& $cmake -S $source -B $build -G 'Visual Studio 18 2026' -A x64 -T "version=$($lock.buildTools.msvc.toolsetDirectoryVersion)" "-DCMAKE_SYSTEM_VERSION=$($lock.buildTools.windowsSdkVersion)" "-DCMAKE_PREFIX_PATH=$qt;$ffmpeg" 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw 'Dependency configure failed.' }
& $cmake --build $build --config Release 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { throw 'Dependency build failed.' }
$originalPath = $env:PATH
try {
    $env:PATH = "$qt\bin;$ffmpeg\bin;$originalPath"
    & (Join-Path $build 'Release/dependency-check.exe') 2>&1 | Tee-Object -FilePath $log -Append
    if ($LASTEXITCODE -ne 0) { throw 'Dependency runtime check failed.' }
} finally { $env:PATH = $originalPath }
Write-Host 'C++20, Qt Widgets, FFmpeg import libraries and decoder lookup passed without opening a UI.'

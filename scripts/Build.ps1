[CmdletBinding()]
param([string]$BuildDirectory = 'build/1.0.0')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$lock = Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$cmake = $lock.buildTools.cmake.path
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $root $BuildDirectory }
foreach ($path in @($cmake, $lock.buildTools.msvc.path, (Join-Path $root "$($lock.packages[0].prefix)/lib/cmake/Qt6/Qt6Config.cmake"), (Join-Path $root "$($lock.packages[1].prefix)/lib/avutil.lib"))) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Pinned dependency missing: $path. See docs/build.md." }
}
$version = (& $cmake --version | Select-Object -First 1)
if ($version -ne "cmake version $($lock.buildTools.cmake.version)") { throw "CMake version drift: $version" }
$compilerVersion = (Get-Item -LiteralPath $lock.buildTools.msvc.path).VersionInfo.FileVersion
if ($compilerVersion -notlike "$($lock.buildTools.msvc.compilerVersion)*") { throw "MSVC version drift: $compilerVersion" }
New-Item -ItemType Directory -Path $build -Force | Out-Null
& $cmake -S $root -B $build -G 'Visual Studio 18 2026' -A x64 -T "version=$($lock.buildTools.msvc.toolsetDirectoryVersion)" "-DCMAKE_GENERATOR_INSTANCE=$($lock.buildTools.visualStudio.path)" "-DCMAKE_SYSTEM_VERSION=$($lock.buildTools.windowsSdkVersion)" 2>&1 | Tee-Object -FilePath (Join-Path $build 'configure.log')
if ($LASTEXITCODE -ne 0) { throw 'Application configure failed; see configure.log.' }
& $cmake --build $build --config Release --parallel 2>&1 | Tee-Object -FilePath (Join-Path $build 'build.log')
if ($LASTEXITCODE -ne 0) { throw 'Application build failed; see build.log.' }
Write-Host "Built runnable native editor: $build\Release\VideoEditor.exe"

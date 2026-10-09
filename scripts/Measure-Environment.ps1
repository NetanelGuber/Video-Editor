[CmdletBinding()]
param([string]$OutputDirectory = (Join-Path $PSScriptRoot '../evidence/session-0/environment'))
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
if (-not ('Session0EnvironmentDpi' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class Session0EnvironmentDpi {
    public delegate bool MonitorCallback(IntPtr monitor, IntPtr hdc, IntPtr rect, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumDisplayMonitors(IntPtr hdc, IntPtr rect, MonitorCallback callback, IntPtr data);
    [DllImport("shcore.dll")] static extern int GetDpiForMonitor(IntPtr monitor, int type, out uint x, out uint y);
    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
    [DllImport("user32.dll")] static extern uint GetDpiForSystem();
    public static uint[] Read() {
        var old = SetThreadDpiAwarenessContext(new IntPtr(-3)); // per-monitor aware
        try {
            var values = new List<uint>();
            values.Add(GetDpiForSystem());
            EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, (m,h,r,d) => {
                uint x,y; if (GetDpiForMonitor(m,0,out x,out y) == 0) values.Add(x);
                return true;
            }, IntPtr.Zero);
            return values.ToArray();
        } finally { SetThreadDpiAwarenessContext(old); }
    }
}
'@
}
$os = Get-CimInstance Win32_OperatingSystem
$windows = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
$dpi = [Session0EnvironmentDpi]::Read()
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$visualStudio = if (Test-Path -LiteralPath $vswhere) { (& $vswhere -all -products '*' -format json | ConvertFrom-Json) } else { @() }
$tools = @($visualStudio | ForEach-Object {
    $root = $_.installationPath
    $msvcRoot = Join-Path $root 'VC/Tools/MSVC'
    [ordered]@{
        name=$_.displayName; version=$_.catalog.productDisplayVersion; path=$root
        msvc=@(Get-ChildItem -LiteralPath $msvcRoot -Directory -ErrorAction SilentlyContinue | ForEach-Object {
            $compiler = Join-Path $_.FullName 'bin/Hostx64/x64/cl.exe'
            [ordered]@{toolsetDirectoryVersion=$_.Name; compilerPath=$compiler; compilerFileVersion=$(if (Test-Path -LiteralPath $compiler) { (Get-Item -LiteralPath $compiler).VersionInfo.FileVersion })}
        })
        cmakePath=(Join-Path $root 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
        ninjaPath=(Join-Path $root 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe')
    }
})
$sdk = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Microsoft SDKs\Windows\v10.0'
[ordered]@{
    schemaVersion=1; collectedAtUtc=[DateTime]::UtcNow.ToString('o')
    windows=[ordered]@{caption=$os.Caption; displayVersion=$windows.DisplayVersion; version=$os.Version; fullBuild="$($windows.CurrentBuild).$($windows.UBR)"; architecture=$os.OSArchitecture; registryProductName=$windows.ProductName}
    cpu=@(Get-CimInstance Win32_Processor | ForEach-Object Name)
    ramBytes=(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory
    gpu=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,DriverDate,VideoModeDescription,CurrentHorizontalResolution,CurrentVerticalResolution,CurrentRefreshRate)
    display=[ordered]@{systemDpi=$dpi[0]; systemScalePercent=($dpi[0]/96*100); monitorDpi=@($dpi | Select-Object -Skip 1); monitorScalePercent=@($dpi | Select-Object -Skip 1 | ForEach-Object { $_/96*100 })}
    disks=@(Get-CimInstance Win32_LogicalDisk -Filter 'DriveType=3' | Select-Object DeviceID,VolumeName,Size,FreeSpace)
    visualStudio=$tools
    windowsSdk=[ordered]@{installationFolder=$sdk.InstallationFolder; version=$sdk.ProductVersion; includeVersions=@(Get-ChildItem -LiteralPath (Join-Path $sdk.InstallationFolder 'Include') -Directory | ForEach-Object Name)}
    pathTools=@(Get-Command cmake,cl,ffmpeg,ffprobe,qmake,qtpaths,ninja,git -ErrorAction SilentlyContinue | Select-Object Name,Source)
} | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'inventory.json') -Encoding utf8
foreach ($tool in $tools) {
    if (Test-Path -LiteralPath $tool.cmakePath) { & $tool.cmakePath --version | Set-Content -LiteralPath (Join-Path $OutputDirectory 'cmake-version.txt') }
    if (Test-Path -LiteralPath $tool.ninjaPath) { & $tool.ninjaPath --version | Set-Content -LiteralPath (Join-Path $OutputDirectory 'ninja-version.txt') }
}
Write-Host "Saved environment inventory to $OutputDirectory"

# Session 16 — Personal Windows package and release regression

Complete and accepted by the user on 2026-10-09. Version 0.16.0, project schema 11. The deliverable is a portable **personal local** Windows x64 ZIP, not an externally published release. No application license is assigned and no artifact is uploaded. A new Windows account or clean OS has not been created; clean-profile evidence below means isolated application data on the target PC.

The pinned Release build and all nineteen suites passed (153.38 seconds). Fresh package verification passed 23 production workflow checks, 80 payload file hashes and 396 PE import resolutions, with 79 actual process module paths recorded. All 30 original footage files retain their Session 0 size/mtime. [Verification summary](../evidence/session-16/verification.json), [package result](../evidence/session-16/package-result.json) and [manifest](../evidence/session-16/package-manifest.json) retain the evidence. [User acceptance](../evidence/session-16/manual-acceptance.json) closes this session; individual native appearance-check outcomes were not separately reported.

## Build, package and verify

```powershell
./scripts/Build.ps1
./scripts/Test-Application.ps1
./scripts/New-WindowsPackage.ps1 -OutputDirectory out/release
./scripts/Test-WindowsPackage.ps1 -Archive out/release/VideoEditor-0.16.0-windows-x64-local.zip -OutputDirectory build/session-16-release-test
./scripts/Collect-Session16Evidence.ps1 -PackageTestDirectory build/session-16-release-test
```

Packaging and package-test scripts refuse existing output directories/artifacts. Choose new output locations to repeat them; they never recursively delete or overwrite previous releases. Build/test scripts remain repeatable. The ZIP plus `.sha256` sidecar is the installation artifact: extract its whole directory and open `VideoEditor.exe`. No administrator privileges, global PATH/profile changes, developer tools, shortcut or file association are required for runtime.

The package/verification scripts require PowerShell 7. Their developer tools are used to build or inspect artifacts, not by the installed editor.

The package includes only the two production executables, required pinned FFmpeg DLLs, Qt's shared Core/Gui/Widgets/OpenGL/OpenGLWidgets modules and selected platform/style/image plugins, app-local MSVC CRT, a relative `qt.conf`, documentation, notices and a small synthetic example. Development test executables, Qt Test, `.lib`/`.pdb` files, Qt tooling, FFmpeg CLI tools and unrelated build files are omitted. Optional Direct3D 12 shader compiler DLLs and software OpenGL are omitted because the editor uses QPainter/OpenGL with CPU presentation fallback and D3D11VA decode. Windows 11 supplies UCRT/system/driver DLLs.

Qt deployment runs the pinned `windeployqt` with `--no-patchqt`, so every packaged DLL remains byte-identical to its recorded origin. `qt.conf` makes plugin/prefix paths relative. The main executable has a Windows version resource. `build/package-manifest.json` records every payload file except itself, SHA-256/size/version/origin, dependency pins and source digests. The entire archive has a separate checksum. This is repeatable packaging from a pinned build, not a claim of byte-identical fresh compiler outputs or ZIP timestamps.

`ExportCapabilityProbe --inventory` records capabilities from the actual linked FFmpeg DLLs. The package also retains supplier CLI configuration/library/encoder/decoder/muxer/filter/hardware inventories. The export dialog still discovers settings asynchronously and validates the exact tuple. The existing media-cache environment override now applies to capabilities too, allowing both caches to be isolated during verification without touching normal settings.

## Automated release checklist

- Pinned Release configure/build; all nineteen application regression suites, including the new first-workflow suite.
- Fresh ZIP extraction, checksum verification, exact file list/hash comparison and Windows x64 PE dependency checks for every shipped executable/DLL/plugin.
- Packaged `VideoEditor.exe` startup and project lifecycle through its offscreen shell check.
- Packaged helper inventory equal to the manifest's retained runtime inventory.
- A separate test harness linked to production MainWindow/timeline/export code, temporarily placed alongside the extracted DLLs; never included in the ZIP. PATH contains only Windows locations. Explicit settings/log/recovery paths and a fresh cache isolate the workflow from normal user data.
- New project, async example import, video/audio placement, Split action, undo/redo, atomic save and reopen with normalized media paths.
- Fresh capability detection, Simple H.264/AAC MP4 and Advanced named-x264 Matroska export at 320×180 with the source's automatic 60/1 FPS; independent FFprobe metadata and complete FFmpeg decode. Independent inspection tools run outside the app and are not runtime dependencies.
- Cancel an export over an existing sentinel destination; verify exact preservation. Kill an autosaved child process, restart, use the production recovery dialog and save the recovered edit; verify last-good-save bytes.
- Capture actual loaded DLL paths and require Qt/FFmpeg/MSVC to load from the extracted package; verify execution leaves the packaged payload unchanged.
- Retain guide, format/performance limits, Qt component attributions/full supplier SPDX/custom/standard license texts, FFmpeg GPL/configuration/source reference, MSVC distributable list and explicit external-distribution blockers.

The evidence collector copies reports/logs, preserves archive and payload identities, and checks the retained original-footage size/mtime inventory. It refuses failed or incomplete regression/package reports. No computer use is performed.

## First-use documentation and remaining evidence

[Quick start](quick-start.md) describes extraction, first edits, saving, choosing Simple/Advanced, offline media and recovery. [Supported formats](supported-formats.md) separates real footage, synthetic fixtures, compiled codecs and physical-device claims. [License review](release-license-review.md) retains exact component evidence and unresolved corresponding-source/application-license/supplier attribution obligations before sharing beyond this PC.

Automated offscreen checks cannot establish the native Windows platform plugin's visible launch/readability at the user's display scale. The previously requested focused human check was:

1. Extract `out\release\VideoEditor-0.16.0-windows-x64-local.zip` to a folder outside the workspace, and open its `VideoEditor.exe`. Confirm **Project Media**, **Sequence**, **timeline** and **Inspector** are visible/readable and there is no missing-DLL or platform-plugin error.

This focused check does not certify other computers, a genuinely fresh Windows account, physical playback devices, subjective audio/video sync, maximum-layer 4K RAM, real phone/HDR sources or long hardware stability. Those limitations remain documented. Public distribution remains outside the local release scope until its license/source blockers are resolved.

## User acceptance

The user stated, "Alright it's good. Mark session 16 as complete.", accepting the implemented local release and authorizing completion. No further human check is required for this session. Individual native launch/readability outcomes were not separately reported, so user acceptance remains separate from agent-run evidence. The shipped ZIP and its recorded hashes remain unchanged; this acceptance is recorded in the workspace after packaging.

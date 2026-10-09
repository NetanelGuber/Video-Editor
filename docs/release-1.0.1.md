# 1.0.1 release

Version 1.0.1 adds automatic and manual sequence ends. New projects and sequences
follow the last clip after every edit. **Sequence → Automatic end (fit to clips)**
enables this for an older project. **Set sequence end to playhead** and **Set
sequence end manually…** switch to manual mode. All three commands are also in
the **Sequence end: Auto/Manual** menu above the timeline. Preview, export,
undo/redo and saved projects use the same frame-exact end.

Automatic end counts every track, including disabled/locked tracks, audio,
titles and nested clips. Manual resizing rejects an end inside an existing clip;
trim or delete that content first. Shortening clips preserves a manual end;
adding or extending content beyond it still grows the sequence to fit. Empty
automatic sequences have zero duration.

Project schema 12 stores the mode. Schemas 1–11 migrate to manual mode, preserving
their saved duration and edits. A file saved by 1.0.1 requires 1.0.1 or newer;
save to a new file if you need to keep a project readable by 1.0.0.

## Portable package

[Release and downloads](https://github.com/NetanelGuber/Video-Editor/releases/tag/v1.0.1)
provide `VideoEditor-1.0.1-windows-x64.zip` and its `.zip.sha256` checksum.
Extract the entire ZIP and open `VideoEditor.exe`. The executable resource,
application title and generated build header identify version 1.0.1.
Windows 11 x64 is the tested platform; the package is unsigned.

Dependencies and their binary bytes remain pinned. The existing
[dependency source/build gaps](dependency-source-review.md) remain unresolved.
The package retains preparation-time draft/source-review labels and
`externalDistributionApproved=false`; those fields describe the source review,
not GitHub publication state. Publication is recorded separately.

## Verification

[Release verification](../evidence/release-1.0.1/verification.json) records the
Release build, full regression and fresh extracted-package workflow.
All **19/19 regression suites**, **28 packaged workflow checks**, **94 payload
hashes** and **396 x64 import resolutions** passed on the target PC.
The new tests cover automatic growth/shrink through inserts, deletes, moves,
trims and track removal; manual overrides; exact fractional frame timing;
locked/disabled tracks; empty sequences; mode/duration undo/redo; native manual
duration input and cancellation; schema migration and save/reopen.

The packaged production actions set and fit the sequence end before exporting.
Independent FFprobe frame counting and FFmpeg full decode verify the one-second,
60-frame H.264/AAC MP4 and MKV outputs have no trailing empty video.
The package check also verifies every payload hash, x64 import and actual
loaded DLL path under an isolated profile and Windows-only PATH.

These are automated/offscreen checks on the target PC. No computer-use testing,
separate machine/clean Windows account, visible native display scaling or
subjective AV-sync validation is claimed.

## Reproduce

```powershell
./scripts/Build.ps1
./scripts/Test-Application.ps1
./scripts/New-WindowsPackage.ps1 -OutputDirectory out/release-1.0.1
./scripts/Test-WindowsPackage.ps1 -Archive out/release-1.0.1/VideoEditor-1.0.1-windows-x64.zip
./scripts/Collect-ReleaseEvidence.ps1
```

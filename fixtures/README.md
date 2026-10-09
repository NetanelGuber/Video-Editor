# Session 0 media test set

Six original clips are referenced by absolute path in [test-set/inventory.json](../evidence/session-0/test-set/inventory.json); they stay in `B:\Videos`. The inventory includes SHA-256, exact byte sizes, stream metadata, first-five-second decoded frame timing, and full-file packet timing. See [the report](../docs/session-0.md) for why each clip was selected.

Four synthetic files under `generated/pinned/` cover 4K H.264 + audio, 4K 10-bit HEVC SDR + audio, VFR, and display rotation. Generated binaries are ignored and can be recreated with the pinned dependencies. Use a fresh output directory: the generator refuses to overwrite existing fixture files.

From the project root:

```powershell
./scripts/Install-Dependencies.ps1
$ffmpeg = (Resolve-Path './.tools/ffmpeg/ffmpeg-9.0.2-full_build-shared/bin/ffmpeg.exe').Path
$ffprobe = (Resolve-Path './.tools/ffmpeg/ffmpeg-9.0.2-full_build-shared/bin/ffprobe.exe').Path

# Run this only when fixtures/generated/pinned does not already contain these files.
./scripts/New-MediaFixtures.ps1 -FFmpeg $ffmpeg -OutputDirectory './fixtures/generated/pinned'
./scripts/Measure-Media.ps1 -Path './fixtures/generated/pinned' -FFprobe $ffprobe `
    -InspectTiming -InspectFullTiming -Hash -OutputDirectory './evidence/session-0/synthetic'

# Refresh the full real-footage metadata inventory (read-only source access).
./scripts/Measure-Media.ps1 -Path 'B:\Videos' -Recurse -FFprobe $ffprobe `
    -OutputDirectory './evidence/session-0/all-media'

# Refresh the six selected originals, using their recorded paths.
$paths = (Get-Content './evidence/session-0/test-set/inventory.json' -Raw | ConvertFrom-Json).files.path
./scripts/Measure-Media.ps1 -Path $paths -FFprobe $ffprobe -InspectTiming -InspectFullTiming -Hash `
    -OutputDirectory './evidence/session-0/test-set'

./scripts/Test-MediaDecode.ps1 -FFmpeg $ffmpeg `
    -Inventory './evidence/session-0/test-set/inventory.json','./evidence/session-0/synthetic/inventory.json'
```

`Measure-Media.ps1` only reads input files. Its reports are written to the requested output directory. `Test-MediaDecode.ps1` decodes CPU video and all audio streams to a null output for up to five seconds and records errors; it is not a playback or performance test. Generating or probing 10-bit SDR does not validate HDR tone mapping. Synthetic coverage was approved as the starting point; add real 4K/phone/HDR clips when available.
# Project persistence fixtures

`projects/multitrack-v2.veproject` and `projects/multitrack-v1.veproject` are checked-in synthetic JSON project documents, not generated media. They cover multiple sequences/tracks/media, audio, Unicode titles, opaque effect parameters, precise int64 values, and schema migration. Referenced files are intentionally absent, exercising offline state. See [project format](../docs/project-format.md) and [Session 2 tests](../docs/session-2.md). Open them read-only and use Save As to a disposable folder for human testing.

# Session 3 disposable inspection/relink fixtures

`./scripts/Prepare-Session3Tests.ps1` prepares `generated/session-3/import/`: a short H.264/AAC video in a subfolder, stereo PCM audio, invalid media, and a text file that folder import ignores. `./scripts/Test-Application.ps1` also creates `generated/session-3/offline.veproject` and a Unicode-named replacement copy. These are disposable generated fixtures; the scripts never move or modify `B:\Videos` footage. The prepared files support the [simple Session 3 checklist](../docs/session-3.md#human-windows-checklist). Normal import references media paths without making copies; the Unicode copy is only a relink test fixture.
## Session 4 deterministic timeline fixture

`projects/editing-v2.veproject` is a small schema-2 model fixture with stable IDs, overlapping/gapped video, 23.976/144 fps source metadata in a 29.97 fps sequence, 48/44.1 kHz audio, an opaque effect, and a title. Its synthetic source paths are intentionally offline; no media files are needed for `TimelineTests`. The suite creates a disposable edited project under `build/session-4/timeline-test-data/edited-roundtrip.veproject` and verifies real repeated atomic save/reopen. See [Session 4](../docs/session-4.md) for the edit and rounding rules.

## Session 5 playback fixtures

`scripts/Prepare-Session5Tests.ps1` creates `generated/session-5/flash-beep.mp4`: twelve seconds of a 60 fps pattern with a white square and 880 Hz beep together every second. `Test-Application.ps1` prepares it and the native playback suite writes `generated/session-5/playback.veproject`: a seven-second, 30 fps sequence with separate video/audio clips, a one-second gap at three seconds, and a second trimmed source section. Open these for the [simple Session 5 checklist](../docs/session-5.md#human-windows-checklist). Generated files are disposable and source footage stays untouched.

## Session 6 first-cut fixture

`Test-Application.ps1` runs the timeline interface suite, which prepares `generated/session-6/first-cut.veproject`. It references the Session 5 flash/beep source, with three-second video/audio clips at frame 0, an empty Video 2, and a twelve-second sequence at 30 fps. Use Save As to `generated/session-6/my-first-cut.veproject` before following the [Session 6 checklist](../docs/session-6.md#human-windows-checklist). Tests recreate the disposable starting project; your separately named test copy is retained. The edited automated round-trip file lives under `build/session-6/timeline-ui-test-data/edited.veproject`.

## Session 7 audio/title fixtures

`projects/multitrack-v3.veproject` is the schema-3 offline persistence example with nonzero audio fades. The v1/v2/v3 files remain explicit predecessor migration fixtures; schema 4 adds effect animation fields.

`Prepare-Session7Tests.ps1` generates a twelve-second 440 Hz stereo soundtrack. `AudioTitleTests` writes `generated/session-7/audio-titles.veproject`: twelve seconds of flash/beep video and audio, a soundtrack at frames 90–360 with 30-frame fades, and a styled title at frames 30–210. Use Save As to `generated/session-7/my-audio-titles.veproject` before the [Session 7 checklist](../docs/session-7.md#human-windows-checklist). The test recreates only the starting project; separately named copies survive. Known PCM constant/impulse WAVs and sample comparison reports are written under `build/session-7/audio-title-test-data/`.

## Session 8 export fixtures

`ExportTests` prepares `generated/session-8/green.mp4`, `export-reference.veproject` (a six-second cut with trims, an upper green clip, a black video gap, a styled title and mixed/faded audio) and `cancel-long.veproject` (one minute of repeated flash/beep footage for cancellation). Use Save As to `my-export-test.veproject` before the [Session 8 checklist](../docs/session-8.md#human-windows-checklist). The test recreates the starting fixtures; separately named projects and exports survive. Independent export MP4s, decoded reference stills and metadata live under `build/session-8/export-test-data/`.

## Session 9 recovery and moved-original fixtures

`RecoveryTests` prepares `generated/session-9/recovery-offline.veproject`: trimmed video/audio, nonzero audio gain/fades and a styled title, with absent paths under `old/`. Original copies are in `generated/session-9/moved/` and `moved/subfolder/`, ready for **File → Relink offline media from folder…**. Real footage remains untouched. The schema 1/2/3 multitrack examples remain migration fixtures. Killed/restarted editor processes use isolated disposable app data under `build/session-9/recovery-test-data/`; their reports and rendered recovery/timeline captures are copied to `evidence/session-9/`. See [Session 9](../docs/session-9.md).

## Session 10 4K performance project

`PerformanceTests` prepares `generated/session-10/4k-editing.veproject`: a twenty-second, 3840×2160, 30 fps sequence alternating the existing synthetic 4K H.264 and HEVC 10-bit SDR sources, with a title and faded audio. The user confirmed the focused visible-viewer check with this project; see [Session 10](../docs/session-10.md). The independently probed/decoded four-second original-media 4K export is `build/session-10/performance-test-data/4k-original-export.mp4`. Tests keep caches, source-mutation copies and results under the chosen build directory; originals and prior working projects are untouched.

## Session 11 effect/keyframe/fade reference

`projects/effects-v4.veproject` is the static-ID schema-4 persistence example with offline synthetic paths, animated transform/opacity, blend modes and independent video fade colors/durations/curves. The previous v1/v2/v3 files exercise explicit migration. `EffectsTests` regenerates only the separate `generated/session-11/red.mp4`, `blue.mp4` and `effects-reference.veproject`: twelve one-second sections covering opacity, black/white/custom color, overlapping fades, crop/transform, multiply/screen, keyframes and a gap in the lower video. Use Save As for any edits you want to keep. The suite's independently decoded exports, dialog image and result live in `build/session-11/effects-test-data/`; see [Session 11](../docs/session-11.md).


## Session 12 references

`AdvancedEffectsTests` regenerates `generated/session-12` automatically. The test set contains moving SDR video with a 440 Hz tone, a blue lower layer, a green/red key subject, a channel-swapping normalized 3D Cube LUT, and gated audio timing pulses. Projects `color-mask-key-lut.veproject`, `speed-ramp.veproject` and `audio-ramp.veproject` exercise preview/export and pitch-preserving speed ramps. They are disposable and contain local relative references when saved. Original user footage is untouched.

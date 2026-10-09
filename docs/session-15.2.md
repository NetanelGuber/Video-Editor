# Session 15.2 — Simple and Advanced export tabs

Complete and accepted by the user on 2026-10-09. Version 0.15.4, project schema 11. The [annotated concept](session-15.2-concept.md) was completed before implementation. Automated acceptance and artifact/source hashes are recorded in [verification.json](../evidence/session-15.2/verification.json); the user authorized completion in [manual-acceptance.json](../evidence/session-15.2/manual-acceptance.json). Individual Windows appearance-check outcomes were not separately reported.

## Using the dialog

Open **File → Export video…** or press **Ctrl+E**. Destination, an effective size/FPS/format/audio summary, validation, progress, compatible reset and Export/Cancel export/Close actions are shared by both tabs.

**Simple** has three dropdowns and one checkbox: Playback target, Video size, Picture quality and Include audio. It uses the four existing compatible targets, software x264/x265, medium speed, automatic pixel format, native quality 20/18/24 and 192 kbit/s AAC stereo. Size can follow the sequence or fit within 1080p/720p while preserving aspect without upscaling. Frame rate automatically follows Primary Video, falling back to the sequence. Broad MP4 is disabled when the current automatic rate or size exceeds its limits; the reason is in its tooltip.

**Advanced** groups encoder/hardware and container, size and exact rational FPS, quality/rate control and preset, pixel/color format, audio codec/bitrate, and expandable profile/tune/native options. Its controls use the existing bundled descriptors and runtime checks. Hardware is selected through a named encoder or Automatic policy and retains the fresh exact probe requirement. Audio bitrate is expressed in kbit/s in the UI and bits/s in persistence and codec configuration; FLAC/PCM disable it. MP3/AC-3 offer discrete rates. Audio remains the existing 48 kHz stereo mix. The color explanation identifies BT.709 SDR and the current 8-bit composition limit; selecting 10-bit storage does not produce HDR.

The Automatic FPS checkbox makes intent explicit. Turning it off preserves manual intent even when the current value equals Primary Video/sequence FPS. Turning it on restores the exact automatic rate. Changing a numerator or denominator sets manual intent. No export option retimes timeline clips.

Tab switching preserves the shared settings. Custom, detailed and stale saved settings initially open Advanced. If settings cannot be represented by Simple, its controls disable with a notice that the Advanced settings remain active. Choosing **Use compatible defaults** explicitly clears overrides, restores Desktop MP4/software/native quality 20/medium/automatic pixels and FPS/192 kbit/s AAC, and fits an even size within 1080p. It keeps destination and audio inclusion, updates the extension and starts validation. It does not bypass destination, media or capability errors.

## Conflicts and persistence

Known incompatible choices are absent or disabled where practical. Retained stale values remain visible and immediately warn with their conflicting setting and a correction. Native-option errors reveal their group. Background catalog progress cannot replace an already-known conflict warning. All changed tuples invalidate prior capability success; Export remains blocked until structural/compiled validation and the complete exact probe succeed. Detailed probe results remain behind **Export details…**. The existing cancellation, atomic output publication, overwrite confirmation and actual encoder/fallback completion feedback remain in use.

Schema 11 stores `audioBitrate`, migrating prior schemas with the former 192000 bits/s default. An older-schema label cannot silently discard a nondefault newer bitrate. Exact probe/helper requests and broad catalog cache identities include audio bitrate. Save/reopen and undo/redo retain native overrides, fractional quality/FPS, audio settings and manual-rate intent. Tab selection is inferred presentation state, not a separate conflicting export-settings record.

## Verification

```powershell
./scripts/Build.ps1
./scripts/Test-Application.ps1
./scripts/Collect-Session15_2Evidence.ps1
```

The final Release build passed all eighteen suites in 142.20 seconds. The expanded export-tab suite then passed all 82 checks in 4.30 seconds, adding explicit portrait/landscape sizing and shared target selection without further product changes. The eighteen-suite regression covers project migrations, timeline/undo, playback, production widgets, import/relink/recovery, 4K composition/cache workloads, effects, nested/multicamera editing, encoder inventory/device probes and capability caching. `ExportTabsTests` exercises both production tabs, their small/grouped control sets, state synchronization, manual/automatic FPS, lossless/discrete audio dependencies, cache identities, reset, stale settings and narrow geometry. It exports through the real dialog and independently uses the bundled FFprobe/FFmpeg to inspect and decode Simple, equal-rate manual, and Advanced outputs. The Advanced sample verifies 30000/1001 FPS, x264 High 10, native profile/tune retention, fractional CRF and 128 kbit/s MP3. Requested bitrate is also verified in the shared codec configuration. Native flags not represented in stream metadata are verified through persistence and the exact probe rather than inferred from FFprobe.

The [FFmpeg codec documentation](https://ffmpeg.org/ffmpeg-codecs.html), [formats documentation](https://ffmpeg.org/ffmpeg-formats.html) and [AVOptions documentation](https://ffmpeg.org/doxygen/trunk/group__avoptions.html) were consulted during concept and implementation. Bundled 9.0.2 help for x264/AAC/Opus/MP3/AC-3 and local audio encodes were checked; retained help/CLI checks are under `evidence/session-15.2`. Bundled descriptors and exact probes take precedence over online examples.

Offscreen captures with the installed Segoe UI font were reviewed at normal and narrow sizes. They establish the tested layout, not native Windows appearance or the user's scaling. Physical playback devices, subjective encoded quality, long hardware stability, audible sync and other existing media coverage limits remain unchanged. Source media is checked against its original size/mtime inventory. No computer use was performed.

## User acceptance

The user stated, "Ok, good, mark 15.2 as complete", explicitly accepting the implemented session and authorizing completion. No further check is required for this session. The prior focused appearance instructions were provided; individual outcomes were not separately reported, so acceptance is recorded separately from agent-run evidence. Other Windows display scales/screens, physical playback devices, subjective quality and long hardware stability retain their documented limits.

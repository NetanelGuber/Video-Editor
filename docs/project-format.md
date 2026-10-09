# Project format and persistence

`.veproject` files use UTF-8, indented JSON with `format: "LocalVideoTools.VideoEditor"` and integer `schemaVersion: 12`. Media remains in its original files. [Project.h](../src/project/Project.h) is the typed model; [the effect fixture](../fixtures/projects/effects-v4.veproject) demonstrates schema-4 animated effects and independent video fades. The project library uses Qt Core and has no Widgets or FFmpeg dependency.

Schema 6 adds optional clip `sequenceId` and optional sequence `multicam` (ordered camera track IDs and ordered frame/track cuts). Absent fields retain plain timeline behavior; schema 1–5 migration adds no timeline features. Nested source ticks are child frames at matching frame rate and 1×, with cycle/depth/source-bound validation. Camera cuts use canonical decimal int64 strings and begin at frame zero. See [Session 14 mapping and limits](session-14.md).

Schema 12 adds sequence `automaticEnd` (boolean). New UI projects and sequences
use automatic mode: every editing transaction recomputes duration from the
maximum clip end across all tracks. Explicit resizing switches to manual mode.
Schema 1–11 projects migrate with `automaticEnd=false` and retain their saved
duration. Automatic duration must equal the last clip end (zero when empty);
mode and duration are saved and restored together by undo/redo. Projects saved
by 1.0.1 require 1.0.1 or newer.

## Records and identity

| Record | Stored fields / meaning |
|---|---|
| Project | UUID, name, active sequence UUID, ordered media/title/sequence collections, export settings |
| Sequence | UUID/name, rational frame rate, integer duration in frames, width/height, audio sample rate, ordered tracks and audio buses |
| Track | UUID/name, video/audio/title kind, enabled/locked/muted/solo, linear gain, pan, audio bus route and audio automation, ordered clips |
| Audio bus | UUID/name, linear gain, pan, mute and solo; buses route directly to the master output |
| Media | UUID/name, path, container, exact file size, stream metadata |
| Stream | Container stream index, video/audio kind, codec, rational timebase/frame rate, signed start timestamp, duration ticks, dimensions/bit depth, audio sample rate/channels, rotation degrees, variable-frame-rate flag |
| Clip | UUID/name/kind, sequence frame start/duration, media or title UUID, selected stream index, source in/duration ticks, gain/pan/mute/fades, audio automation, ordered effects |
| Title | UUID, text, font family/size, left/center/right alignment, `#AARRGGBB` text/background colors, normalized x/y anchor, shadow flag |
| Effect | UUID, type, implementation version, enabled state, JSON parameter object, signed content-time offset in sequence frames, per-parameter ordered keyframes (frame/value/outgoing curve) |
| Export settings | Container/video/audio codec strings, preset, output path, dimensions, rational frame rate and manual-override flag, sample rate/channels, finite quality number, audio enabled |

All record UUIDs are non-null, lowercase, brace-free canonical UUID strings, unique across the entire project. Stream indices identify streams within one media record rather than carrying a separate UUID. References must resolve, and the active sequence must exist. Collection order is preserved exactly. Unknown effect types/versions and their parameter objects survive serialization; enabled unsupported effects warn in preview and block export until bypassed. Session 11 validates and renders transform, crop, opacity, composite and videoFade version 1 through the shared preview/export compositor. See [effect/time rules](session-11.md). Interactive controls use atomic command batches and exact project/selection undo. Saving retains history; new/open clears it.

New projects start with one 1920×1080 sequence at 30/1 fps and 48 kHz, empty Video 1/Audio 1 tracks, and H.264/AAC MP4 export-setting records. Session 8 implements the compatible H.264/AAC MP4 preset; encoder availability and supported settings are checked at export time. Export choices use command-based undo/redo and the existing schema 3 record.

## Exact time and rounding

All authoritative time/count values are signed 64-bit integers in C++. In schema 2, `durationFrames`, `startFrame`, `startTicks`, `durationTicks`, `sourceInTicks`, `sourceDurationTicks`, and `sizeBytes` are canonical decimal **strings**. Rational `numerator` and `denominator` are also decimal strings. This avoids rounding in JSON consumers that represent numbers as IEEE-754 doubles. The fixture includes values above 2^53. Ordinary bounded fields such as dimensions, stream indices, and sample rates remain JSON integers; gain and normalized title positions are floating-point parameters, not authoritative time.

Rationals must be reduced. Both components are positive and at most INT32_MAX, except that an audio stream's frame rate is exactly 0/1. Video stream frame rate records the average/source rate; variable-frame-rate timestamps remain the authoritative source positions. Sequence and export frame rates are positive.

- Sequence time starts at frame zero. Duration is a frame count, and the end is exclusive. A clip occupies `[startFrame, startFrame + durationFrames)`, with a positive duration; it must fit in the sequence. Bounds checks use subtraction to avoid signed overflow.
- A media clip's source in point is a nonnegative tick offset from its selected stream's signed `startTicks`. `sourceDurationTicks` is positive; its exclusive source end must fit within the stream duration. Source ticks use that stream's stored timebase, never sequence frames. Zero-duration stream metadata is representable but cannot back a positive-duration clip.
- Title clips reference a title instead of media. Their source offsets/duration and stream index are zero.
- This persistence layer preserves independent source and sequence ranges. Session 4 defines exact source-boundary edits from these stored intervals in the [timeline model](session-4.md#exact-time-and-source-boundaries). Source-to-sequence resampling and mixed-rate/VFR rendering behavior are defined in later playback sessions, not inferred by the serializer.

For sequence rate `N/D` and audio sample rate `R`:

```text
frame boundary f -> sample boundary: f * D * R / N
sample boundary s -> frame boundary: s * N / (D * R)
```

`framesToSamples` and `samplesToFrames` take an explicit rounding mode: Floor, Ceil, or Nearest (ties upward). Times are nonnegative. Evaluate boundaries from the original absolute frame/sample index each time; never repeatedly add a rounded per-frame duration. Use Floor for a containing/start boundary, Ceil for coverage of an exclusive end, and Nearest for selecting the closest seek/edit boundary. Future audio scheduling must choose the policy for its operation explicitly. At 30000/1001 fps, 48 kHz, one frame is 1601.6 samples: Floor=1601, Ceil=1602, Nearest=1602; 30000 frames are exactly 48048000 samples.

The Windows x64 implementation uses MSVC 128-bit multiply/divide intrinsics for intermediate arithmetic. It returns an empty optional for invalid rates, negative input, unsupported sample rate, or a result outside int64. Supported sample-rate records are 8000–384000 Hz. These are schema/conversion bounds, not measured playback capabilities.

## Validation and migration

Loading is transactional: parse into a temporary model, validate, resolve paths, then replace the UI document only on success. Errors identify the field or JSON byte offset and reach the existing diagnostic log/status/dialog path. Failed loads/saves preserve the current model, filename, and dirty state.

Validation rejects missing/unknown record fields, wrong scalar/array types, unsupported schema versions, invalid/noncanonical numbers, duplicate IDs or stream indices, broken references, kind mismatches, out-of-bounds ranges, invalid colors, and nonfinite/out-of-range numeric settings. Documents are limited to 16 MiB, individual arrays to 100000 entries, text fields to 65536 characters, and effect parameter nesting to 32 levels. Unknown **record** fields fail instead of being silently discarded; arbitrary effect parameter keys are preserved. A future format addition must supply an explicit migration fixture and update its schema version.

Schema 1 is a deliberately checked-in predecessor fixture: only sequence duration and clip start/duration were safe JSON integer numbers rather than strings. Its migration accepts nonnegative integral values through 2^53−1, converts those fields to strings, and then runs full schema-2 validation. Other fields match schema 2. Unsupported older/future versions fail with a supported-version message. This is a format-evolution test, not a claim of compatibility with another editor's files.

The [v1 fixture](../fixtures/projects/multitrack-v1.veproject) includes multiple tracks/media, audio, a title/effect, and a long-duration sequence. Load records `migratedFrom=1`; the UI marks that document dirty and reports migration. It does not rewrite the source automatically. An explicit save writes current schema 4 and retains the original schema-1 bytes in `.bak` when saving over that file.

## Paths and offline media

File loading resolves relative media/export paths against the **document's directory**, independently of the process working directory. Runtime paths passed to ProjectStore must be absolute; raw `serialize`/`deserialize` retain stored path strings without filesystem access. Saving computes relative paths from the destination directory where possible (cross-drive paths remain absolute). Save As therefore preserves the resolved file references while rebasing their stored spelling. No media probing or copying occurs during save.

An unavailable/non-file/unreadable source is reported as offline by media UUID. The media record, metadata, clip references, timing, and source path survive unchanged. Moving the project together with its relative media tree works; moving only the sources leaves them offline. The app never searches for similarly named files or silently relinks. Media import/probing/relink UI comes in Sessions 3 and 9. Existence/readability checks do not verify that current file contents match stored metadata.

Project, backup, autosave, and lock destinations are checked against source paths (including canonical parent directories). The writer refuses symbolic-link destinations and source-file collisions. Source files remain untouched.

## Save, backup, and autosave policy

The writer validates before filesystem writes and acquires a short-lived advisory `<destination>.lock` through QLockFile. Competing save transactions fail promptly with a lock/directory error. Dead-process locks are recoverable. This lock does not merge independently edited documents or detect all external modifications.

All document writes use [Qt QSaveFile](https://doc.qt.io/qt-6/qsavefile.html) with direct-write fallback explicitly **disabled**. A temporary file in the destination directory replaces the target only at commit. Failure to create/write/commit produces an error; the writer never falls back to truncating the primary file.

1. First save writes/commits the new primary; there is no invented backup.
2. On later explicit saves, read and validate the existing primary, then atomically copy its exact bytes to `<project>.bak`.
3. Session 9 retains two previous good versions: before replacing `.bak`, validate it and atomically copy its bytes to `<project>.bak.2` when they differ from the current primary. Only after backup writes succeed, atomically replace the primary. Unchanged bytes do not age out the second backup. Invalid primary/backup or a backup-write failure stops the save, preserving the primary and retained good data; use Save As to a new location. Both backup destinations receive the same source/symbolic-link protection.
4. Autosave atomically replaces one separate latest snapshot, never the primary or backups. The user's Session 9 policy keeps the cadence at 120 seconds while dirty. Both named and untitled projects now use `<app-data>/recovery/<project-UUID>.veproject.autosave` (the diagnostics app-data root). An unresolved/existing snapshot causes a new UUID suffix rather than overwriting old work. A per-snapshot `.session.lock` lease excludes live editors from recovery; dead-process locks are recoverable.
5. Successful explicit save, Discard on New/Open/Close, or undo back to the saved state removes only the current editor's owned, validated, matching-ID snapshot. Failed saves/autosaves and Cancel retain it. Unresolved and invalid snapshots remain until explicitly discarded. Startup and File → Recover project… list up to the 100 newest abandoned snapshots with Recover/Discard/Later actions. Recovery leaves primary/backup bytes untouched and opens an unsaved document for review and Save As.

Opening `.bak`, `.bak.2` or `.autosave` through File → Open loads it as a dirty untitled document, so Save requests a new primary location. Legacy named `.autosave` sidecars are still recoverable this way; startup discovery is confined to app data. New/Open/Close ask Save/Discard/Cancel when dirty; Cancel or a failed save preserves the current document. Cancelled dialogs do not clear dirty state. See [Session 9](session-9.md) for background folder matching, compatibility validation, offline indicators and remaining evidence boundaries.

Process termination during a write leaves the old primary and/or complete backup, with possible orphan temporary files. An interrupted first-ever save may leave no primary, never a half-written primary. Tests kill the actual save process after half of its QSaveFile temporary file is written and flushed, then compare the primary/backup bytes and perform the next save. These tests establish process-interruption behavior; sudden power loss, filesystem corruption, or storage hardware failure are not simulated.


## Session 7 audio settings and schema 3

Schema 3 adds required `fadeInFrames` and `fadeOutFrames` canonical nonnegative int64 strings to clips. Both must be zero for video/title clips; each audio fade must fit within its clip duration. Schema 2 migrates explicitly with zero fades; schema 1 first migrates its frame values to schema 2, then to schema 3. Unknown old fields are retained and rejected rather than discarded. Loading a predecessor marks the document dirty and reports the original version; explicit save now writes schema 4 and the existing backup policy preserves predecessor bytes. The [v2 fixture](../fixtures/projects/multitrack-v2.veproject) remains a migration input; the [v3 fixture](../fixtures/projects/multitrack-v3.veproject) contains nonzero audio fades.

## Session 11 effect animation and schema 4

Schema 4 adds required `timeOffsetFrames` (signed canonical int64 string) and `keyframes` (object keyed by parameter name) to every effect envelope. Each keyframe contains `frame` (nonnegative canonical int64 string), `value` (finite number) and `curve` (`hold`, `linear`, or `eased`). Keys must be strictly increasing within each parameter. Effect vector order, identity, implementation version, bypass state and arbitrary parameter payloads remain lossless. Known version-1 visual effects receive parameter/range/fade validation; unknown versions/types remain stored and can be bypassed.

Schema 3 migrates with zero effect offsets and empty keyframes, retaining every previous parameter. Schema 1/2 migrate through their existing steps, then through 3→4. Invalid predecessor collections and unexpected new fields fail instead of being silently normalized. `migratedFrom` records the original version; opening does not rewrite the source. Explicit save now writes schema 4; the existing atomic backup policy retains the predecessor bytes. The [schema-4 fixture](../fixtures/projects/effects-v4.veproject) exercises animated transform/opacity and independent color/opacity fade settings; v1/v2/v3 remain migration inputs.

Video fade settings reside in `videoFade` effect parameters and are independent of clip audio fades: `inFrames`/`outFrames` are canonical decimal strings, `inMode`/`outMode` choose opacity/color, `inColor`/`outColor` are opaque `#ffRRGGBB` strings and `inCurve`/`outCurve` choose hold/linear/eased. Defaults are zero durations, opacity, black and linear. See [Session 11](session-11.md) for endpoint/overlap/time semantics and bounds.

Track/clip/bus gain is linear amplitude in 0–16. Mute excludes a source. Soloed tracks and buses play together and exclude other sources; track or bus mute takes priority. Buses are single-level routes; an empty track bus ID sends directly to Master. Track and bus faders/pan are applied to each routed source before the master sum.

Audio automation reuses the ordered `Keyframe` record (`frame`, finite `value`, outgoing `curve` of hold/linear/eased). Volume keys multiply the corresponding clip/track fader in linear amplitude (0–16); pan keys offset the corresponding pan (-1 left, 0 center, +1 right). Track keys use absolute sequence frames. Clip keys use original content-relative frames with a signed content offset, so left trims advance the offset and splits give the right half a continuous offset. Before/after the key range, endpoint values are held. Static clip, track and bus pan values combine with automation and clamp to the supported pan range. Pan uses equal-power stereo balance: center preserves both channels, and moving to one side attenuates the opposite channel.

The preview/export mixer converts source audio through FFmpeg `libswresample` to interleaved 48 kHz float stereo. It mixes bounded 1024-sample blocks against absolute sequence sample positions; sequence frame/sample boundaries use Nearest rounding once from the absolute frame index. Clip intervals are half-open. Clip fade-in/out remain clip-local linear-amplitude ramps in sequence frames; overlapping fades multiply, short trims clamp their lengths, and splits retain fade-in only on the left and fade-out only on the right. There is no automatic crossfade at the split. The sum hard-clips to [-1, 1] with no normalization; preview and export consume the same mixer. Source decode failures are visible during preview and fail export instead of silently publishing an incomplete mix.


## Session 12 version-1 effects in schema 4

Color, LUT, speed, mask and chroma-key settings use the existing versioned effect envelope, so no schema migration or new clip field is needed. Numerical parameters and keyframes receive bounded validation; audio accepts speed but rejects supported visual effects, and titles reject speed. One speed effect per clip is allowed. LUT tables are embedded with a provenance path and remain usable when the original LUT file is missing. Unknown effects/versions still round-trip and can be bypassed, while enabled unsupported effects block export. See [Session 12](session-12.md) for defaults, color assumptions, time remapping, audio policy, source bounds and limits.

## Session 13 audio routing and schema 5

Schema 5 adds optional-in-v4 audio mix records: `audioBuses` on a sequence, `audioBusId`/`pan`/`audioAutomation` on tracks, and `pan`/`audioAutomation` on clips. Each automation record has a signed `timeOffsetFrames` and bounded `volume` and `pan` key arrays. Existing version-4 projects migrate with no buses, direct-to-master track routing, center pan, empty automation and zero offsets. Schema 1–3 projects continue through their earlier migrations and then 3→4→5. Earlier project files are not rewritten on open; explicit save emits schema 5.

The 48 kHz preview uses WASAPI shared mode and requests a 100 ms endpoint buffer; the playback clock follows submitted audio minus current device padding. Device/driver scheduling and decoder readiness can add latency, so 100 ms is a requested buffer size, not a measured end-to-end latency guarantee. The live master meter shows dBFS over a rolling 100 ms window; clipping count stays latched until the next seek/pipeline restart. Track and bus peaks are available in the meter tooltip. Export results report pre-clip peak and clipped-sample count. See [Session 13](session-13.md) for UI, automated coverage and remaining limits.

## Session 15 export intent and schema 7

ExportSettings adds required `compatibilityProfile`, `videoEncoder` and `pixelFormat` strings. Profile IDs are `compatible-mp4` (desktop MP4), `desktop-mkv`, `broad-mp4` and `hevc-mp4`. Encoder intent is `software`, `auto` or a supported FFmpeg adapter name; pixel format is `auto`, `yuv420p` or `nv12`. Current unknown strings round-trip so projects remain editable on another computer, while runtime export validation reports a specific conflict and blocks export. Capability/device/probe results are runtime state and never serialized. Choosing Export records requested intent through undo/redo; actual encoder and fallback diagnostics are job results.

Schema 6 migrates to desktop MP4/software/automatic pixel format defaults, preserving its prior codec/container, size/rate/quality/audio/path settings. Conflicting legacy tuples remain visible for repair rather than silently changed. Earlier schemas continue through their prior migrations into schema 7. Opening never rewrites an older file; explicit save emits the current schema. The migration rejects nondefault schema-7 choices mislabeled as schema 6, rather than discarding them. See [Session 15](session-15.md) for supported combinations, fallback and cross-computer behavior.

## Session 15 encoder-specific intent and schema 8

Schema 8 allows fractional quality for encoders with float controls and adds export qualityMode (native/default/bitrate), audioEncoder and encoderOptions (a bounded map of context:name/private:name to native strings). Schema 7 migrates with native quality/AAC/default options, mapping its explicit AMF/NVENC/QSV speed intent to the previously applied native value. Capability availability is rediscovered at export time and never prevents timeline persistence/editing. Unknown encoder and option choices remain recoverable and are explained by export validation.

Custom export settings retain encoder names, native-option keys and values as project intent. Unknown names/options remain serializable so the project stays editable on another computer; runtime availability is checked on the current machine. Limits on map size/key/value length and embedded nulls protect persistence without encoding transient hardware assumptions.

## Schema 9 export defaults and availability

Schema 9 stores whether the export frame rate is a manual override. Automatic export rate uses the lowest valid frame rate across imported video streams, falling back to the sequence rate when there are no imported videos. A changed rate is preserved as a manual choice; schema-8 projects with a saved output path infer manual intent by comparing their saved rate with the active sequence rate. Custom encoder choices are shown only after a short encode/mux probe succeeds for the current size and rate. Container choices require declared codec support, and manual pixel formats are limited to formats the editor can create through its CPU conversion path; Automatic retains the encoder's hardware format path.

## Schema 10 primary video

Schema 10 adds `primaryVideoMediaId` and `primaryVideoStreamIndex` to each sequence. Automatic export FPS follows that stream's inspected rational rate, falling back to sequence FPS when unset or unusable. Schemas 9 and older migrate with Primary Video unset; existing timing and manual export-rate intent are preserved. See Session 15.3 in the plan.

## Schema 11 audio export bitrate

Schema 11 adds required integer `exportSettings.audioBitrate` in bits/s. Schemas 10 and older migrate to 192000, the former hard-coded audio bitrate. Nondefault schema-11 intent mislabeled as an older schema is rejected, rather than discarded. Structural persistence allows bounded 1–1000000 values to retain stale intent; runtime export restricts lossy audio to 32–512 kbit/s and the discrete MP3/AC-3 rates, and exact probes check the complete codec/container combination. Audio-off, FLAC and PCM retain but do not apply the bitrate. The mixer remains 48 kHz stereo. Simple/Advanced selection is presentation state inferred from the shared saved settings; both tabs use one settings record. Explicit manual FPS remains manual even when equal to the automatic rate.

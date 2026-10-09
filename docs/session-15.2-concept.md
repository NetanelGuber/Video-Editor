# Session 15.2 export dialog concept

Completed 2026-10-09 before implementation. This is the implementation specification.

## Annotated tab concepts

```text
Export video
Destination / actual output size, FPS, encoder, format and audio summary [1]
Output file [____________________________] [Browse…]                    [2]
┌ Simple ───────── Advanced ────────────────────────────────────────────┐
│ Playback target [Desktop MP4 / Desktop Matroska / Broad MP4 / HEVC] [3]│
│ Video size      [Sequence size / Fit within 1080p / Fit within 720p]    │
│ Picture quality [Standard / Higher quality / Smaller file]            │
│ [✓] Include audio                                                     │
│ Automatic FPS follows Primary Video, otherwise sequence.              │
│ Target explanation / settings retained from Advanced notice.          │
│ [Use compatible defaults]                                         [4] │
└──────────────────────────────────────────────────────────────────────┘
Specific conflict or check/ready state                                  [5]
[progress] [Export] [Cancel export] [Close]
```

```text
┌ Simple ───────── Advanced ────────────────────────────────────────────┐
│ Playback target / custom encoder and container                        │
│ Video encoder and hardware: Software / Automatic / validated encoder │
│ Container: compatible muxers only                                     │
│ Size and timing: Width / Height, [✓] Automatic FPS, exact N / D        │
│ Quality and speed: native/default/bitrate, encoder scale, preset        │
│ Pixel and color: advertised convertible pixels / Automatic; SDR note  │
│ Audio: Include audio, compatible encoder, bitrate (kbit/s)             │
│ Profile, tune and encoder options [expand to native option table]     │
│ [Use compatible defaults] [Refresh encoder capabilities] [Details…]    │
└──────────────────────────────────────────────────────────────────────┘
```

[1] Always report effective settings, including changes made in the other tab; name the encoder which passed the exact check and any software fallback. [2] Destination is shared; choosing a different container changes the extension. [3] Four curated targets, no codec matrix or raw encoder options in Simple. [4] Explicit reset preserves destination and audio inclusion, restores Desktop MP4/software, native quality 20/medium, automatic pixels/FPS, 192 kbit/s AAC and an even size within 1080p. It clears overrides and starts a new check. [5] Warning and Export gate are outside the scrolling tabs, so both tabs show the same validation.

## Boundary, defaults and dependencies

Simple uses software x264/x265, medium speed, native quality 20/18/24, automatic encoder pixels, 192 kbit/s AAC and 48 kHz stereo. Size fits preserve sequence aspect, allow portrait, never upscale, and round down to even dimensions. Initial settings use the existing project intent. New ordinary projects open Simple; custom, stale or detailed settings open Advanced. Tab switching never resets settings. If a setting cannot be represented by Simple, its small controls are disabled and a notice names Advanced as the active configuration with the explicit reset action available. All exports use the same underlying settings, worker and validation.

Simple target and size choices are disabled when their size/FPS exceeds the fixed target limits; a retained incompatible value is warned immediately and remains blocked. Broad MP4 does not silently cap Primary Video FPS. Choose a desktop target or change FPS in Advanced. Picture quality describes a size/quality tradeoff, not a promise of a particular file size. Target help retains physical-device compatibility limits.

Advanced keeps the existing exact rational frame rate with a new explicit Automatic FPS toggle. Editing a manual rate persists manual intent even when it equals the current automatic rate; switching Automatic on restores Primary Video/sequence rate. Encoder changes explicitly install that encoder's defaults and clear previous overrides. Native controls derive from the bundled AVOptions. Primary speed/quality and conflicting table overrides remain blocked. Fixed target constraints continue to apply; Custom permits wider format/encoder choices. Profile, tune, level and hardware-specific options are in the expandable encoder table, with help, native names, types/ranges and defaults. Missing saved overrides remain visible for removal.

Pixel choices come from advertised formats the existing CPU conversion supports. The color note explains BT.709 SDR output, automatic RGB/full versus YUV/limited range and that selecting 10-bit storage does not produce HDR or restore precision lost in the current 8-bit composition. No unsupported HDR/transfer-function selector is invented. Hardware is chosen through the encoder/policy, with fresh exact hardware probing required; available hardware-specific tuning is in its native table.

Audio encoder choices are limited to compiled AAC/Opus/FLAC/MP3/AC-3/PCM encoders accepted by the container. Lossy bitrate is configurable in Advanced, 32–512 kbit/s; MP3 offers only its documented discrete rates and AC-3 uses its discrete rates. Exact encode/mux probing still determines acceptance. FLAC/PCM disable bitrate and explain lossless/default behavior. Disabled audio retains settings without applying them. The mixer remains 48 kHz stereo. Persist `audioBitrate` in schema 11, migrating schema 10 and older with the former 192000 bits/s default; do not overwrite nondefault new intent mislabeled as old schema. Cache identities and helper requests include audio bitrate.

## Validation and unavailable options

Every edit immediately recomputes structural/compiled validation and disables Export. A changed complete tuple triggers a cancellable background exact check; stale results never enable Export. Known incompatible combo entries are disabled or absent. A retained invalid saved value is shown explicitly, its conflict names the settings and a resolution, and Advanced is revealed. Runtime failures describe the specific encoder, container, dimensions/FPS and computer, keep complete diagnostics in Details and point to another encoder, correcting the conflict, audio-off or compatible defaults. Native unknown/invalid values block export. Reset is recovery from configuration conflicts, not a guarantee of destination permissions, timeline media availability or hardware/device playback.

## Acceptance and evidence

Production-widget tests cover the two tabs, small Simple control set, all Advanced groups, cross-tab preservation/synchronization, incompatible disabled targets, saved stale choices, immediate blocking, explicit reset, exact automatic/manual FPS intent, audio bitrate dependencies and capability invalidation. Save/reopen and undo tests preserve representative native settings and bitrate, including schema migration. Independently inspect/decode exports from both tabs for container, codec, dimensions, fractional FPS, pixel format, profile/tune where observable and audio; compare requested audio bitrate at the codec context and independent MP3 stream metadata. Run the full existing regression. Appearance at the user's Windows scaling remains a focused user review when automated widgets cannot establish it; no computer use is authorized.

## Documentation checked before implementation

- [FFmpeg codec options and encoder-specific controls](https://ffmpeg.org/ffmpeg-codecs.html): bitrate units, x264 preset/profile/tune/CRF and audio encoder constraints.
- [FFmpeg container documentation](https://ffmpeg.org/ffmpeg-formats.html): MP4 codec/container interaction and existing faststart behavior.
- [FFmpeg AVOptions API](https://ffmpeg.org/doxygen/trunk/group__avoptions.html): option iteration, constants, defaults and validation.

Bundled FFmpeg 9.0.2 `-h encoder=libx264`, `aac` and `libopus` help was inspected before implementation. Bundled runtime descriptors and exact probes take precedence over online documentation; consult the official sources again while implementing and verify further audio/encoder values against the local build.

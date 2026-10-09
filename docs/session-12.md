# Session 12 — Color correction, speed and masks

Complete, 2026-10-08, version 0.12.0. The pinned native build and all 12 suites passed in 102.62 seconds, including 613 Session 12 checks. Independent export mean RGB error was at most 1.70 in the reported references; constant-speed pitch measured 440 Hz at 0.125×/0.5×/2×/8× and the ramp measured 439–440 Hz. Gated ramp transient timing error was at most 41.27 ms against a 60 ms filter-window tolerance. All 30 original files match Session 0 size/mtime; deployed runtime hashes match the pinned packages. See [Session 12 evidence](../evidence/session-12/verification.json). The user explicitly authorized marking Session 12 complete; see the [acceptance record](../evidence/session-12/manual-acceptance.json). Individual manual or visual test results were not reported; the documented evidence limits remain. No computer use was performed.

Select a clip and click **Effects and fades**. Choose an effect from the dropdown and click **Add effect**. **OK** applies the stack in one undo entry; **Cancel** discards it. **Reset to defaults**, **Enabled (uncheck to bypass)**, ordering and removal work for the new controls. Audio clips expose only speed; titles support color/LUT/mask/key but cannot change media speed. Existing audio gain/fade and title controls remain in **Clip properties**.

## Color and optional LUT

**color** provides exposure (-5 to +5 stops, default 0), contrast (0–4, default 1), temperature and tint (-1 to +1, default 0), and saturation (0–4, default 1). All numeric controls support hold/linear/eased keyframes with the existing content-frame semantics. Exposure and white-balance gains apply first, contrast around encoded RGB 0.5 follows, then saturation around weighted luma `0.2126R + 0.7152G + 0.0722B`. Output clamps to 0–1; alpha is preserved. Temperature applies `2^temperature` to red and its inverse to blue; tint applies `2^(tint/2)` to red/blue and `2^-tint` to green. Temperature/tint are creative channel gains, not calibrated kelvin or a chromatic-adaptation model.

These controls operate on the decoder's full-range, encoded 8-bit SDR RGB raster, rather than scene-linear light. They assume ordinary SDR display content. Existing decoder matrix/range conversion remains unchanged. There is no ICC/OCIO pipeline, HDR tone mapping, log-camera conversion, calibrated monitor transform or professional color-management accuracy claim. A stop doubles/halves encoded channel values; it does not claim a photometric exposure result. LUT input must match this same assumed SDR domain.

**lut** supports optional normalized 3D `.cube` files with grid size 2–33, red changing fastest, then green, then blue. Use **Choose LUT…** or enter a file path. Only `TITLE`, `LUT_3D_SIZE`, optional normalized `DOMAIN_MIN 0 0 0`/`DOMAIN_MAX 1 1 1`, comments and the exact RGB table are accepted. 1D, combined, log/HDR or out-of-domain tables fail visibly. Import is limited to 4 MiB. Trilinear interpolation and animated **LUT amount** (0–1, default 1) blend the result with original RGB. Empty path/default table is identity. Reset clears the LUT.

The imported table is embedded in version-1 effect parameters (`path`, `size`, `table`, `amount`). The path is provenance only after import; reopening and rendering need no external LUT file. Changing the path imports a new table transactionally. To reload a changed file at the same path, clear/apply the path, then select it again. Save As preserves the embedded table; original LUT/media files are never modified. All new effect envelopes fit schema 4 without adding project fields. Existing v1/v2/v3 migrations and unknown-version preservation remain enabled.

## Speed and ramps

**speed** supplies a forward rate from 0.125× to 8× (default 1×) and the existing keyframe table. One speed effect is allowed per media clip. Add **rate** keys and choose hold/linear/eased outgoing curves to make a speed ramp. Reverse, freeze, optical flow and interpolated video frames are outside this slice.

Applying a changed speed profile recalculates the clip's integer output duration while preserving its selected `sourceInTicks`/`sourceDurationTicks` interval. The new duration is the smallest positive sequence-frame count whose integrated rate covers that source interval's natural duration. It uses the selected stream timebase and sequence rational frame rate. Bypass/removal/reset to 1× restores the selected interval's natural duration, rounded up to a frame. Shortening can leave a gap; lengthening can create an overlap. Other clips and tracks stay in place. Sequence duration grows when necessary and does not automatically shrink. Audio/video are separate timeline clips: apply matching rate/key settings separately to both when they should stay together. This avoids implicit changes to other tracks or the soundtrack.

Let `I(f)` be the analytic integral of the speed curve from the effect's content origin offset to offset + output frame f. Source mapping is `sourceInTicks + round(sourceDurationTicks * I(f) / I(D))`, with exact integer endpoints at f=0 and f=D. Normalizing by `I(D)` absorbs the sub-frame remainder from rounding up duration, spreading that remainder over the selected interval. Constant speed is linear; ramps change the distribution of source time. Hold/linear/smoothstep segments integrate analytically. Source positions and clip/keyframe boundaries remain integers; integration/interpolation uses floating point with deterministic rounding. This is not a claim of arbitrary-precision ramp math above the supported duration. Limits are 10 million output frames and 1.25 million natural source frames for a duration-changing speed edit, a 256-key limit per speed curve and a content-offset bound of ±10 million frames, plus the existing project/effect limits.

The decoder uses the inverse map to timestamp source frames for both preview and export. Seeking maps into source time and retains the preceding source frame when needed. Presentation holds the latest decoded frame whose mapped timestamp has arrived. Real source timestamps, including coarse container timebases and VFR spacing, determine that selection; the editor does not invent evenly spaced source frames. Faster playback drops source frames and slower playback repeats them.

Trims evaluate both source boundaries before shifting the speed/keyframe origin. Splits evaluate the remapped boundary, divide the selected source interval, give copied effects new IDs and advance the right half's content offset. The normalized subranges preserve the original ramp within source-tick rounding. Existing outer-edge fade behavior remains intact. Move, undo/redo and save/reopen preserve the profile.

## Pitch-preserving audio policy

The user selected **preserve pitch**. A constant speed effect on an audio clip routes its resampled 48 kHz stereo PCM through three streaming FFmpeg `atempo` stages. Ramped profiles use the pinned build's `rubberband` filter with pitch=1, a short window, crisp transients, smoothing and quality pitch processing. Each stage stays between 0.5× and 2×; their product covers the supported range without the greater-than-2 sample-skipping mode. FFmpeg documents [atempo](https://ffmpeg.org/ffmpeg-filters.html#atempo) and [Rubber Band](https://ffmpeg.org/ffmpeg-filters.html#rubberband), including runtime tempo commands. The effective tempo follows the same normalized source mapping; ramp commands update every at-most-512 input samples (10.7 ms at 48 kHz). The existing mixed output gain/fades/mute/solo apply in output sequence time.

Constant speeds use WSOLA; ramps use Rubber Band time stretching. Both preserve musical pitch and can change waveform texture. It can introduce transient/texture artifacts, especially at extreme rates and abrupt ramp keys. Video remapping is frame/timestamp based; audio ramps use short tempo blocks and a filter window, so transient alignment is approximate. Exact output start/end sample counts are enforced; the mix pads a short filter tail with silence when needed. A source error remains visible.

For sample-identical seeks, the retimed audio pipeline decodes and stretches from the selected clip's source in point, discarding PCM before the requested output position. This keeps memory bounded and cancellation available, but a late seek into a long retimed audio clip can buffer longer than the Session 10 sub-second video-seek profile. A persistent retimed-audio cache and long-clip seek optimization are not implemented. Do not claim the prior performance target for that new workload.

## Bounded mask and chroma-key workflow

**mask** supplies a rectangular keep region in current-canvas fractions: left/top default 0, right/bottom default 1, inward feather default 0 (max 0.5), and **Invert rectangular mask** default off. Numeric edges/feather animate. Pixel-center distance to the nearest edge sets a linear inward coverage ramp. Crossed/empty edges produce an empty keep region; inversion reverses coverage. Masks operate at their stack position after the source fits the canvas, so ordering relative to transform/crop changes the result. Reset restores full coverage.

**chromaKey** defaults to opaque green `#ff00ff00`, normalized RGB-distance tolerance 0.1, soft-edge width 0.1 and spill suppression 0. Tolerance/softness/spill animate; key color is fixed per effect. **Choose key color…** opens the native picker. Distance is Euclidean encoded RGB distance divided by sqrt(3). Pixels within tolerance become transparent; the next softness interval ramps linearly to opaque. Zero softness is a hard threshold. Spill suppression reduces excess of the key's dominant channel in partially keyed pixels. It is a first SDR workflow, without tracked/freeform masks, edge reconstruction or production keying guarantees.

Both workflows modify straight-color coverage then return premultiplied alpha to the existing shared compositor. Lower video/title layers show through, or black when none is below. Stack order and final blend mode remain explicit. Preview and export use identical raster operations; spatial feathering can differ slightly with preview resolution. Existing limits of eight simultaneous video layers and 64 effects per clip still apply. Color/LUT/key/mask processing is CPU raster work; sustained multilayer 4K effects playback has not been benchmarked.

## Verification and remaining limits

Run:

```powershell
./scripts/Build.ps1
./scripts/Test-Application.ps1
./scripts/Collect-Session12Evidence.ps1
```

`AdvancedEffectsTests` checks analytical color pixels/defaults/clipping/alpha, normalized LUT import and embedded reopen, mask inversion/feather/crossed edges, key coverage/spill, speed integrals and source boundaries, trim/split continuity, transactional failure/undo/redo, native offscreen controls, production preview, independently decoded moving-source and effect exports, spectral pitch at minimum/maximum/intermediate rates and ramp progression, independently timed audio transients with a 60 ms ramp filter-window tolerance, exact audio sample counts/seek equivalence and independent AAC output. Earlier suites retain recovery, migration, original-media export and CPU/D3D11VA checks. Offscreen PNGs and reports are saved in `build/session-12/advanced-effects-test-data` and copied into `evidence/session-12`.

Disposable references are prepared automatically in `fixtures/generated/session-12`: `color-mask-key-lut.veproject`, `speed-ramp.veproject` and `audio-ramp.veproject`, with their local source files. They provide repeatable edge-case footage rather than changing a user project.

Automated native controls/pixels/audio evidence does not establish Windows display scaling, subjective audio quality, perceived AV sync, calibrated color, real HDR/log footage, long retimed-audio seeking or sustained effects-heavy 4K performance. No blanket manual checklist is required. A minimal visible check, if the user wants to close the perceptual gap, is to open `fixtures/generated/session-12/audio-ramp.veproject` in `build/session-12/Release/VideoEditor.exe` and play it: the three beeps should come closer together while keeping the same musical pitch. Windows interaction and subjective perception remain user-tested evidence only.




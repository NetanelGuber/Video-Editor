# Session 11 — Effect stack, keyframes and video fades

Complete, 2026-10-07, version 0.11.0, project schema 4. The user explicitly accepted Session 11 and authorized completion; see the [user acceptance record](../evidence/session-11/manual-acceptance.json). Automated verification covers native model/undo/persistence, production Qt offscreen controls and playback, analytical raster pixels and independently decoded MP4 output. Individual manual or visual test results were not reported. Human interaction, display scaling and perceived multilayer playback smoothness are not claimed. No computer use was performed.

## Editing effects

Select one video or title clip and click **Effects and fades** in the timeline. Double-clicking a video clip opens the same editor. Title text/style remains in **Clip properties**; titles also support the effects editor. Unlock the track before editing. Audio retains its separate gain/fade controls.

Use **Add effect**, **Remove**, **Move up**, **Move down**, **Enabled (uncheck to bypass)** and **Reset to defaults**. Each effect has a stable UUID and implementation version. Effects run from top to bottom within a clip. **OK** commits all changes as one undo entry, refreshes the sequence viewer and marks the project dirty; **Cancel** leaves the project and history unchanged. Numeric values retain their full stored precision when left untouched. Unknown effects/versions retain their data and can be bypassed or removed; preview reports a warning and export rejects enabled unsupported effects.

| Effect | Controls and defaults |
|---|---|
| transform | x/y offsets in sequence-canvas fractions (0), horizontal/vertical scale (1), clockwise rotation in degrees (0); transform about the canvas center |
| crop | left/right/top/bottom fractions (0) of the current canvas; excluded pixels become transparent; opposing crops totaling 1 or more leave an empty layer |
| opacity | alpha multiplier (1), revealing lower layers or black |
| composite | sourceOver (default), multiply or screen; the last enabled composite effect selects the clip's final blend mode |
| Video fades | independent in/out duration (0 frames), kind (opacity), solid color (black) and curve (linear) |

Video initially fits the sequence aspect ratio with transparent surrounding bars. Crop and transform operate on this fitted canvas in stack order; they do not stretch a cropped image back to fill the canvas. Titles form transparent layers on the same canvas. Enabled tracks render in stored order, with later tracks above earlier ones; later clips on a track are also above earlier clips. The final sequence background and output-aspect bars are black. Blend modes affect the entire resulting clip layer, including its fade colors. Effects later in a stack can intentionally alter earlier fade endpoints; use sourceOver and put **Video fades** last for literal chosen-color endpoints.

## Keyframe time and interpolation

Transform, crop and opacity numeric parameters support keyframes. **Add at playhead** creates a key for the selected parameter. Edit **Content frame**, **Value** and **Outgoing curve** in the table, or use **Delete selected key**. Keys are sorted when applied; duplicates, invalid frames and values outside the parameter's range are rejected visibly.

Time is an exact signed int64 count of sequence frames. The keyframe's nonnegative **Content frame** is measured from the original clip content origin, at the sequence frame rate. For a visible frame, animation time is `sequenceFrame - clip.startFrame + effect.timeOffsetFrames`. The offset is serialized as a signed decimal string; keyframe frames are nonnegative decimal strings. Moving the clip keeps this mapping. Trimming its in edge advances the offset; extending it backward can make the offset negative. Splitting gives the right half new effect IDs and advances its offset by the left duration, preserving the existing animation. Keys outside a trimmed clip remain stored for later extensions. This implements the user's selected content-preserving behavior.

Values hold before the first and after the last key. With no keys, the parameter uses its constant/default value. Static effects do not accumulate trim offsets; when their first keys are added, their origin starts at the current clip in edge. An already animated effect retains its original content origin across trims/splits. A key's curve governs the segment to its next key:

- **hold** keeps the left value until the next key's exact frame, then jumps.
- **linear** uses the integer time differences to compute the segment fraction.
- **eased** uses smoothstep, `t²(3 − 2t)`, with zero endpoint slope.

Sequence frame boundaries rounded to microseconds map back to the exact authoritative frame, including 30000/1001 and 24000/1001 rates. Between boundaries the renderer holds the current sequence-frame evaluation. Export can sample at a different output rate using the same mapping. Integer positions above 2^53 remain exact; parameter interpolation uses double precision and deterministic raster evaluation, not professional color-management math.

## Video fade semantics

Each edge independently chooses a frame duration, curve, and **opacity** or **color** kind. **Choose…** opens the solid-color picker; black, white and custom `#ffRRGGBB` values are supported. Colors are opaque; video fade durations are separate from audio fade settings.

A duration of 0 disables the edge. A duration of N covers the first/last N displayed sequence frames. For N ≥ 2, the in edge starts at the selected-color/zero-opacity endpoint on local frame 0 and reaches the unfaded clip on frame N−1; the out edge starts unfaded on frame D−N and reaches its endpoint on frame D−1. A one-frame fade affects only the first or last frame. Both endpoints are included; the clip's half-open end has no displayed frame. **hold** stays at the start value until the window's last frame; linear and eased interpolate across the window.

Opacity fades multiply the layer's alpha and reveal underlying video/title layers, or black when none is beneath it. A color fade blends the selected solid color across the whole clip canvas, including transparent fitted/cropped areas. Both edges apply in then out. Overlapping opacity fades multiply. Overlapping color fades blend sequentially, with out as the final color operation; independent outer endpoint colors remain exact with sourceOver. Multiple fade effects follow normal stack ordering.

Each duration must fit within the clip. Trims clamp each duration to the new clip length and attach it to the new outer edge. Moves leave durations, colors and curves intact. Splits retain only the original fade-in on the left and fade-out on the right, clamp each to its half's duration, and disable the newly created inner fades. The stored colors/curves for disabled edges remain available for future edits. Undo/redo restores all values, keys, offsets, order and IDs exactly; schema-4 save/reopen preserves them.

## Rendering and bounds

Preview and export call the same `renderLayers`/`applyEffects` compositor. All active source clips needed by lower layers are decoded, rather than only the last video clip. Effects animate during title-only playback and between newly decoded source frames. CPU composition is SDR raster using Qt's premultiplied alpha and blend modes; optional D3D11VA still accelerates source decoding, with CPU fallback. Output is the existing H.264/AAC MP4 path using originals.

The graph supports at most eight simultaneous video layers and 64 effects per clip. More layers produce a visible preview diagnostic and export validation error. Keyframes are limited to 10,000 per parameter and 64 animated parameter names per effect; project size remains at most 16 MiB. Converted preview queues share the existing 32 MiB standard / 4 MiB low-memory budget, with a 1 MiB floor per active layer (at most 8 MiB in low-memory mode for eight layers). Decode dimensions reduce automatically when needed to fit a frame inside its share; the selected final preview canvas remains unchanged. Each queue also retains its six-frame bound. Export has at most eight queues, each at most 64 MiB/six frames. Held/lookahead, composition/intermediate images, codec references, GPU surfaces, audio and encoders are additional allocations. These are converted-queue bounds, not a process-RSS limit or an eight-layer 4K real-time promise.

Seek/edit cancellation remains asynchronous and coalesces to the latest target. At video membership changes the current video pipelines restart while audio keeps its master clock. The existing brief loading at cuts is a known limit. Multi-layer sustained 4K performance, real phone/HDR sources and slow storage remain unmeasured.

## Reproduce and evidence

```powershell
./scripts/Build.ps1
./scripts/Test-Application.ps1
./scripts/Collect-Session11Evidence.ps1
```

The [effect report](../evidence/session-11/effects-result.json), [complete test log](../evidence/session-11/test.log) and [verification inventory](../evidence/session-11/verification.json) record the final result. The suite checks model/keyframe precision, migrations 1/2/3→4, locked/invalid/overflow transactions, reset/bypass/reordering, exact undo/redo/reopen, independent black/white/custom colors, lower video, overlaps, one-frame/hold/eased fades, and trim/split/move boundaries. It drives native production dialogs and preview seeks, verifies animated titles without video, exercises rapid scrub/eight-layer low-memory/cancellation, and independently decodes all reference export frames plus fractional-rate, title and edited-boundary exports. Raster color/alpha endpoints are exact; encoded comparisons allow mean RGB error below 3/255, with per-channel endpoint tolerance below 5/255 for H.264 conversion/compression. The previous ten regression suites include target-PC CPU/D3D11VA synthetic 4K preview and original-media export.

The ready-to-open project is `B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\session-11\effects-reference.veproject`. Its twelve one-second red-over-blue sections show opacity fades, black/white/custom-color fades, overlaps, eased transform/crop, multiply/screen, keyframed opacity, black with no lower video, and hold/eased color fades. The test exports are under `build/session-11/effects-test-data/`. The checked-in [schema-4 example](../fixtures/projects/effects-v4.veproject) has static IDs and synthetic offline paths; the tests regenerate only the separate generated project/media.

No blanket human checklist is requested. Automated checks establish model, controls, raster appearance and preview/export agreement. Visible Windows interaction/display scaling and perceived smoothness remain explicit evidence limits.

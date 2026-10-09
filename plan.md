# Video Editor Development Plan

**Status:** Working plan, 2026-10-09
**Workspace:** `B:\Coding\02-Projects\Tools\Video-Editor`

## Product direction

Build a Windows desktop video editor for personal use. The first usable release should cover the complete basic workflow: import media, arrange and trim clips on a multitrack timeline, add titles and audio, save a project, view the sequence in a live viewer while editing, and export a finished video. The live viewer is a must-have: it stays available during timeline work and updates during playback, seeking/scrubbing, and edits to clips, titles, and effects. After that end-to-end base is reliable, add advanced editing capabilities in deliberate slices.

The confirmed target machine runs Windows 11 Pro 26H2 (build 26300.9550), with an Intel i7-13700F, AMD RX 9070, and 32 GB RAM. Session 0 inspected all 30 videos in `B:\Videos`: 1080p, 8-bit H.264/HEVC in MP4/MKV, mostly 144 fps, with AAC/FLAC/ALAC audio and some multiple-audio-stream files. The user approved starting with this footage plus synthetic coverage for 4K, 10-bit SDR, variable frame rate, and rotation. Real 4K/phone/HDR behavior remains a coverage gap. Treat hardware decode and encode as capabilities to measure on the target PC, not as guaranteed behavior.

### Proposed technical direction

- **Language and build:** C++20, MSVC, and CMake.
- **Desktop UI:** Qt 6 Widgets. Keep the UI native and local; do not use Electron, a browser shell, WebView, HTML, or other web UI technology.
- **Media:** FFmpeg libraries for media probing, decode, filters, resampling, and export. Keep the media layer behind app-owned interfaces so the UI and project model do not depend directly on FFmpeg data types.
- **Project format:** Versioned, human-readable JSON project document, written atomically; media stays in its source files and is referenced by stable project records and paths.
- **Render architecture:** A shared timeline/render description feeds both preview and export. Benchmark the frame path early, then choose and document the GPU presentation path. Keep a CPU fallback and leave proxy generation as a planned performance feature.
- **Audio output:** Select Qt Multimedia or Windows WASAPI in the playback spike after measuring latency, synchronization, and implementation cost.
- **Dependency policy:** Session 0 pins Qt 6.11.3 (MSVC 2022 x64 base package), FFmpeg 9.0.2 (Gyan full shared, GPLv3-or-later), MSVC 19.51.36244/toolset 14.51.36231, CMake 4.2.3-msvc3, and Windows SDK 10.0.26100.0 in `dependencies.lock.json`. Record enabled FFmpeg components and licenses. If the app is ever shared, review redistribution and codec licensing before packaging.

Qt documents supported Windows desktop configurations and its C++ Widgets module; FFmpeg documents the media libraries and available hardware acceleration paths. The plan uses those as candidate building blocks and requires target-PC validation before relying on acceleration.

## Completion targets

### First usable release

A user can create and reopen a project; import and organize the target footage; place video, audio, and title clips on multiple tracks; keep a live sequence viewer visible while editing; see it update during playback, seeking/scrubbing, and timeline changes; trim, split, move, and delete clips; undo and redo edits; adjust basic clip audio; save safely; and export an MP4 that plays correctly outside the app.

### Advanced roadmap

After the first usable release, add a keyframed effect stack, transform/crop/opacity controls, color correction, speed changes and ramps, masks/chroma key, more capable audio mixing, proxy media, and finally multicamera and nested sequences. This ordering prioritizes capabilities that improve a single-editor workflow while postponing the largest timeline-model expansions.

## Session-sized steps

Each item is intended to be a focused working session with a visible result. If a session grows, split it at its deliverable boundary and carry the remaining acceptance criteria forward; do not merge unfinished features just to preserve the numbering.

### Session 0 — Confirm footage and target environment

**Status:** Complete for the user-approved initial scope, 2026-10-06. See [Session 0 report](docs/session-0.md), [dependency pins](dependencies.lock.json), and `evidence/session-0/`. Metadata probes passed for all 30 real files; six representative originals and four synthetic fixtures have timestamp and CPU decode evidence. A C++20 program linked and ran with the pinned Qt Widgets and FFmpeg libraries. GPU playback/encode, actual viewer behavior, and real 4K/phone/HDR footage are explicitly unverified and carried forward to Sessions 3/5/10.

**Deliverable:** A small, documented media test set and a pinned initial environment.

- Record the Windows version, display scaling, available disk space, and driver versions on the target PC.
- Gather a few representative clips and note container, codec, resolution, frame rate (including whether variable), bit depth, audio codec, rotation metadata, and file size.
- Include at least one expected 4K clip and one clip with audio. Add a variable-frame-rate or phone clip if that matches the user's footage.
- Select the initial Qt, MSVC, CMake, and FFmpeg builds based on the OS and media inventory. Record FFmpeg enabled components and licensing notes.

**Acceptance:** The app's intended OS is written down; each sample's stream metadata can be read; codec and hardware-acceleration assumptions are listed as verified or still unknown.

**Depends on:** None.

### Session 1 — Buildable native application shell

**Status:** Complete, 2026-10-06. Clean configure/build and automated offscreen startup/error/layout-persistence checks passed. The user tested the full manual Windows checklist and confirmed that all checks passed, including rendering, scaling, and interaction. See [Session 1 report](docs/session-1.md), [build and manual-check instructions](docs/build.md), and `evidence/session-1/`.

**Deliverable:** A cleanly buildable Windows executable with a basic editing workspace.

- Set up CMake, C++20, MSVC, and Qt Widgets with pinned dependency versions.
- Create the main window, menu/toolbar, dockable media bin, preview area, timeline area, and status/progress area as placeholders.
- Add logging, app version, settings location, and a consistent error-reporting path.
- Check in build instructions and a dependency/license inventory.

**Acceptance:** A clean configure/build starts the app; the UI uses no browser or web UI component; startup and an intentional error are logged usefully.

**Depends on:** Session 0.

### Session 2 — Project model and safe persistence

**Status:** Complete, 2026-10-06. Pinned clean build, native model/filesystem/migration/process-termination checks, and offscreen application project-lifecycle checks passed. The user tested the full manual Windows checklist and confirmed that everything passed. See [Session 2 report and manual checklist](docs/session-2.md), [manual acceptance record](evidence/session-2/manual-acceptance.json), [project format and persistence policy](docs/project-format.md), and `evidence/session-2/`. Automatic startup recovery and interactive relinking remain in Session 9.

**Deliverable:** Versioned project files that can be created, saved, reopened, and migrated deliberately.

- Define project, sequence, track, media, clip, title, effect, and export-setting records.
- Store sequence frame rate and duration with rational/integer time values rather than floating-point seconds. Define audio sample-time conversion and rounding rules.
- Store a schema version, stable IDs, source metadata, and media paths. Specify what happens when media is missing or moved.
- Implement atomic save, load validation, backup/autosave policy, and a migration test fixture before the format accumulates users' work.

**Acceptance:** A project with multiple tracks and media references round-trips without losing values; malformed or older files fail with a useful message or migrate through a defined path; an interrupted save cannot leave a half-written project as the only copy.

**Depends on:** Session 1.

### Session 3 — Media import and inspection

**Status:** Complete, 2026-10-06. Automated verification passed, and the user accepted Session 3 and authorized marking it complete. Background file/folder import, canonical-path duplicate detection, full stream inspection, oriented thumbnails, basic opening-30-second audio waveforms, cancellation, and selected-file relinking are implemented. All 30 original files import; the six real/four synthetic Session 0 samples match independent probes. See [Session 3 report and simple manual checklist](docs/session-3.md), [user acceptance record](evidence/session-3/manual-acceptance.json), and `evidence/session-3/`. Playback/GPU validation remains Session 5; broader recovery/bulk relinking remains Session 9.

**Deliverable:** A media bin populated by real files and accurate stream metadata.

- Add file/folder import, duplicate detection, asynchronous probing, thumbnails, and basic audio waveform generation.
- Show duration, frame size/rate, video/audio streams, orientation, and offline/unsupported state.
- Verify the sample set from Session 0. Preserve source paths; do not copy large source files into the project by default.

**Acceptance:** Importing the sample set does not freeze the UI; displayed metadata agrees with an independent probe; a missing file can be identified and relinked.

**Depends on:** Sessions 1–2.

### Session 4 — Timeline editing model and undo/redo

**Status:** Complete for the model-only scope, 2026-10-06. The pinned clean build and four automated suites pass, including deterministic timeline commands, exact undo/redo and selection, mixed-rate/audio/title fixtures, boundary/overflow/lock/overlap checks, and repeated atomic save/reopen without drift. Ripple affects only the edited track, as selected by the user. See [Session 4 rules and verification](docs/session-4.md) and `evidence/session-4/`. No human visual acceptance is claimed or required for this library; playback and interactive controls remain Sessions 5–6.

**Deliverable:** A deterministic editing model independent of the timeline drawing code.

- Implement video/audio tracks, source in/out points, timeline placement, gaps, clip splitting, trimming, move/ripple rules, track enable/lock, and selection.
- Implement command-based undo/redo for each editing operation. Define edge behavior at sequence boundaries and frame boundaries.
- Add a small deterministic synthetic project fixture covering overlaps, gaps, mixed source frame rates, audio, and titles.

**Acceptance:** Editing commands preserve duration and media references; undo/redo returns the model exactly to its previous state; frame-based edits do not drift after repeated save/reopen cycles.

**Depends on:** Session 2.

### Session 5 — Decode, playback, and rendering feasibility spike

**Status:** Complete, 2026-10-06. Automated validation passed, and the user accepted Session 5 and authorized marking it complete. Source/sequence monitors, bounded cancellable FFmpeg decoding, WASAPI audio clock/output, CPU fallback, and CPU/OpenGL presentation are available in version 0.5.0. All five automated suites pass; actual RX 9070 D3D11VA decode, hardware OpenGL framebuffer comparison, and AMD H.264/HEVC/AV1/HEVC10 encoder checks passed. See [Session 5 report and manual checklist](docs/session-5.md), [user acceptance record](evidence/session-5/manual-acceptance.json), [rendering decision and benchmark](docs/rendering-decision.md), and `evidence/session-5/`. Individual manual checklist outcomes were not separately reported; documented prototype limitations remain.

**Deliverable:** A source monitor and sequence preview that can play and seek the sample footage, plus a written rendering decision.

- Build asynchronous demux/decode queues with cancellation and explicit buffer limits; keep decoding and UI work off the same blocking path.
- Present decoded frames in the Qt preview and synchronize audio playback against the sequence clock.
- Compare CPU conversion/presentation with an accelerated path available on the target PC. Test supported decode/encode options, seeking, scrubbing, pause, and fallback behavior with the actual AMD GPU and drivers.
- Record a short benchmark: startup/seek latency, playback stability, CPU/GPU use, dropped frames, and memory use for representative clips. Select Qt Multimedia or WASAPI for audio output.
- Lock the render description and preview/export boundary. Prefer one timeline/effect description that can be evaluated by both paths; document any known preview/export differences.

**Acceptance:** The sample footage can be opened, paused, sought, and played without UI stalls or unbounded memory growth; unsupported acceleration falls back cleanly; the performance decision and known limitations are recorded.

**Depends on:** Sessions 0–3. The playback prototype can use temporary sequence data before the timeline UI is finished.

### Session 6 — Timeline interface and basic edit workflow

**Status:** Complete, 2026-10-06. The pinned build and all six automated suites pass, and the user accepted Session 6 and authorized marking it complete after the 0.6.1 drag-entry fix. Version 0.6.1 adds the multitrack timeline, mouse/keyboard placement/trim/move/split/delete, snapping, track controls, per-track ripple, atomic mixed edits and undo/redo, persistence integration, and live viewer/playhead synchronization. Basic title placement/text preview is included; styling and audio mixing remain later sessions. See [Session 6 report and manual checklist](docs/session-6.md), [user acceptance record](evidence/session-6/manual-acceptance.json), and `evidence/session-6/`. Individual manual checklist outcomes were not separately reported; documented preview limitations remain.

**Deliverable:** A usable multi-track timeline for the first cut.

- Draw tracks and clips from the timeline model; add zoom, horizontal scroll, playhead, selection, snapping, and track controls. Keep the live sequence viewer visible in the editing workspace while the timeline is active.
- Support drag placement, trim handles, split-at-playhead, move, delete, and basic ripple behavior.
- Wire keyboard shortcuts and route every edit through the command/undo system.

**Acceptance:** A sample sequence can be assembled and edited with mouse and keyboard while the live viewer remains visible; the viewer updates after placement, trim, split, title, and seek operations; selection and playhead stay coherent after edits; undo/redo remains correct after mixed operations.

**Depends on:** Sessions 4–5.

### Session 7 — Audio and title basics

**Status:** Complete, 2026-10-07, version 0.7.0. The pinned build and all seven automated suites pass, and the user confirmed that Session 7 works. Preview now mixes simultaneous audio clips with clip/track gain, mute/solo and frame-based fades; title properties cover text/font/size/alignment/colors/position/shadow/duration. Changes undo atomically and persist in schema 3 with explicit v1/v2 migration. See [Session 7 report and manual checklist](docs/session-7.md), [user acceptance record](evidence/session-7/manual-acceptance.json), and `evidence/session-7/`. Sample-level timing and production offscreen controls are verified. Individual manual checklist outcomes were not separately reported; documented preview limits remain. Export remains Session 8.

**Deliverable:** A sequence with soundtrack, clip audio controls, and editable title overlays.

- Add audio clips, mute/solo, clip gain, track gain, and simple fades. Render audio in the same sequence timebase as video.
- Add title clips with text, font, size, alignment, color, position, duration, and background/shadow options.
- Make title changes undoable and persist them in the project model.

**Acceptance:** Audio remains in sync during preview; title placement and timing are visible in the preview; saved projects reopen with the same title and audio settings.

**Depends on:** Sessions 2, 4, 5, and 6.

### Session 8 — First complete export

**Status:** Complete, 2026-10-07, version 0.8.0. The pinned build and all eight automated suites pass, including 160 export checks, and the user tested the implementation, confirmed it works and authorized marking Session 8 complete. Verification covers shared preview/export composition, full-resolution titles, mixed/faded audio, H.264/AAC MP4, exact frame/sample scheduling, output metadata and independently decoded content, fractional rates, all ten representative real/synthetic sources, atomic cancellation/retry, errors and production offscreen controls. See [Session 8 report](docs/session-8.md), [user acceptance record](evidence/session-8/manual-acceptance.json) and `evidence/session-8/`. Individual manual checklist outcomes were not separately reported; documented rendering and performance limits remain.

**Deliverable:** A rendered MP4 from an edited project.

- Compile timeline clips, audio, and titles into the same render description used by preview.
- Add an export dialog for output path, sequence size/frame rate, audio, quality/preset, progress, cancellation, and errors.
- Start with a broadly compatible H.264/AAC MP4 preset; keep presets data-driven so the actual preferred formats can be added after footage discovery.
- Verify output duration, stream metadata, audio/video synchronization, first/last frame, and playback in a separate player.

**Acceptance:** The first cut can be exported, cancelled safely, and exported again; output plays correctly and its content/timing matches the sequence within documented tolerances.

**Depends on:** Sessions 2, 4–7.

### Session 9 — Project recovery and media relinking

**Status:** Complete, 2026-10-07, version 0.9.0. The user explicitly accepted Session 9 and authorized marking it complete. Implements the user's two-minute autosave/two-backup policy, startup recovery with exclusive live-session leases, safe unsaved New/Open/Close handling, recursive background folder relinking, and offline bin/timeline states. All nine suites pass, including 76 recovery/relink checks covering killed/restarted production MainWindow processes, latest multitrack snapshot recovery, byte-exact preservation of last-good saves, failure/cancellation paths, migrations, and exact edit/undo/reopen behavior. See [Session 9 behavior and verification](docs/session-9.md), [user acceptance record](evidence/session-9/manual-acceptance.json) and `evidence/session-9/`. The completion request does not establish individual manual test results; no computer use or human visual acceptance is claimed. Windows appearance, sudden power loss and large-project/slow-storage performance remain explicit evidence limits.

**Deliverable:** A resilient save/reopen path for real editing sessions.

- Add autosave cadence, backup retention, recovery prompt, and safe handling of unsaved changes.
- Add a relink workflow for moved or unavailable source files and clear offline states in the media bin/timeline.
- Add schema migration examples and a recovery project fixture.

**Acceptance:** Forced app termination followed by restart offers the latest recoverable project; source files moved to another folder can be relinked without changing clip edits; failed recovery leaves the last good save intact.

**Depends on:** Sessions 2–8.

### Session 10 — Proxy and 4K performance pass

**Status:** Complete for the agreed editing profile, 2026-10-07, version 0.10.0. The user confirmed the focused visible playback check and authorized marking Session 10 complete. The agreed targets are 30 fps preview at 1080p or below, sub-second seeking and bounded memory. Adds 360p/720p/1080p preview controls, a 30 fps presentation cap, standard/low-memory byte budgets, persistent bounded thumbnail/waveform caching and failure/cancellation recovery. All ten suites pass, including original-media 4K export with independent content/timing checks. CPU and actual RX 9070 D3D11VA meet the edited synthetic 4K profile; the plan's measured-need gate does not warrant optional proxies, so proxy generation/switching and failed-proxy tests are deferred explicitly. See [Session 10 report](docs/session-10.md), [user acceptance record](evidence/session-10/manual-acceptance.json) and `evidence/session-10/`. Real 4K/phone/HDR and larger graphs/slow storage remain coverage gaps; no agent computer use was performed.

**Deliverable:** A documented, responsive editing profile for the target PC.

- Measure the first complete release against the Session 0 sample set and Session 5 baseline.
- Add optional proxy generation and original/proxy switching if the benchmark shows a meaningful need. Keep source relinking and final export based on originals by default.
- Add thumbnail/waveform caching, bounded memory budgets, background task cancellation, and preview-quality controls.
- Re-test the RX 9070 acceleration path against CPU fallback; do not require acceleration for project correctness.

**Acceptance:** A representative 4K sequence can be assembled, previewed, and exported within agreed performance expectations; proxy and original media produce the same edit timing; low-memory and failed-proxy paths recover cleanly.

**Depends on:** Sessions 5 and 8–9.

### Session 11 — Effect stack and keyframes

**Status:** Complete, 2026-10-07, version 0.11.0. The user explicitly accepted Session 11 and authorized marking it complete. Adds ordered reusable visual effects, hold/linear/eased keyframes, transform/crop/opacity/blend controls, independent opacity/selected-color video fades, transactional native editing and schema-4 persistence with v1/v2/v3 migration. Preview/export now compose all active video/title layers through the shared renderer. All eleven suites pass, including 979 Session 11 checks and independently decoded reference, fractional-rate, title and trim/split/move exports. The user's selected content-relative keyframes and outer-edge-only split fades are implemented. See [Session 11 rules and evidence](docs/session-11.md), [user acceptance record](evidence/session-11/manual-acceptance.json) and `evidence/session-11/`. Individual manual or visual test results were not reported; no computer use or human visual testing is claimed. Windows interaction/display scaling and sustained multilayer 4K smoothness remain unverified. No blanket manual checklist is requested.

**Deliverable:** Reusable, editable effects with animated parameters, including video fades and fades from/to a selected color.

- Define effect identity, ordering, enabled/bypass state, defaults, serialization, and versioning.
- Add keyframe interpolation (hold, linear, and eased curves) with explicit time semantics.
- Implement transform, crop, opacity, and simple compositing controls first.
- Add explicit video fade-in and fade-out controls with independently adjustable durations in sequence frames. Opacity fades reveal the underlying video tracks, or black when no video is beneath the clip.
- Add fades in from and out to a user-selected solid color, with independent colors and durations for the in/out edges (including black, white, or a custom color). Define fade curves, overlapping fade behavior, and clip-edge behavior after trim/split/move edits; route changes through undo/redo and save them in the project.

**Acceptance:** Keyframed values evaluate deterministically at frame boundaries, persist after reopen, undo correctly, and match between preview and export on a reference sequence. Video opacity fades and fades from/to selected colors have the specified timing and endpoint appearance in preview and export; their independent colors, durations and curves survive save/reopen and undo/redo. Reference tests cover black, white, a custom color, underlying video, overlapping fades, and trim/split/move boundaries.

**Depends on:** Sessions 2, 5, 8, and 10.

### Session 12 — Color correction, speed, and masks

**Status:** Complete, 2026-10-08, version 0.12.0. The user explicitly authorized marking Session 12 complete; see the [acceptance record](evidence/session-12/manual-acceptance.json). All twelve suites pass, including 613 Session 12 checks. Verification and remaining limits are recorded in [Session 12 report](docs/session-12.md) and `evidence/session-12/`. Adds shared SDR color correction, embedded optional 3D LUTs, forward constant/ramped speed with source-bound preservation and the user's selected pitch-preserving audio policy, plus rectangular masks and chroma key. Native transactional controls, reset/bypass, undo and schema-4 persistence are included. Individual manual or visual test results were not reported; no computer use was performed. Professional color management, subjective audio quality, long retimed-audio seek performance and sustained effects-heavy 4K playback remain unverified.

**Deliverable:** A focused set of high-value visual controls.

- Add exposure/contrast, white balance, saturation, and an optional LUT path; document color-space assumptions and avoid implying professional color-management accuracy until validated.
- Add clip speed changes and then speed ramps with explicit frame/time remapping behavior.
- Add a bounded first mask/chroma-key workflow only after the shared preview/export graph can express it reliably.

**Acceptance:** Each effect has stable serialized settings, appears in preview and export, supports a defined reset/default, and has reference footage for edge cases. Speed edits preserve source bounds and audio policy is explicit (preserve pitch, shift pitch, or mute).

**Depends on:** Session 11.

### Session 13 — More capable audio mixing

**Status:** Complete, 2026-10-08, user-authorized. Implemented in version 0.13.0/schema 5; all 12 automated suites passed, including automation and bus persistence, shared-mixer preview/export checks, status meter/error controls, and independent audio export inspection. A later correction made audio automation key positions follow the selected seconds/frames display; the updated app builds, but the earlier suite run predates that correction. See the [acceptance record](evidence/session-13/manual-acceptance.json), [Session 13 implementation and limits](docs/session-13.md), [collected suite evidence](evidence/session-13/verification.json), and [time-display correction record](evidence/session-13/time-display-correction.json). Windows scaling, perceived audio quality, and end-to-end device latency remain unmeasured.

**Deliverable:** A reliable multi-track audio mix.

- Add volume/pan automation, track buses, fades, meters, and peak/clipping indication.
- Define latency, sample-rate conversion, clip-edge behavior, and optional audio effects.
- Keep audio render and preview behavior aligned.

**Acceptance:** A multitrack mix previews and exports in sync; automation saves/reopens correctly; clipping and missing-audio conditions are visible.

**Depends on:** Sessions 7–8 and the keyframe model from Session 11.

### Session 14 — Nested sequences and multicamera

**Status:** Complete, 2026-10-08, user-tested and user-authorized, version 0.14.0/schema 6. Automated verification passed across thirteen suites, including nested video/audio mapping, cycles and bounds, trim/split/undo, repeated/two-level/fractional-rate nesting, child effects/titles, retimed child audio, camera cuts, native offscreen controls, save/reopen and independently decoded exports including original-resolution nested 4K. Four distinct real 1080p sources meet the measured target-PC CPU/D3D11VA profile: 30 fps presentation cap, sub-second seeks, bounded working set and synchronized camera timestamps. The measured need requires a 360p decode cap per camera for three/four-camera preview plus coordinated buffering; final export uses full-resolution originals. Nesting requires matching frame rates and 1× parent mapping; speed remains editable inside the child. Camera source offsets provide manual alignment and audio stays on the reference camera. See [Session 14 behavior and limits](docs/session-14.md), [verification evidence](evidence/session-14/verification.json) and [playback gate](evidence/session-14/multicam-benchmark.json). The user reported testing the implementation and explicitly authorized marking Session 14 complete; see the [acceptance record](evidence/session-14/manual-acceptance.json). Individual test outcomes were not separately reported. No agent computer use was performed; documented frame-rate, preview-quality, scaling, perceived AV-sync and real synchronized-camera recording evidence limits remain.

**Deliverable:** The next timeline-level capabilities, added behind a stable sequence model.

- Add nested sequences first, with explicit source-time mapping and cycle prevention.
- Add multicamera grouping/switching only after synchronized multi-source playback has been measured on the target PC.
- Keep both features optional in project files so earlier projects remain editable.

**Acceptance:** Nested or multicamera sequences save/reopen and export consistently; missing camera media is reported; playback remains responsive at the supported track count.

**Depends on:** Sessions 4–5, 9–10, and 11.

### Session 15 — Device-aware export compatibility

**Status:** Complete, 2026-10-08, including the follow-up request to filter custom export choices and derive the default output frame rate from imported media. The original Session 15 implementation (version 0.15.1/schema 8) passed all fifteen suites in 140.72 seconds: 124 capability/profile checks and 763 every-encoder checks, with 40 independently inspected and decoded representative exports. Its evidence remains in [verification](evidence/session-15/verification.json), [encoder catalog](evidence/session-15/encoder-catalog-result.json), and `evidence/session-15/samples/`. The follow-up adds schema 9 persistence for manual frame-rate overrides; automatic output FPS is the lowest valid rate among imported video streams, falling back to the sequence rate. In custom export mode, the encoder list is filtered by bounded availability probes for the current settings, compatible containers are constrained by codec/muxer support, and manual pixel formats are limited to formats the editor can create while Automatic retains supported hardware formats. The updated Release application built successfully. No new automated suite or human UI acceptance was run for this follow-up; physical playback, Windows scaling, subjective quality/AV sync, every possible native-option combination, and long hardware stability remain unverified. The compositor remains opaque 8-bit SDR. Session 15.1 UI organization is implemented and verified below; Session 15.2 export tabs remain pending.

**Deliverable:** Export choices that adapt to the FFmpeg capabilities available on the current computer and produce files for a range of common playback devices.

- **Concept first:** Document the capability-discovery model, the relationship between output containers and encoders, proposed broad-compatibility targets, fallback behavior, and how unavailable choices will be explained. Complete this concept before implementation.
- Continuously consult the official FFmpeg documentation while designing and implementing this work. Check the relevant library/API, encoder, muxer, pixel-format, hardware-acceleration, and licensing documentation, and confirm each proposed option exists in the bundled FFmpeg build.
- Detect usable software and hardware encoders, muxers, pixel formats, and hardware devices at runtime. Treat compiled-in support and actual device/driver availability as separate facts.
- Define a small set of playback-device compatibility profiles with documented codec/container assumptions and representative test samples. Map those profiles to validated output combinations that the current FFmpeg build and device can actually provide.
- Retain a dependable software fallback where available, and report unsupported combinations with a useful explanation.
- Prevent unsupported or incompatible encoder/container/pixel-format/hardware combinations from being silently selected. If a setting conflicts with another choice or with capabilities detected on this computer, immediately identify the conflicting settings and explain the available fix; disable unavailable options with a visible reason where practical.
- Define how device-specific export choices behave when a project is opened on a different computer; do not silently assume that a hardware encoder or driver is present there.
- **User clarification, 2026-10-08:** Expose every video encoder in the bundled FFmpeg build by its specific name. Adapt native speed choices, quality scales, private options, pixel formats and compatible containers for each encoder; never substitute a shared CPU speed/quality scale for hardware or another software codec. Desktop playback is prioritized. Probe exact settings, retain unavailable compiled names with reasons, and block conflicting or unusable configurations.

**Acceptance:** The written concept is complete before code changes begin. Runtime choices reflect the bundled FFmpeg build and current device; unavailable hardware falls back or fails with a clear reason; incompatible choices are never left selected without an immediate, specific warning; representative outputs are independently inspected for container, codec, dimensions, frame rate, and audio; compatibility claims are limited to the devices and evidence actually covered.

**Depends on:** Sessions 0, 5, 8, 10, and 14.

### Session 15.1 — UI/UX organization and clarity

**Status:** Complete, 2026-10-08, with explicit user acceptance recorded in [manual acceptance](evidence/session-15.1/manual-acceptance.json). Version 0.15.2 with schema 9 unchanged. The concept below was completed before implementation. The workspace now has task menus, Project Media search and inline task feedback, explicit Sequence/Source viewing, a dockable selection Inspector with focused-dialog fallback, contextual timeline commands, clearer recovery/error states, and a summarized Export video dialog with expandable encoder details. All sixteen regression suites pass, including 58 production-workspace checks; the same 58 checks pass with the native Windows Qt platform. Agent visual review covered the 1,400 × 900 and 850 × 700 workspaces, empty/offline states, selected-clip Inspector and export-ready layout on the current computer. Other display scales/screens, subjective AV sync and long hardware/device playback retain explicit evidence limits. See [implementation notes](docs/session-15.1.md) and [verification](evidence/session-15.1/verification.json). Simple/Advanced tabs remain Session 15.2.

**Deliverable:** A more understandable, consistent native editing workspace and clearer user-facing workflows.

#### Current UI audit and design goals

The source-level audit covered `src/MainWindow.cpp`, `src/timeline/TimelineWidget.cpp`, `src/timeline/PropertyDialogs.cpp`, `src/timeline/EffectDialog.cpp`, and `src/export/ExportDialog.cpp`, plus the Session 15 export notes and evidence. The current shell has a left **Media bin** dock with import/relink/refresh buttons, a source/sequence tabbed viewer above the timeline, and a status bar with task progress and audio peak. The main toolbar has New/Open/Save/Import plus dock/reset controls. File contains project, media, recovery and export actions; media actions also appear in the dock; export is only in File. The timeline stacks sequence/multicamera, edit, and placement/property controls in three toolbars. Clip properties, effects, audio, and buses open separate dialogs.

This creates repeated entry points for importing, leaves the primary export action less visible than import, and puts common clip edits beside advanced nesting/multicamera controls. Some names describe implementation objects (`Media bin`, `Add at playhead`, `Track audio`) rather than the user's task. Empty, progress, and error feedback often shares the transient status bar. The sequence viewer remains above the timeline, but the Source tab replaces it while source footage is being previewed.

Session 15 materially expanded export: fixed playback targets now coexist with a Custom path; the bundled build catalogs 140 video encoder names, with exact-setting availability checks and encoder-specific options. In the collected representative probe, 101 configurations passed and 39 failed with reasons; these are outcomes for those tested configurations, not global encoder availability. The current single-page dialog exposes output, target, encoder/container/pixel format, dimensions/rate, native quality/speed/options, audio, capability status and details together. Users need a plain-language route through this complexity, while Session 15.2 remains responsible for specifying the exact Simple and Advanced tabs and their control boundaries.

This is a source audit; the current executable was not available as a target in the desktop inspection surface for a live screenshot. The wireframe below is a layout direction, not a claim about current pixel geometry or Windows display scaling. Implementation acceptance must include a focused visual review at the supported window sizes/scales.

#### Proposed organization

Keep one editing workspace so the user can see the sequence viewer and timeline together. Use a familiar menu row, a small task toolbar, and three clear content areas:

- **Project Media** on the left: one prominent **Import media** menu button for files or folders, searchable media list with clear Online/Offline/Importing states, and selected-item preview/details below. Keep relink and refresh actions on the selected offline/media item and in the Media menu; do not repeat a full button row for every action.
- **Viewer and Timeline** in the center: keep the sequence viewer above the timeline. Name the viewer tabs **Sequence** and **Source**. Selecting the timeline or changing its playhead returns focus to Sequence; opening a source is an explicit preview action. Keep the active sequence name, frame rate, duration and current position together above the timeline. Keep playback, seek and scrubbing visible while editing.
- **Inspector** as an optional, dockable right panel: show the selected media, clip, title or track context. Provide focused sections for **Clip**, **Effects**, and **Audio/Track**; opening a current properties/effects/audio command selects the matching section. Keep long forms scrollable. If the panel is hidden or the window is narrow, the same commands may open a focused native dialog. Never let the inspector cover the sequence viewer or timeline.

```text
┌ File  Edit  Media  Sequence  View  Help ── New  Open  Save  Import media  Export video ┐
├ Project Media ────────────────┬ Viewer ───────────────────────────────┬ Inspector ─────┤
│ Import files / Import folder  │ [ Sequence ] [ Source ]                │ Selected item  │
│ Search                        │                                        │ Clip           │
│ Media list · state            │           live preview                 │ Effects        │
│                               │       transport · position              │ Audio / Track  │
│ Selected item details         ├ Sequence · rate · duration ────────────┤               │
│ waveform when relevant        │ Undo Redo · Split · Trim · Delete      │               │
│                               │ [timeline tracks and playhead]         │               │
├───────────────────────────────┴────────────────────────────────────────┴───────────────┤
│ Ready / current task and progress                                      Audio peak      │
└───────────────────────────────────────────────────────────────────────────────────────┘
```

At narrow widths, the Inspector and Project Media remain dockable and hideable; the center workspace keeps its minimum space for the sequence viewer and timeline. Sequence controls for nesting and multicamera belong under **Sequence** and appear in a compact contextual group only when useful. Keep the main editing toolbar focused on Undo/Redo, Split, Trim, Delete, the existing per-track Ripple and Snap toggles, Add track, and the selected-media placement action. Put selection-specific properties/effects/audio commands in the Inspector/context menu instead of a permanently dense third toolbar. Preserve all current shortcuts and exact edit behavior, including ripple affecting only the edited track.

#### Terminology and interaction rules

Use these labels consistently in menus, buttons, tooltips and dialogs:

| Current wording | Proposed wording / rule |
| --- | --- |
| Media bin | **Project Media** |
| Import files / Import folder | **Import media** button with **Files…** and **Folder…** choices; keep explicit entries in Media menu |
| Open in source | **Preview source**; source view remains distinct from the sequence view |
| Show project sequence | **Return to Sequence**; timeline focus also returns to Sequence |
| Add at playhead | **Add selected media at playhead** |
| Clip properties | **Clip** in Inspector; use the same label in context actions |
| Effects and fades | **Effects** in Inspector |
| Track audio | **Track audio** in Inspector; explain that it affects the selected audio track |
| Export sequence… | **Export video…** in the primary toolbar and File menu; retain Ctrl+E |
| Capability details… | Keep under export's advanced/help area as **Why is this option unavailable?** / **Export details…** |

Use a concise label on each control and a short tooltip or inline helper for unfamiliar behavior. Examples: **Ripple on this track** explains that later clips move only on the edited track; **Add selected media at playhead** names both the source and insertion point. Keep codec names, FFmpeg option names, and device-probe diagnostics in Advanced export or expandable details. Never translate a short failed probe into a claim that an encoder is universally unsupported; say that this encoder/settings combination could not be validated on this computer and offer the relevant next step.

Menus should group actions by task: **File** for project create/open/save/recovery, Export video and Exit; **Media** for import, relink, refresh and cancel media work; **Edit** for undo/redo and standard clip commands; **Sequence** for sequence management, nesting and multicamera; **View** for Project Media/Inspector visibility, Frames/Seconds and Reset workspace; **Help** for a short getting-started guide, shortcuts, diagnostics and About. The intentional test-error command belongs only in a test/developer build, not the user-facing Help menu.

#### Common task flows

1. **Start or continue a project:** an empty workspace states **Import media to begin** and offers Import media plus New/Open project. Import progress appears beside Project Media with counts and Cancel; successful rows show ready state, and failures identify the file and recovery action. Dragging a media row to a track and **Add selected media at playhead** remain available ways to place it.
2. **Make a first edit:** select/preview a source, add it to a video/audio track, then edit with the sequence viewer and timeline visible together. Selecting a clip exposes the relevant Inspector section; changes continue through the existing undo/redo path. Add title and track actions are grouped with timeline creation tools. Sequence-only nesting/multicamera controls do not compete with routine trim/split controls.
3. **Export:** choose **Export video…** from the toolbar, File menu or Ctrl+E. The entry flow summarizes destination, playback target and audio, then makes the current capability check state clear. While checking, explain that the exact choices are being validated on this computer; only a successful check enables Export. On failure, name the conflicting/unavailable choices, explain whether the result is configuration-specific, and give the repair (for example, select a compatible container, change the encoder, or turn audio off). Show the actual encoder and any software fallback before export and in completion feedback. Session 15.2 will define which ordinary choices appear in Simple and which detailed encoder controls live in Advanced.
4. **Recover or relink:** recovery copy distinguishes **Recover selected**, **Later**, and the destructive **Discard selected…** action. A successful recovery says the project is recovered and asks the user to review it and save a working copy. Offline media stays visible with an Offline label and a direct **Locate file…** action; folder relinking reports how many references were restored and which remain offline.

#### Empty, progress and error states

Use an inline state at the point of work, with a next action, and retain the status bar for concise whole-app feedback. Provide distinct copy for no project/media/sequence, media import or relink in progress, export capability checking, export in progress/cancelling, success, and failure. Every blocking error should say what failed, whether project/media/output was preserved, and the next safe action. Keep detailed paths, library versions and encoder diagnostics behind a details affordance. Recovery must distinguish postponing, restoring and permanently discarding a snapshot.

#### Tradeoffs and scope boundaries

- The optional Inspector reduces modal hopping and makes selection context visible, but uses horizontal space. Keep it hideable and preserve a focused-dialog fallback; do not shrink the live sequence viewer below a usable size.
- A smaller primary toolbar improves scanning but moves occasional multicamera, nesting and diagnostic commands into a menu or contextual area. Preserve shortcuts and keep those actions searchable/visible in the Sequence or Help menu.
- Replacing repeated import buttons with one menu reduces duplication but adds a click to choose files versus folders. Keep **Import files…** and **Import folder…** as direct choices in the Media menu and empty state.
- Exposing a clearer export summary must not hide the Session 15 capability gate or suggest that profile names guarantee physical-device playback. The Simple/Advanced division and exact export control layout remain Session 15.2 scope.
- This session reorganizes presentation and explanations only. Do not change project data, codec support, export capability rules, timeline timing, playback/rendering, or edit semantics.

#### Acceptance checks for the implementation

- A production-UI check confirms each frequent action has the stated menu path, toolbar entry where specified, stable shortcut, and correct enabled/disabled selection context.
- In a new project, the next action to import media is visible without opening a menu; import, preview, add-to-timeline, Save and Export video can be located from the main workspace.
- The Sequence viewer remains above and usable with the timeline during edits; placing, trimming, splitting, seeking and changing the active sequence updates it. Source preview is clearly named and returning to Sequence is obvious.
- Selecting a clip/track/media item updates Inspector context; the same properties/effects/audio commands work in the focused-dialog fallback. No command changes the existing per-track ripple, undo/redo, persistence or playback behavior.
- Empty, importing, offline, recovery, capability-checking, export-ready, export-failed and export-complete states each identify the current state and a usable next step. A failed custom export probe is described as specific to the checked settings/computer and never silently enables Export.
- UI text uses the terminology table consistently; normal workflows do not surface raw FFmpeg/AVOption names. The test-only error action is absent from the production Help menu.
- Automated production-widget checks cover control presence, state changes and action routing. Visual review checks the default and narrow workspace at the target Windows scaling; any appearance/interaction that automation cannot establish remains explicitly unverified pending that focused review.

**Acceptance:** This concept and wireframe are complete before UI implementation. The implemented workspace follows these flows, keeps the live sequence viewer available during timeline work, uses consistent names and state feedback, and passes automated production-UI checks. Any visual or interaction evidence that cannot be checked automatically remains explicitly identified for focused user review.

**Depends on:** Sessions 1–14 and 15.

### Session 15.2 — Simple and Advanced export tabs

**Status:** Complete, 2026-10-09, accepted and authorized by the user. The annotated [dialog concept](docs/session-15.2-concept.md) was completed before implementation. Release version 0.15.4 and all eighteen regression suites passed; the expanded production export-tab suite passed 82 checks. Simple has target/size/quality/audio choices; Advanced groups runtime-backed encoder/container/hardware, exact FPS, quality/preset, pixel/color, audio bitrate and native profile/tune options. Cross-tab settings, immediate conflicts, compatible reset, schema-11 bitrate migration/save/reopen/undo and independently inspected/decoded exports are covered. See [Session 15.2 report](docs/session-15.2.md) and [verification evidence](evidence/session-15.2/verification.json). Offscreen normal/narrow layout review passed, and the user accepted the session; individual Windows appearance-check outcomes were not separately reported. Acceptance is recorded in [manual-acceptance.json](evidence/session-15.2/manual-acceptance.json). Physical-device playback, subjective quality, HDR and long hardware-session limits remain explicit.

**Deliverable:** An export dialog with a deliberately small Simple tab and a comprehensive Advanced tab.

- **Concept first:** Document the dialog layout and the exact boundary between Simple and Advanced, including control dependencies, defaults, validation, and how unavailable FFmpeg options and incompatible combinations are explained. Prepare annotated tab concepts before implementation.
- Keep Simple focused on a few clear decisions and compatible presets, without exposing a large matrix of codec and tuning choices.
- Provide broad customization in Advanced, organized into understandable groups such as container/video encoder, dimensions and frame rate, quality or rate control, encoder profile/preset/tune, pixel/color format, hardware acceleration, and audio codec/quality. Expose only combinations supported by Session 15's runtime capability discovery.
- Continuously consult the official FFmpeg documentation throughout design and implementation. Verify option names, valid values, and encoder/container interactions against the bundled FFmpeg version and build.
- Explain consequential settings in plain language, validate incompatible combinations before export, and provide a clear way to return to a known compatible configuration.
- Prevent incompatible choices from remaining silently selected. Disable choices that are incompatible with the current settings or detected capabilities where practical; if a user's change creates a conflict or an opened project contains stale choices, immediately show a warning that names the conflicting settings, explains why they conflict, and tells the user how to resolve them. Block export until the conflict is resolved.

**Acceptance:** The written concept is complete before implementation. Simple presents only a small set of understandable choices; Advanced exposes the supported customization with invalid or unavailable combinations prevented or explained. No incompatible combination can remain selected without an immediate, specific warning, and export remains blocked until it is resolved. Representative settings survive save/reopen where applicable, and independently inspected exports match the selected options.

**Depends on:** Sessions 8, 15, and 15.1.

### Session 15.3 — Cached encoder capabilities and primary-video export FPS

**Status:** Complete, 2026-10-08, user-authorized. The Release build succeeded; all five focused project, timeline, UI, export-FPS, and capability-cache suites passed, along with the existing export-device compatibility suite. Automated evidence covers schema 9 migration, Primary Video persistence/selection and FPS fallback/manual override, cache fingerprint invalidation and warm catalog hits, and device compatibility on this PC. The user observed the Primary Video status in the desktop app and reported a spacing issue; it was corrected and rebuilt, but the updated visual spacing has not been rechecked. Cross-PC GPU/driver behavior and measured launch-time improvement remain unverified.

**Deliverable:** Reusable, environment-aware FFmpeg capability results and a sequence-level Primary Video choice that supplies the automatic export frame rate.

- Preserve Session 15's asynchronous capability loading, initial `libx264` selection, export validation gate, and the existing **Refresh encoder capabilities** button.
- Store completed encoder catalogs and compatible-setting probe results in an atomic, persistent JSON cache under the user's application cache directory. Include a cache-format version and an environment fingerprint containing the FFmpeg version/configuration and linked library versions, plus each detected GPU adapter's identity and driver version.
- Key compatible settings by the complete normalized encoder/container/pixel-format, dimensions, exact rational frame rate, quality/native options, and audio configuration. Key broad custom-encoder catalogs by output size, exact rate, audio configuration, playback target, and the environment fingerprint.
- Load a matching catalog asynchronously from the cache on later launches to skip the full scan of all compiled encoders. Cache hits can populate compatibility choices, but hardware-dependent encoders must pass a fresh exact settings probe before export is enabled. Software-only cached checks may be reused while the complete fingerprint matches.
- Write only completed scans and checks. Ignore malformed, incomplete, oversized, incompatible-version, or environment-mismatched cache files and rescan. Cache read/write failures must leave export validation usable. The manual refresh button bypasses saved entries, refreshes relevant device/encoder checks, and atomically updates cache entries.
- Persist a primary video media ID and video stream index on each sequence. Let users set it from a selected timeline video clip and clear it when no clip is selected; initialize it to the first external video source placed on an otherwise video-empty sequence. Existing projects with video clips and no saved primary remain unset after migration, preserving their current sequence-FPS default. Do not change sequence frame rate, clip timing, track order, ripple behavior, or other edits when the choice changes.
- When export FPS is automatic, use the primary stream's inspected rational frame rate. If no primary is selected or its rate is unavailable, use the sequence frame rate. Preserve manual FPS overrides and existing save/reopen behavior.
- Migrate schema 9 projects by adding an unset primary-video choice; do not silently choose from existing clips during migration, so their current sequence-FPS default remains stable. Validate primary media/stream references and preserve the selection across save/reopen.

**Edge cases:** Empty projects and sequences with no video use sequence FPS; audio-only media and nested sequences cannot become an automatic primary source; the selected video stream is retained when a source has multiple video streams; VFR sources use the inspected stream rate recorded by import; unavailable media keeps its saved primary identity and metadata; split, trim, move, delete, and undo/redo preserve existing timeline edits; changing or clearing the primary choice never retimes clips. Cache corruption, partial/cancelled scans, failed atomic writes, changed FFmpeg builds, GPU changes, driver updates, and unknown driver versions must fail safely to fresh probing.

**Automated acceptance tests:**

- Cache JSON round-trips a complete encoder catalog and exact compatible settings; entries with a changed format version, FFmpeg fingerprint, GPU/driver fingerprint, malformed JSON, or missing encoder rows are rejected and rescanned.
- Exact cache keys distinguish fractional FPS, output dimensions, selected encoder/container, native options, quality, and audio settings. Cancelled scans are not saved; write failures do not crash capability checks; cache hits skip the broad custom catalog scan.
- A cached hardware result is never sufficient to enable Export: the selected hardware tuple is probed again before export. The manual refresh action bypasses prior results and replaces the matching cache entry.
- Project tests cover schema 9 migration, primary ID/stream round-trip, invalid or audio-stream references, and fallback for unset or unusable primary rates.
- Timeline tests cover automatic selection on first video insertion, explicit set/clear, undo/redo, and byte-for-byte-equivalent clip/track timing before and after changing the primary source.
- Export tests cover exact primary-stream FPS, sequence-FPS fallback, a designated primary with a different rate from other imported sources, and a preserved manual override.

**Acceptance:** Existing asynchronous export behavior and compatibility checks remain intact. A second custom-capability scan with a matching cache avoids probing the full encoder catalog, current hardware support is revalidated before export, and the automatic output rate follows the selected Primary Video without changing any timeline timing or edits.

**Depends on:** Sessions 2, 4, 8, and 15.

### Session 16 — Packaging, regression pass, and first-use documentation

**Status:** Complete for the personal local release, 2026-10-09, version 0.16.0. The user confirmed that it is good and authorized marking Session 16 complete. The pinned Release build and all nineteen automated suites pass. A fresh ZIP extraction with restricted PATH and isolated settings/recovery/cache passed 23 production workflow checks, both export tabs, independent output inspection/decode, cancellation and forced-termination recovery; 80 payload files and 396 PE imports were verified, and actual Qt/FFmpeg/MSVC DLL paths resolve to the package. See [Session 16 report](docs/session-16.md), [user acceptance record](evidence/session-16/manual-acceptance.json), [quick start](docs/quick-start.md), [format/performance limits](docs/supported-formats.md), [local release license review](docs/release-license-review.md) and `evidence/session-16/`. Individual native Windows appearance-check outcomes were not separately reported; no further check is required for this session. A genuinely new Windows account/clean OS and other computers remain unverified; external distribution is not approved because application licensing, exact corresponding source and supplier dependency notices remain unresolved.

**Deliverable:** A repeatable local install/build package and a usable first-release checklist.

- Create a clean Windows package with required Qt and FFmpeg runtime components, version/build info, and dependency notices.
- Review the selected FFmpeg configuration and all third-party licenses before distributing the package beyond the development PC.
- Verify install/launch, project creation, import, edit, save/reopen, export, cancellation, and recovery from a clean user profile, including capability detection and both export tabs.
- Write a concise quick-start guide and known-format/performance notes, including how to choose Simple or Advanced export settings and the limits of device-compatibility claims.

**Acceptance:** A clean install can complete the first usable workflow without developer tools; the package's included binaries match the recorded dependency/build inventory; export choices reflect the packaged FFmpeg build; known gaps and supported formats are documented.

**Depends on:** Sessions 0–15.3.

## Milestones

1. **Native foundation:** Sessions 0–2. Reproducible build and safe project format.
2. **First edit:** Sessions 3–7. Import, preview, timeline editing, titles, and audio.
3. **Core editing and export:** Sessions 8–10. First export, recovery, and performance.
4. **Advanced editing core:** Sessions 11–13. Effects/keyframes, color/speed/masks, and richer audio.
5. **Advanced timeline:** Session 14. Nested and multicamera editing.
6. **Compatibility and usability refinement:** Sessions 15–15.3. Device-aware export, UI/UX clarity, export customization, cached capabilities, and primary-video timing.
7. **Packaged first release:** Session 16. Installable package, regression pass, and first-use documentation.

## Cross-session engineering rules

- Keep timeline time exact and frame-aware; do not use floating-point seconds as the authoritative edit position.
- Keep UI responsive: probing, thumbnails, decoding, waveform generation, proxies, and export run in cancellable background work with bounded queues.
- Write projects atomically and maintain migration fixtures whenever the schema changes.
- Keep source media untouched. Use proxies only as regenerable edit aids; export originals unless the user explicitly chooses otherwise.
- Make preview and export share the same edit/effect description, and record any approximation as a known limitation.
- For Sessions 15, 15.1, and 15.2, write and complete a reviewable concept before implementation begins. Concepts should define user-facing behavior, workflows, supported choices, fallbacks/errors, and acceptance checks as relevant.
- For export work in Sessions 15 and 15.2, continuously consult the official FFmpeg documentation during concept and implementation. Verify APIs, encoders, muxers, pixel formats, hardware paths, option values, compatibility constraints, and licensing against the bundled FFmpeg build; never assume an option is available just because FFmpeg documents it.
- Export controls must identify incompatible setting combinations immediately: name the conflicting choices, explain the incompatibility, and give a resolution. Prevent or disable invalid combinations where practical, and do not start export until any conflict is resolved.
- Verify each feature at the right level using agent-run builds, deterministic fixtures, real sample media, independent export inspection, production UI tests and target-PC hardware measurements wherever possible. Keep automated evidence distinct from human perception or interaction that the agent cannot verify.
- **Minimize human testing (user requirement, 2026-10-07):** Do not include manual checks for anything the agent can verify through its own tests, scripts, probes, fixtures or other permitted tools. Ask the user to test something only when it is **absolutely necessary** because the relevant result cannot be verified by the agent. Complete all feasible agent-run verification first, explain the specific remaining evidence gap, and request only the smallest focused human check needed to close it. Do not provide blanket manual checklists or ask the user to repeat behavior already verified automatically. Computer use still requires explicit user authorization.
- Keep human testing instructions simple and easy to follow (user preference, 2026-10-06): use plain language, short numbered steps, exact button/menu names, concrete file paths, and a clear expected visible result for each check. Avoid bundling several tests into one step or assuming technical knowledge; explain any necessary technical terms. Prepare test files automatically and give their locations. Keep JSON inspection, malformed-file creation, permission changes, and other technical checks automated wherever possible, so the user mainly checks what they can see and do in the app.
- Preserve recoverability: keep working projects and media samples separate from generated proxies and temporary exports.

## Main risks and decision gates

| Risk | Gate / response |
|---|---|
| Source codec, variable frame rate, rotation, or 10-bit behavior differs from defaults | Inventory real clips in Session 0; add representative fixtures before committing to format promises. |
| Smooth 4K preview/export depends on codec and driver support | Benchmark in Session 5 and Session 10; retain CPU fallback and optional proxies. |
| Preview and export produce different pixels, timing, or audio | Use a shared render description; compare reference frames and audio/video sync before expanding the effect set. |
| Timeline features make project data hard to evolve | Version the project schema and keep migration fixtures from Session 2 onward. |
| FFmpeg build options affect available codecs and redistribution | Pin and inventory the build; review license implications before packaging or sharing binaries. |
| FFmpeg encoder or hardware support varies by computer and driver | Probe runtime capabilities, retain a tested software fallback, and limit compatibility claims to covered configurations in Sessions 15–16. |
| Export controls overwhelm users or expose invalid combinations | Keep Simple deliberately small; use a concept and runtime capability model to organize Advanced in Sessions 15.1–15.2. |
| Larger features interrupt progress on the first usable editor | Keep Sessions 11–15.2 as deliberate follow-on milestones; package the first usable release in Session 16. |

## References

- [Qt supported platforms](https://doc.qt.io/qt-6/supported-platforms.html)
- [Qt Widgets module](https://doc.qt.io/qt-6/qtwidgets-module.html)
- [FFmpeg documentation and libraries](https://ffmpeg.org/documentation.html)
- [FFmpeg licensing and legal considerations](https://ffmpeg.org/legal.html)
- [FFmpeg hardware acceleration and Media Foundation documentation](https://www.ffmpeg.org/ffmpeg-all.html)





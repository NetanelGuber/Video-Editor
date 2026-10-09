# Session 7: Audio and title basics

Complete, 2026-10-07, version 0.7.0. The pinned build and all seven automated suites pass, and the user confirmed that Session 7 works. See [verification](../evidence/session-7/verification.json), [audio/title report](../evidence/session-7/audio-title-result.json), and [user acceptance record](../evidence/session-7/manual-acceptance.json). Individual manual checklist outcomes were not separately reported. No computer-use automation was performed. Export remains Session 8.

## Audio workflow

Audio clips remain separate from video clips. Drag media onto an audio track, or select its audio stream in the timeline stream selector and use **Add at playhead**. Select among multiple audio streams there. All audible clips that overlap in sequence time mix together, including overlaps on one track and clips on multiple tracks. Empty intervals produce silence. Video track order does not suppress audio.

Double-click an audio clip, or select one and click **Clip properties**, to edit gain, mute, fade-in and fade-out. Gain is linear amplitude: 1 is original level, 0 is silent, 0.5 is half amplitude. Each fade is a nonnegative number of sequence frames, at most the clip's duration. At 30 fps, 30 frames is one second. Linear amplitude ramps multiply if they overlap. Shortening a clip clamps each fade to its new duration. Splitting keeps fade-in on the left half and fade-out on the right half, clamped to each half; it does not add fades at the cut. Moves keep clip-local fades.

Select an audio track header and click **Track audio**, or double-click its header name, to edit track gain, mute and solo. Track and clip gains multiply. Enabled solo audio tracks play together and exclude other audio tracks. Mute takes priority over solo; a disabled solo track has no effect on other tracks. Locked tracks reject all property edits until unlocked. Audio header labels show mute/solo/gain, and audio clip captions show their gain or muted state.

**OK** commits a property edit as one undo entry; **Cancel** leaves the edit and history untouched. Changes pause preview, retain the playhead, rebuild its shared description, and affect the next playback. Undo/redo restores audio and title settings along with selection. Opening properties does not round untouched numeric settings stored with more precision than their displayed controls.

## Title workflow

**Add title** places a three-second title at the playhead. Double-click a title clip, or select it and click **Clip properties**, to edit multiline text, font, size, alignment, text color, background color, normalized horizontal/vertical position, shadow and duration in sequence frames. The color chooser includes opacity; direct color fields use `#AARRGGBB`, where `AA` is opacity (`00` transparent, `ff` opaque). Size is in sequence pixels; preview scales it with the sequence image. Horizontal position anchors the left edge, center or right edge of the text according to alignment; vertical position centers it. Text wraps at 90% of the image width. Positions near an edge can intentionally crop text/background. The background surrounds the measured text box; shadow is a small black text offset. Invalid color/duration values keep the dialog open with a reason.

Applying title properties updates the live viewer and timeline caption. Style and duration changes form one atomic undo entry. Duration uses a normal out trim, regardless of the ripple toggle, and can extend the sequence when needed. Titles display only within their half-open clip interval, over the active video or over black in a gap. Track enable hides/shows titles. Split halves share a title record: style/text changes affect both; duration applies only to the selected clip. A locked use anywhere in the project rejects a shared style edit transactionally.

Title drawing uses the same app-owned rendering helper and ordered title records retained in the render description. Font availability is local to the machine; Qt falls back if a saved font is unavailable. The tests explicitly load installed Windows fonts because Qt's offscreen platform does not enumerate them automatically. Export must reuse or verify this rasterization policy in Session 8.

## Timing, mixing and persistence

The mixer converts absolute sequence frame boundaries to 48 kHz samples with integer rational arithmetic and Nearest rounding. It decodes each selected audio stream, resamples to stereo float PCM, schedules source-trimmed samples, applies clip/track gain and fade envelopes, sums overlapping sources, and clamps the final sum to [-1, 1]. There is no automatic normalization or limiter. Lower gain if a busy mix sounds distorted.

WASAPI remains the master clock for audible preview, with the existing explicit silent wall-clock fallback when the endpoint fails. A single mixer/output pipeline spans the sequence, including audio gaps and video cuts. Video cuts replace only the video worker, preserving the audio clock. Seeks and edits cancel and rebuild the pipelines asynchronously. Video decode may show a short black frame while loading a cut; this does not restart audio. Pausing preserves the clock and bounded queues.

The mixed queue holds at most eight 1024-frame stereo blocks (64 KiB). Audio sources open only as their intervals are reached. Each decoder retains the existing 24-block queue bound; at most 64 audio decoders are active at once. Exceeding that limit produces a visible diagnostic and silence rather than a partial unexplained mix. A missing/failed source produces silence for that source and a diagnostic while other sources continue. Decode cancellation wakes full queues; worker joins stay off the UI edit/seek path. The resampler's small unflushed tail retains the existing silence-padding policy (less than 1 ms).

Schema 3 adds `fadeInFrames`/`fadeOutFrames` strings. Schema 2 migrates with zero fades; schema 1 first converts its numeric frame fields and then gains zero fades. Existing title, track and clip values retain their IDs and settings. Older files become dirty on open and are written only on explicit save; overwriting keeps the predecessor bytes in `.bak` under the existing safe persistence policy. Current-schema files reject missing or unknown fields. See [project format](project-format.md) and the [schema-3 example](../fixtures/projects/multitrack-v3.veproject).

## Automated verification

Run from the workspace in PowerShell:

```powershell
./scripts/Build.ps1
./scripts/Test-Application.ps1
./scripts/Collect-Session7Evidence.ps1
```

The existing six suites pass along with `audio-title-basics`. The seventh suite checks every sample of a known six-second stereo mix against an independent arithmetic oracle, overlapping gains/fades, leading/trailing gaps, seek equivalence, source-trimmed impulse positions at 30000/1001 fps, saturation, muted/solo/disabled controls, offline-source continuation, fixed queue bounds and cancellation. It exercises real WASAPI or its explicit endpoint fallback across video cuts/gaps, checks that the two-source mixer is not restarted, and reaches the exact sequence end. Production offscreen dialogs cover all title/audio properties, invalid input, cancellation, precision preservation, atomic history, locked edits and exact save/reopen/rendered-pixel equality. Native persistence suites retain v1/v2 migration, malformed input and interrupted-save checks.

These are model, sample, native endpoint and offscreen Qt behavioral results. They do not establish audible sync or native Windows visual acceptance. `evidence/session-7/` includes the reports, build/test logs, executable/library/source hashes, deployed FFmpeg hashes, source footage size/mtime checks and a rendered title image. The generated starting project is disposable; tests recreate it without changing your separately named copy.

## Human Windows checklist

Use these files:

```text
App: B:\Coding\02-Projects\Tools\Video-Editor\build\session-7\Release\VideoEditor.exe
Project: B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\session-7\audio-titles.veproject
Your test copy: B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\session-7\my-audio-titles.veproject
```

The ruler uses frames at 30 fps. Keep the **Sequence** viewer visible and **Ripple** off. Widen the window or use toolbar overflow if a button is hidden. Scroll vertically to reach the Soundtrack and Titles tracks. Save to your test-copy path before changing anything.

1. Launch the app, open the project above, and **Save project as…** the test copy. Expect Video 1, Audio 1, Soundtrack and Titles, with a twelve-second sequence.
2. Seek to frame 0 and play. Expect the white square and beep together every second. From frame 90 (three seconds), also hear a continuous lower tone fading in; it should coexist with the beeps. Near the final second it should fade out. Pause, resume and seek inside the overlap; check that beep/flash sync stays steady.
3. Select the **Soundtrack** header, click **Track audio**, enable **Mute track**, and click **OK**. Play inside the overlap. Expect beeps only. Reopen it and turn mute off; both sounds should return.
4. In **Track audio**, enable **Solo track** for Soundtrack. Expect the tone alone. Also solo Audio 1: expect both sounds. Turn both solos off. Check that a muted solo track stays silent and disabling a solo track does not suppress another enabled track. Restore both tracks enabled and unmuted.
5. Double-click the Soundtrack clip. Change **Gain** from 0.4 to 0.2. Expect a quieter tone during overlap. Toggle **Mute clip** and expect only beeps. Restore mute off, then change track gain to 0.5 and confirm an additional level reduction.
6. Set Soundtrack track gain to 1 and clip gain to 0.4. Set both clip fades to 60 frames, then play from just before frame 90 and near the end. Expect two-second fades. Enter a fade longer than 270 frames; **OK** should keep the dialog open with a reason. Cancel that invalid edit.
7. Seek to frame 60. Expect the styled title in the viewer. Double-click its clip and change text, font, size, alignment, color, position, background opacity and shadow; click **OK**. Expect those changes in the viewer and the text in the timeline caption. Check that controls/text remain readable at your Windows scaling.
8. Set title duration to 90 frames. Its start is frame 30, so expect it visible at frame 119 and absent at frame 120. Use the frame controls/arrow keys for exact boundaries. Undo once: expect both styling and duration from the previous property edit to return together; redo should restore the edit.
9. Open title properties, change something and click **Cancel**. Expect the title unchanged. Lock its track using **L: open** and try changing properties or moving it; expect the edit rejected. Unlock it. Toggle **E: on/off** and expect the overlay to hide/show.
10. Change an audio clip setting, undo and redo it, and confirm the mix follows the restored settings. Save with **Ctrl+S**, close, relaunch and reopen your test copy. Expect the same title styling/duration, track gain/mute/solo and clip gain/mute/fades. Play again and check sync.

The user confirmed that Session 7 works. This checklist remains available for regression testing; report any failed step number and what you heard or saw.

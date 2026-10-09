# Session 8: First complete export

Complete in version 0.8.0, 2026-10-07. The pinned build and all eight automated suites pass, including 160 export checks. Automated validation is recorded in [verification](../evidence/session-8/verification.json) and the [export report](../evidence/session-8/export-result.json). The user tested the implementation, confirmed it works and authorized marking Session 8 complete; see the [user acceptance record](../evidence/session-8/manual-acceptance.json). Individual checklist outcomes were not separately reported. No computer-use automation was performed; documented limits remain.

## Export workflow

Use **File → Export sequence…** or **Ctrl+E**. Choose an `.mp4` output path, width/height, frame rate numerator/denominator, quality, encoding speed and whether to include audio. Initial size/rate follow the active sequence. Subsequent exports remember the choices stored in the project. **Export** records them as one undoable command; saving the project preserves them in the existing schema 3. Cancelling the dialog before starting leaves the project untouched. Exporting includes current unsaved edits.

The initial format is H.264 (`libx264`), 8-bit YUV 4:2:0, square pixels, limited-range BT.709 SDR, with optional 48 kHz stereo AAC at 192 kbit/s. Preset records live in [presets.json](../src/export/presets.json) and feed the controls and encoder settings. Quality is x264 CRF: lower numbers produce higher quality and generally larger files; 20 is the default. Encoding speed changes compression effort, not timeline speed. Size must be even and between 2 and 4096 pixels per axis; frame rate must be positive and at most 240 fps. Audio disabled produces a video-only MP4; enabled audio with no audible sources produces silence.

The dialog shows progress, **Cancel export**, and errors. Controls become usable again after completion, failure or cancellation, allowing another export. **Close**, the window close button, or Escape during export requests cancellation and closes after the worker exits. Choosing an existing output prompts **Replace export?**; declining keeps its bytes. Acceptance replaces it only after a successful export. Errors and results also enter the normal application log.

## Shared rendering and timing

Preview and export consume the same `compileSequence` description and `renderFrame` composition function, the same decoder and the same audio mixer. Export snapshots the graph when the dialog opens. The modal dialog prevents timeline edits during that export. Video selection preserves the current last-active-track rule, including overlapping clips on one track; lower tracks become visible when the upper clip ends. Gaps render black. Titles are drawn above video in track order with the saved font, alignment, colors, normalized position, background and shadow. Disabled tracks are skipped; track/clip gain, mute, solo and fades apply to the audible mix.

Export asks the decoder for full-resolution images rather than preview's 1280×720 cap. It rasterizes titles at output resolution, scales video to fit the sequence aspect, and adds black bars when output aspect differs. Title padding and shadow scale with the image. Preview now uses this same composition path for gaps and letterboxing. Source orientation and the chosen source stream/in point retain the existing decoder semantics. Unavailable fonts use Qt's platform fallback.

Frame scheduling uses exact integer rational conversion. When output rate matches the sequence, one output frame covers each sequence frame. For a changed rate, export samples the timeline at each output frame start and takes the latest source frame at or before that time. It repeats or skips source frames, without optical interpolation. Video count is the ceiling of the sequence duration at the output rate, so the final output frame covers the end rather than truncating it. This can extend video by less than one output frame. Audio ends at the nearest 48 kHz sample of the original sequence duration. Clip start/end/fade boundaries use the same frame-derived sample values as preview; export supplies the exact total sample count to the mixer to avoid a microsecond round trip.

H.264 delayed frames are flushed and carry explicit durations. AAC priming is described by MP4 timestamps/edit metadata. A decoder may return up to 1023 padded samples after the declared audio end; this is less than 21.34 ms at 48 kHz and does not shift the start or the flash/beep markers. The reference duration tolerance is 2 ms for matching frame rates; changing rate permits less than one output frame of coverage extension. Lossy output is compared with mean RGB error below 8/255 on reference frames and AAC RMS amplitude error below 0.012 against the uncompressed mix. These are fixture tolerances, not guarantees of professional color management.

## Cancellation, resource limits and errors

Decode, title drawing, mixing, encoding and file writes run off the UI thread. Offline video uses a bounded six-frame decoder queue and one frame of lookahead so it cannot accidentally encode a stale frame just because decode is slower than rendering. Audio retains eight mixed blocks, up to 64 simultaneous source decoders and the existing 24-block queues. Encoders use two threads each. Export uses CPU decode and encoding for correctness; hardware export and performance tuning remain later work.

MP4 writes through custom FFmpeg I/O into a `QSaveFile` temporary file beside the destination. Direct-write fallback is disabled. Only successful encoder flush, muxer trailer and file flush permit atomic publication. Failure, cancellation and ordinary dialog closure discard that temporary file and retain an existing output. Source-media destinations are rejected. Inputs remain read-only. An abruptly terminated process can leave an orphan temporary file, but cannot publish a half-written destination; automatic orphan cleanup is outside this session. MP4 metadata is at the end of the file, so this first preset is intended for local playback and does not claim web fast-start support.

Missing media is reported before export. Decoder/mixer, encoder, file-open/write/finalization failures stop publication with a reason. Enabled effects are explicitly rejected until Session 11. Existing preview limitations remain: one opaque active video source, no transform/effect/compositing stack, stereo downmix with final hard clipping, and the small resampler tail may be silence-padded. HDR tone mapping, professional color management, anamorphic media, source streams beyond current decode limits, long-project memory/performance and real phone/4K coverage remain unverified.

## Automated verification

```powershell
./scripts/Build.ps1
./scripts/Test-Application.ps1
./scripts/Collect-Session8Evidence.ps1
```

All eight suites must pass. `sequence-export` constructs a six-second cut with video trims, a 24 fps upper clip, a video gap, title boundaries and two overlapping audio sources with gain/fades. Independent FFprobe checks stream metadata, duration and frame count. A separate FFmpeg process decodes all output frames and PCM; references cover first/last frames, cuts/gaps, title interval boundaries and aligned flash/beep markers. The suite also covers fractional sequence/output rates, full-resolution title-only output, changed aspect, video-only export, offline/corrupt media, invalid output settings, atomic cancellation, preserved destinations, retry, undo/persistence, production export controls, replacement confirmation and closing during export.

The six Session 0 original samples and four pinned synthetic 4K/10-bit SDR/VFR/rotation samples are exported from trimmed source positions and decoded independently. These short exports establish format/decode coverage, not long-session performance or perceived playback quality. Evidence includes reports, output/reference stills, MP4 metadata, hashes, build/test logs and original-footage size/mtime comparisons.

## Human Windows checklist

Accepted historical reference; no repeat testing is requested. Future human checks must follow the plan's requirement to ask only when absolutely necessary.

App: `B:\Coding\02-Projects\Tools\Video-Editor\build\session-8\Release\VideoEditor.exe`  
Starting project: `B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\session-8\export-reference.veproject`  
Cancellation project: `B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\session-8\cancel-long.veproject`  
Output folder: `B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\session-8`

1. Launch the app and open the starting project. Use **File → Save project as…** to save `my-export-test.veproject` in the output folder. Expect a six-second sequence. Play it once to see the expected content.
2. Choose **File → Export sequence…**. Set **Output file** to `B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\session-8\my-export.mp4`. Leave 640×360, frame rate 30/1, quality 20, speed medium and audio enabled. Click **Export**. Expect progress and **Export complete**. Check that the dialog text and controls are readable at your Windows scaling.
3. Open `my-export.mp4` in Windows Media Player or VLC. Expect six seconds: a moving pattern at the start, a green upper clip from two to three seconds, black video from four to five seconds, and the pattern again during the final second. The title is visible from one to four seconds. Expect no missing first/last image, unexpected stretching or end cutoff.
4. Replay the MP4. Check that the white square and beep happen together at zero, one and five seconds. From three seconds, also hear a tone that fades in and out. Compare with the editor's sequence playback; expect matching title appearance and audio levels. Beeps continue through the green clip and black gap.
5. Export again to `my-export-muted.mp4` in the same folder with **Include audio** unchecked. Open it in the separate player. Expect the same picture with no sound.
6. Close the export dialog, save the test project, close the app and reopen that project. Open **Export sequence…**. Expect the previous size/rate/quality/speed/path/audio choices restored. Close the dialog.
7. Open the cancellation project, discarding only changes to your disposable test copy if prompted. Open **Export sequence…**, select the existing `my-export.mp4`, set 1920×1080 and speed slow, enable audio, and click **Export**. Choose **Yes** in **Replace export?**. Click **Cancel export** while progress is running. Expect **Export cancelled** and usable controls. Reopen `my-export.mp4` in the separate player: it should still be the original six-second export.
8. In that dialog, choose `my-export-retry.mp4`, size 640×360 and speed veryfast. Export again. Expect completion and a one-minute MP4 that plays in the separate player. Start another export to `my-export-close.mp4` and immediately click **Close**; expect the dialog to close after cancellation and no partial MP4 to be published.

The user accepted Session 8 after testing it. This checklist is retained as a historical reference, not a request to repeat these checks. For future work, follow the plan's requirement to ask for human testing only when absolutely necessary and never duplicate checks the agent can perform.



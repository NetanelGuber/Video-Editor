# Session 5 — Playback and rendering spike

Complete, 2026-10-06, in version 0.5.0. Automated validation is recorded under `evidence/session-5/`. The user accepted Session 5 and authorized marking it complete after the manual checklist was provided; individual checklist outcomes were not separately reported. See the [user acceptance record](../evidence/session-5/manual-acceptance.json). Automated rendering and endpoint tests do not independently establish visible playback or perceived audio/video sync.

## What is available

The workspace has **Sequence** and **Source** tabs. Select a media-bin item and click **Open in source**, or double-click it. **Preview selected in sequence** loads a temporary source preview without editing or saving the project. **Show project sequence** returns to the saved active sequence. Both monitors offer **Play/Pause**, a seek slider, **Previous frame**, **Next frame**, CPU/D3D11VA decode, and CPU/OpenGL presentation. Changing tabs or playing the other monitor pauses the previous one.

The existing timeline rows remain a summary. Interactive editing is Session 6. The prepared project below is a real persisted sequence with separate video/audio clips, a one-second gap, and source trims.

## Rendering and audio decision

Use FFmpeg library decoding behind app-owned types and retain CPU conversion/presentation as the default. D3D11VA is an optional measured decode path. It downloads decoded GPU frames before the same SDR conversion used by the CPU decoder; it is not a zero-copy renderer. OpenGL/QPainter texture presentation is available for comparison with the raster QWidget path. The standalone benchmark uses a hardware OpenGL framebuffer on the RX 9070; visible QOpenGLWidget interaction still needs the checklist below. The benchmark results and tradeoffs are in [rendering decision](rendering-decision.md).

Select **WASAPI shared-mode output**, with interleaved 48 kHz stereo float PCM from FFmpeg's swresample. Device enumeration, initialization, writes, and release happen on a dedicated COM STA thread. The clock is submitted PCM frames minus WASAPI's current queued frames; video chooses the newest decoded frame at/before this clock. Pause stops the endpoint without throwing away its pending samples. A seek starts a new endpoint/decoder epoch so old PCM cannot play at the new position. Device errors switch to a silent monotonic clock and appear in monitor diagnostics. Silence is generated for audio timestamp gaps. Underruns freeze the audio clock until more samples arrive, and are counted. Endpoint latency beyond the shared buffer is not calibrated; perceived sync is a human check.

Qt Multimedia was not measured or downloaded: the pinned Qt base package does not contain it. WASAPI supplies the needed endpoint and clock without changing dependency pins. This is a feasibility choice, not a comparative claim about Qt Multimedia quality.

## Thread, queue, and cancellation rules

- One video decode worker, one audio decode worker, and one audio-output worker per monitor; at most six worker threads with both monitors loaded. Qt handles transport and presentation, never blocking media reads or device initialization.
- Demux and codec work happen on each decode worker. Packets are decoded immediately rather than building a second packet queue. FFmpeg's send/receive backpressure and the bounded output queues govern production.
- Video queue: six images, each at most 1280×720×4 bytes (about 21.1 MiB per monitor), plus the current displayed image and one seek-preroll reference. Rotation preserves the pixel count. Decoder reference surfaces and driver allocations are additional bounded working memory, measured separately.
- Audio queue: 24 blocks, each at most 4096 stereo float sample frames (0.75 MiB). The resampler rejects excessively large input/output blocks; the shared endpoint requests 100 ms and rejects buffers above one second.
- Video decode rejects frames above 4096×4096 pixels. Probe budget is 8 MiB, analysis is capped at three seconds, streams at 64, seek index at 1 MiB, and selected compressed packets above 64 MiB are rejected. Local-file protocols only.
- Cancellation wakes full queues and interrupts FFmpeg I/O. Each I/O operation has a ten-second deadline. Seek/scrub requests coalesce to the latest target; a replacement starts after cancellation finishes, without waiting on the UI thread or accumulating workers. Synchronous joins occur only at destruction. Driver calls are not forcibly terminated; app closing can wait for in-flight native work.
- Video seeks decode from a prior keyframe, retaining only the last frame at/before the requested point before presentation. Audio seeks include 100 ms of decoder/filter preroll, then trim PCM to the requested sample. Repeated frame stepping converts from the sequence/source rational rate rather than repeatedly adding a rounded frame duration.
- Source frame stepping uses the stored average frame rate. On VFR footage it moves along that nominal grid and can show the same source frame twice; timestamp-based playback/seek remains supported. Exact adjacent-source-frame stepping is a later refinement.
- D3D11VA device/configuration/decode/transfer failures retry with a fresh CPU decoder and preserve the fallback reason. OpenGL creation failure selects the raster presentation path. Offscreen UI tests deliberately use raster presentation.

## Shared preview/export boundary

`playback/RenderDescription.h` owns the boundary. A sequence description retains the complete ordered sequence, media, and title records, including opaque effect data. The authoritative edit graph keeps rational frame rates and integer source ticks/sequence frames. Its derived half-open microsecond schedules are rounded once to nearest (at most 0.5 µs), and provide clip-to-source mapping to preview. The frame-rate conversion and full graph remain available to the future export evaluator.

For this spike, the evaluator picks the last active video track and one last active audio clip. It honors disabled tracks, audio mute/solo/gain, gaps, source trims, and track order. Video clips do not implicitly select an audio stream in a saved sequence: audio clips reference their own stream. The source monitor uses the first video/audio streams, preserving their relative stream-start offsets; other audio streams are tested through the native decoder but have no UI selector yet.

Known differences carried forward: preview fits within 1280×720, uses 8-bit SDR images, and drops late frames to follow its clock. Offline export will use original media, the chosen output size, and deterministic frame/sample iteration without realtime drops. Titles/effects remain in the shared graph but are not evaluated yet. Multitrack compositing, audio mixing, full color management/HDR tone mapping, and an export evaluator are later sessions. No preview/export pixel equivalence is claimed before Session 8. Cut changes currently restart the pipeline and can briefly buffer; preloading cuts is a later performance improvement.

## Automated verification

Run from the workspace in PowerShell:

```powershell
./scripts/Build.ps1
./scripts/Test-Application.ps1
./scripts/Measure-Session5Playback.ps1
./scripts/Test-Session5Encoders.ps1
./scripts/Collect-Session5Evidence.ps1
```

The five CTest suites cover project persistence, offscreen application lifecycle, media import, timeline editing, and playback. Playback covers all six real/four synthetic Session 0 clips plus the flash/beep fixture: CPU and actual D3D11VA start/middle/end seeks, every audio stream, source rotation/VFR, resampling and sample trim, fixed queue backpressure, cancellation of full queues, injected unavailable acceleration, missing media, a retained render graph, gap/source-in evaluation, paused seeks, sixty scrub requests followed by the final target, exact sequence end, and an independent FFmpeg reference-frame comparison. The monitor/controller runs offscreen; WASAPI uses the actual default endpoint. The benchmark opens no visible application window and uses no computer-use automation.

Prepared test files are generated automatically by the test script; originals in `B:\Videos` stay untouched. Encoder tests create small disposable MP4 files and independently probe/decode them; they implement no export feature.

## Human Windows checklist

Perform these yourself. Report which step failed and what you saw/heard. If they all pass, say **“Session 5 checklist passed.”**

1. Launch `B:\Coding\02-Projects\Tools\Video-Editor\build\session-5\Release\VideoEditor.exe`. The **Sequence** viewer and timeline summary should both be visible.
2. Use **File → Open project…** to open `B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\session-5\playback.veproject`. A moving test pattern's first frame should appear while paused.
3. Click **Play** in the Sequence tab. The white square in the picture's top-left corner and the beep should happen together, about once per second.
4. Let that sequence finish. At about 3 seconds there should be one second of black picture and silence. The pattern and beeps should return at about 4 seconds. Playback should stop at 7 seconds.
5. Select **flash-beep.mp4** in the media bin, then click **Open in source**. The Source tab should show the clip with a total duration of 12 seconds.
6. Click **Play**, then **Pause**. The picture/time should stop and the sound should stop promptly.
7. Drag the slider to roughly the middle while paused. The picture should update, the displayed position should change, and audio should remain stopped.
8. Click **Next frame**, then **Previous frame**. The position should advance by about 0.017 seconds and move back by about that amount; the image should update where the pattern changes.
9. Select **D3D11VA decode (CPU fallback)** and click **Play**. The picture and beeps should continue. On this PC the status should name D3D11VA; if it shows CPU fallback, tell me.
10. Select **OpenGL presentation**. The picture should remain correctly oriented and look the same. If it returns to **CPU presentation**, tell me.
11. While playback runs, resize the window and move the media-bin dock. Controls should respond promptly and the picture should continue updating.
12. Use **File → Import files…** to import `B:\Videos\Acu - 68% to 100%.mkv`. After import finishes, double-click that media item. Click **Play** and confirm its picture and sound play together.
13. Pause that real clip and drag the slider to a later point. It should show the new frame after a short loading period and stay paused. Click **Play** to confirm sound resumes from that point.
14. Import and open `B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\pinned\synthetic-rotation90-h264.mp4` the same way. Its picture should be portrait-oriented.
15. Import and open `B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\pinned\synthetic-4k-hevc10-aac.mp4` the same way. The short clip should play and seek without freezing controls. This is synthetic SDR coverage, not real phone/HDR acceptance.

## API references

The implementation follows [FFmpeg's hardware decode example](https://www.ffmpeg.org/doxygen/8.1/hw_decode_8c-example.html), [Qt's OpenGL/QPainter example](https://doc.qt.io/qt-6/qtopengl-2dpainting-example.html), and Microsoft's [WASAPI initialization](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-initialize) and [queued-frame clock semantics](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getcurrentpadding). Builds compile against the pinned local headers; the linked API pages are supporting documentation.

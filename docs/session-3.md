# Session 3: media import and inspection

Completed 2026-10-06, version 0.3.0. Native build and automated checks pass. The user accepted Session 3 and authorized marking it complete after receiving the manual checklist below. See the [user acceptance record](../evidence/session-3/manual-acceptance.json); user-reported acceptance is separate from automated evidence.

## Deliverable

`build/session-3/Release/VideoEditor.exe` adds **File → Import files…** (Ctrl+I), **Import folder…**, **Relink selected media…**, **Refresh selected media**, and **Cancel import**. The media dock also has buttons for these actions. Selecting a media row shows its source path, state, container, size, duration, video/audio streams, exact frame rate/time base, frame dimensions, bit depth, and rotation. Each video has an oriented still thumbnail; audio has a peak waveform. The sequence viewer stays in place; actual playback remains Session 5.

Imports reference original source paths. They do not copy footage into a project or change its contents. Folder import includes subfolders, skips symbolic links, and filters common video/audio extensions; **Import files… → All files** can explicitly attempt other file types. Duplicate detection uses the case-insensitive canonical file path, including directory junction aliases. Copies at different paths are separate media; this is not a content/hash deduplicator.

Probing, folder enumeration, packet timing sampling, thumbnail decode, and waveform decode run on one cancellable background worker. Only app-owned Qt/project values cross into the UI. One result is allowed in flight; the UI acknowledges it before the worker proceeds. Switching documents or closing cancels outstanding work, and document generations prevent late results entering another project. A cancellation keeps completed imports.

The limits are 1000 media records per project, 1000 inspections per batch, 64 retained thumbnail/waveform results, 100 retained failed-import rows, two decoder threads, 64 probe streams, an 8 MiB probe budget, 5 seconds of stream analysis, a 15-second probe/timing deadline, and a 20-second preview-aid deadline. Preview decode accepts at most 4096 × 4096 pixels per frame. These bounds are deliberately conservative for this slice; **Refresh selected media** regenerates an evicted aid. Session 10 retains the broader disk-cache/proxy/performance work.

## Metadata and preview policies

- All video/audio stream indices are retained, including both audio streams in `Tomato 4.mp4`. Subtitles, data streams, and attached cover images are outside the project model.
- Frame rate uses the container's average rate, falling back to its nominal rate. Rationals are reduced; source start/duration stay integer ticks. Video with no usable frame rate is rejected with a useful message instead of inventing editing metadata.
- Container duration is reported in microseconds. Stream duration uses reported ticks when present and a labeled container estimate otherwise. Zero means unknown. A duration estimate is not a decoded integrity check.
- Timing samples up to 4096 packets / 512 video timestamps per stream. Packet PTS are sorted into presentation order; an incomplete B-frame tail is discarded and one tick of container rounding is allowed. The synthetic variable-rate files are identified without misclassifying the MKV samples' alternating 6/7 ms intervals. The UI labels the sampled scope; constant sampled intervals do not establish whole-file CFR.
- A thumbnail is the first decodable video frame, scaled and rotated using the display matrix. These are CPU SDR inspection aids; color management, HDR tone mapping, non-square pixel correction, and mirrored display matrices are not validated here.
- Waveforms contain 256 absolute peak bins across all channels of the **first audio stream's opening 30 seconds**, or its shorter available span. They retain silence without normalization. They are basic source inspection aids, not a complete timeline waveform or synchronized audio preview. If a resource limit stops analysis, notes explain that remaining bins are blank.
- Missing/unreadable files show **Offline**. Malformed/non-media imports show **Unsupported** with a reason in the inspector; failed imports are transient diagnostic rows and are not written as invalid media records. A known stream without a decoder retains metadata and shows **Unsupported stream**. Decode-aid errors appear in inspection notes.
- Schema 2 remains unchanged. Imported streams/path/size/container persist through the existing atomic project writer. Thumbnails, waveforms, container microsecond duration, detailed probe notes, and failure states regenerate at runtime. Available project sources are reinspected asynchronously on reopen without changing saved metadata or dirtying the project.

Relinking probes the replacement first. It preserves the media ID/name and all sequence/clip records, updates the source path and metadata, and marks the project dirty. The original stream indices, codec, time base, frame rate, frame size/bit depth, orientation, start ticks, sample rate, and channel count must match. Existing clip ranges must fit the replacement, and the path cannot belong to another media record. An incompatible, too-short, missing, or unreadable replacement leaves the project untouched. This is a basic selected-file relink; bulk search, automatic recovery, and richer relinking remain Session 9.

## Verification

| Check | Evidence / result |
|---|---|
| Pinned MSVC/Qt/FFmpeg native configure/build | Passed from a fresh build directory; [configure](../evidence/session-3/configure.log), [build](../evidence/session-3/build.log) |
| All six Session 0 real samples and all four synthetic samples | Native inspection and oriented thumbnails passed; exact stream indices, dimensions, bit depth, orientation, codec, time base, start/duration ticks, frame rate, container duration and size agree with a fresh independent FFprobe process |
| AAC, FLAC, ALAC and audio-only PCM waveform | Passed; peaks agree with independent FFmpeg floating-point PCM decode, including a valid silent first stream |
| All 30 original files in `B:\Videos` | Passed actual offscreen application folder import; all 30 populate the bin with no failures |
| UI responsiveness during background imports/reopen | Passed 10 ms event-loop heartbeat check; measured maximum gap is in [media report](../evidence/session-3/media-result.json) (500 ms test ceiling) |
| Folder recursion, duplicate paths/casing, failed/missing media, import dirty state, project round-trip, regenerated aids | Passed actual application methods/widgets with Qt's offscreen platform |
| Relink success, Unicode path, stable media ID and exact clip edits; incompatible/missing/too-short source rejection | Passed native model and offscreen application checks |
| Immediate cancellation, cancellation after a completed import, late-result rejection after New project | Passed offscreen worker/application checks |
| Previous model/filesystem/interrupted-save/startup/error/layout checks | Passed regression tests; [project report](../evidence/session-3/project-result.json), [shell report](../evidence/session-3/smoke-result.json), [test log](../evidence/session-3/test.log) |
| Original footage preservation | All 30 source sizes and full Windows modification timestamps match Session 0; [final verification](../evidence/session-3/verification.json) |
| Windows interaction and visual acceptance | Accepted by the user, 2026-10-06; [acceptance record](../evidence/session-3/manual-acceptance.json). Individual checklist outcomes were not separately reported. |

The metadata comparisons cover the declared stream fields; whole-file decode/integrity, audio synchronization, GPU acceleration, playback, and real 4K/phone/HDR behavior remain unverified. Existing Qt deployment warnings about translation catalogs and optional Vulkan headers remain unchanged. FFmpeg emits upstream UDTA/AAC timestamp warnings for some inputs; inspection and independent probe checks pass. No computer-use automation was performed.

## Human Windows checklist

The user accepted Session 3 on 2026-10-06. This checklist is retained for future regression testing.

The test files have already been prepared under `B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\session-3`. You do not need to edit JSON or move any original footage. To prepare them again, run `./scripts/Prepare-Session3Tests.ps1` and `./scripts/Test-Application.ps1` (the test run also creates the offline project).

Launch the app yourself:

```powershell
Start-Process -FilePath 'B:\Coding\02-Projects\Tools\Video-Editor\build\session-3\Release\VideoEditor.exe'
```

1. Choose **File → Import folder…** and select `B:\Videos`. Expected: **30 media file(s)** in the bin after import finishes. While it imports, resize the window or open **View**; the app should respond.
2. Select **Tomato 4.mp4** in the bin. Expected: a still image and details showing **1920 × 1080**, **144/1 fps**, one video stream, and **two audio streams**. The first audio stream is silent, so its waveform can be flat.
3. Choose **File → Import files…** and select `B:\Videos\Tomato 4.mp4` again. Expected: still **30 media file(s)**; the status says one duplicate was skipped.
4. Choose **File → Import files…** and select `B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\pinned\synthetic-rotation90-h264.mp4`. Select its new row. Expected: a **portrait** still image, **rotation 90°**, and **variable intervals observed** in the details.
5. Choose **File → Import files…** and select `B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\session-3\import\audio-only.wav`. Select its row. Expected: **48000 Hz**, **2 channels**, and a visible repeating blue waveform. A waveform is a picture of sound loudness; this step does not play audio.
6. Choose **File → Import files…** and select `B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\session-3\import\corrupt.mp4`. Expected: an **Unsupported** row; selecting it explains that the file is invalid. Other media remains available.
7. Choose **File → Save project as…** and save as `B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\session-3\my-import-test.veproject`. Close and reopen the app, then open that project with **File → Open project…**. Expected: your successful media rows remain, and thumbnails/waveforms return after background inspection. The failed import row is not saved.
8. Choose **File → Open project…** and open `B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\session-3\offline.veproject`. Expected: one media row marked **Offline**.
9. Select that Offline row, choose **File → Relink selected media…**, and select `B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\session-3\import\subfolder\short-video.mp4`. Expected: the row becomes **Available**, a still image and waveform appear, and the project title gains `*` to show an unsaved change.
10. Choose **File → Save project as…** and save as `B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\session-3\my-relinked-test.veproject`. Reopen that file. Expected: the media remains **Available**.

For future regression testing, report any unreadable controls, freezes, wrong details, sideways thumbnail, missing waveform, or relink/save/reopen problem.

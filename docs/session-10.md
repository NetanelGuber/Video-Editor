# Session 10 — Proxy gate and 4K editing performance

Complete for the agreed editing profile, 2026-10-07, version 0.10.0. Native C++20/Qt Widgets/FFmpeg; project schema remains 3. All ten automated suites pass. The user confirmed the focused visible playback check and authorized completion; see the [user acceptance record](../evidence/session-10/manual-acceptance.json). No agent computer use was performed. Documented coverage gaps remain.

## Agreed editing profile

The user selected 30 fps preview at 1080p or below, seeking under one second, and bounded memory. Export retains the chosen full sequence/output resolution and frame rate. The recommended starting profile on the i7-13700F/RX 9070 is **Preview: 720p**, **Standard memory**, **CPU presentation**. CPU decode works for the measured synthetic 4K 30 fps sources; select **D3D11VA decode (CPU fallback)** to reduce HEVC decode CPU work. Hardware is optional for project correctness.

Both source and sequence monitors now offer **Preview: 360p**, **Preview: 720p**, and **Preview: 1080p**. **Low memory (360p)** overrides the selected quality and restricts the converted-video queue to 4 MiB. Quality/memory changes cancel the old pipeline and seek the latest retained playhead; they do not change clips, original paths, saved projects, export settings, or undo history. Playing presentation is capped at 30 fps; pause/seek/frame stepping still use exact edit positions. High-rate originals are decoded with their original timestamps, so dropped source frames in diagnostics can reflect deliberate preview sampling.

## Proxy decision

Session 10 makes proxy generation conditional on a meaningful measured need. The current release, same-PC Session 9 comparison, Session 5 baseline, and edited 4K measurements support editing the available originals directly. Optional proxies were therefore **not added**. No proxy timing, switching, or failed-proxy acceptance is claimed. The timeline keeps exact original source ticks; relinking and final export continue to use originals. Reopen this decision if longer/real 4K, a different codec/storage path, or substantially more concurrent audio sources fails the agreed profile. The existing renderer evaluates one winning video layer; this pass does not establish simultaneous multilayer 4K compositing performance.

## Caching and memory

- Persistent thumbnails and opening-30-second, 256-bin waveforms are cached together in atomic JSON/PNG entries under the application’s local cache directory, `media-aids-v1`. Metadata is always re-probed from the original before using cached aids.
- Keys include the aid/dependency version, canonical path, size, modification time, and SHA-256 of the first/last 64 KiB. Relinks get new keys; missing originals remain offline. This is a bounded aid fingerprint, not a whole-file identity guarantee against middle-only changes that also preserve size/time.
- Entries are limited to 1 MiB each, 1,024 entries and 256 MiB total, with oldest-use eviction. A process mutex plus a short cross-process lease protects writes/retention. Images and waveform arrays are checked before decoding/allocating. Corrupt, deleted, disabled, unwritable, or over-budget caches regenerate without failing imports. Interrupted analysis never publishes an aid entry.
- The media bin retains at most 64 aid results and 8 MiB of image/peak payloads; row pixmaps and metadata have separate bounded entry limits. Evicted aids return through **Refresh selected media** from disk cache.
- Converted video queues retain at most six frames **and** 32 MiB in standard preview, 4 MiB in low-memory preview, or 64 MiB in export. A frame that cannot fit reports an actionable error; selecting lower quality can recover. Full queues wake on cancellation. One conversion image and held/presented images live outside the queue.
- Original codec reference frames, hardware surfaces, audio buffers, Qt/font resources and export encoder memory are additional allocations. Low-memory mode reduces converted preview buffers, not original-resolution decoder reference frames. The budget is not a hard process-RSS limit. Existing pixel/packet/stream/thread bounds remain; audio retains bounded decoder and mixed-PCM queues.

`VIDEO_EDITOR_CACHE_DIR` redirects caches for isolated tests only. Normal settings and source media are untouched by the test scripts. Cache deletion is safe while the app is closed; project/backup files are not caches.

## Verification and performance evidence

All ten automated suites cover the previous editing/export/recovery workflow plus Session 10. The new `PerformanceTests` suite constructs and saves a 20-second, 3840×2160, 30 fps montage alternating the pinned 4K H.264 and 10-bit SDR HEVC fixtures, with full-resolution titles and faded audio. It drives production offscreen monitor controls, CPU and actual D3D11VA playback, seeks, exact edits/undo/persistence, low-memory retries, unavailable-hardware fallback, cache failures/invalidation/retention, and full-resolution export. Independent FFprobe/FFmpeg verifies every exported frame, metadata/audio duration and original/title pixels at first/last/cut boundaries. The 4K export uses `ultrafast` only for timing evidence; normal export retains its existing quality/preset choices.

The detailed edited results are in [performance-result.json](../evidence/session-10/performance-result.json), the full corpus/source comparison and hardware OpenGL framebuffer results in [benchmark/summary.json](../evidence/session-10/benchmark/summary.json), and build/runtime/source checks in [verification.json](../evidence/session-10/verification.json). The source comparison uses the same native paced decoder harness as Session 5, including all six real and four synthetic sources; it compares the actual 0.9.0 and 0.10.0 builds sequentially on this PC and includes a 20-second stability run. The editor’s 30 fps composition cap is measured separately in the production monitor suite.

| Edited 4K, 720p preview, 20 s | CPU decode | RX 9070 D3D11VA |
|---|---:|---:|
| Startup | 38 ms | 93 ms |
| Four seeks | 73 / 375 / 379 / 50 ms | 119 / 184 / 179 / 96 ms |
| Delivered preview cadence | 29.70 fps | 29.39 fps |
| Peak process working set | 282.9 MiB | 206.7 MiB |
| CPU, one-core percentage | 50.0% | 35.9% |
| Largest UI timer gap | 7 ms | 7 ms |

The four-second, 120-frame full-resolution H.264/AAC export took **3.58 s** at the `ultrafast` test setting and peaked at **646.0 MiB** process working set. This validates the selected profile, not a real-time export promise for longer projects or the default `medium` preset. Four first/last/cut frame comparisons had mean RGB differences of **5.17–5.51 / 255**, within the documented lossy/reference-scaling tolerance of 8. Cold/warm synthetic aid inspection took **58 / 20 ms**, including fresh metadata probing on both runs. The final Session 10 suite has **78 checks**.

All 20 current raster source/decode combinations presented their expected samples with zero drops. Synthetic 4K H.264 CPU/D3D11VA seeks were **98 / 132 ms**, and HEVC10 **256 / 111 ms**, closely matching both same-PC 0.9.0 and the Session 5 baseline. The 20-second 144 fps HEVC stability run presented **2,880 frames with zero drops** and finished at **85.9 MiB** working set. All four hardware OpenGL FBO comparisons reported **AMD Radeon RX 9070** and zero pixel difference against raster; one real high-rate GL run dropped three frames, reinforcing the existing CPU-presentation recommendation. These are decoder/presentation harness numbers, distinct from the edited 30 fps monitor measurements above.

Measurements are short local runs, not sustained leak/stress or statistically repeated guarantees. CPU percentages use one logical processor as 100% (divide by 24 for total-machine percentage). Offscreen Qt timers, composition and WASAPI sample timing establish automated behavior; they do not establish visible Windows display cadence, perceived audio/video sync, HDR correctness, or real phone/4K footage. Synthetic 4K/10-bit SDR, VFR and rotation remain the approved coverage from Session 0. Large-project/slow-storage and more complex graphs remain coverage gaps.

Reproduce from PowerShell:

```powershell
./scripts/Build.ps1
./scripts/Test-Application.ps1
./scripts/Measure-Session10Playback.ps1
./scripts/Collect-Session10Evidence.ps1
```

## Accepted visible playback check

The agent verified import/cache, editing/undo, seeking, low-memory behavior, cancellation, relinking, output timing/metadata/content and CPU/GPU execution. The user confirmed the focused visible Windows playback check on 2026-10-07. The instructions below record the accepted check; no repeat testing is requested.

1. Launch `B:\Coding\02-Projects\Tools\Video-Editor\build\session-10\Release\VideoEditor.exe` and use **File → Open project…** to open `B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\session-10\4k-editing.veproject`.
2. Leave **Preview: 720p**, **Standard memory**, and **CPU presentation** selected in the Sequence viewer, then press **Play**. Observe whether the 20-second picture plays smoothly while the window remains responsive. Brief loading at a cut is a known pipeline limitation; report any distracting sustained stutter or frozen controls.

The user's confirmation establishes acceptance of this visible editing profile. No additional individual measurements, display-scaling checks, real 4K/phone/HDR tests or audible-sync results were reported.

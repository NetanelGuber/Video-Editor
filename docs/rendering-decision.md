# Session 5 rendering decision and benchmark

For the current measured 4K editing profile, preview quality/memory controls, aid caching and the decision to defer optional proxies, see [Session 10](session-10.md). The numbers below remain the historical Session 5 baseline.

2026-10-06; Windows 11 target PC, i7-13700F (24 logical processors), RX 9070, AMD driver 32.0.31041.3013. Dependency pins are unchanged. Raw measurements are in [benchmark summary](../evidence/session-5/benchmark/summary.json), [playback checks](../evidence/session-5/playback-result.json), and [encoder capabilities](../evidence/session-5/encoders/capabilities.json).

## Decision

Keep **CPU raster presentation** as the default, with the measured **D3D11VA decode option** and automatic CPU retry. CPU decode remains the initial monitor setting. D3D11VA is the preferred candidate for 144 fps HEVC on this PC: it substantially reduced CPU work and seek latency in this run. OpenGL image upload/QPainter presentation works on the actual RX 9070, but costs more time/memory than raster painting with this already-converted image path. Retain it as an optional comparison path; defer zero-copy D3D/GPU conversion work until a measured need warrants it. Neither project correctness nor opening a file requires GPU support.

Use **WASAPI shared output** and its queued-PCM sample clock. The native test obtained the real default output endpoint and reported zero underruns over the pause/resume test. The user accepted Session 5 after the checklist was provided; individual audible-output and perceived-sync outcomes were not separately reported. Qt Multimedia was not installed or compared: the pinned Qt base package supports the selected UI/presentation modules, and WASAPI avoids another package.

## Measurement method and limits

`PlaybackBenchmark` runs the production decode worker with a paced monotonic clock and CPU QImage/QPainter or a native OpenGL offscreen framebuffer. It opens no visible editor window. Preview images are capped at 1280×720; original 4K sources are decoded at source resolution. Each GPU paint completes with `glFinish`, which measures more than submission time and may cost more than asynchronous visible presentation. GPU pixels match the CPU reference in all sixteen comparisons (mean absolute difference 0/255). This validates the FBO operation, not the actual QOpenGLWidget/display compositor.

The two real sources run for five seconds per configuration; synthetic sources run for 1.8 seconds of their two-second duration. Seeking uses 1.5 seconds for real sources and one second for synthetic sources, after a new decoder is opened. Startup/seek values are request-to-first-displayable-frame including decoder/device creation and keyframe preroll, excluding file inspection before opening the monitor. A separate CPU run covers twenty seconds. This is a short feasibility baseline, not a repeatability study or final editing performance guarantee.

CPU percentages use **one logical processor = 100%**, so divide by 24 for total machine capacity. Working set includes the process, codec reference frames, conversion, and presentation resources. GPU counters are per-process, per-engine Windows performance samples; video-engine peaks are not total GPU percentage. Some short runs have no valid matching sample. Disappearing wildcard instances can cause warnings; raw counters/notes are retained. Counter collection and background load can affect short paced runs. These measurements do not establish visible interaction, display refresh, speaker latency calibration, real 4K/phone, or HDR behavior.

## Decode comparison with raster presentation

| Clip | Decode | Startup / seek ms | Presented / dropped | CPU, one-core % | Working set MiB |
|---|---|---:|---:|---:|---:|
| Acu, 1080p HEVC 144 fps, 5 s | CPU | 7 / 610 | 553 / 167 | 122.6 | 85.6 |
| Acu, same | D3D11VA | 62 / 233 | 720 / 0 | 41.4 | 104.5 |
| Butiti II, 1080p H.264 144 fps, 5 s | CPU | 12 / 281 | 720 / 0 | 68.6 | 100.7 |
| Butiti II, same | D3D11VA | 61 / 204 | 720 / 0 | 47.2 | 110.4 |
| Synthetic 4K H.264 30 fps, 1.8 s | CPU | 24 / 99 | 54 / 0 | 36.8 | 63.0 |
| Synthetic 4K H.264, same | D3D11VA | 76 / 127 | 54 / 0 | 31.6 | 82.2 |
| Synthetic 4K HEVC 10-bit SDR 30 fps, 1.8 s | CPU | 63 / 252 | 54 / 0 | 65.4 | 63.3 |
| Synthetic 4K HEVC 10-bit, same | D3D11VA | 71 / 117 | 54 / 0 | 34.2 | 93.9 |

The Acu CPU short run dropped frames; a separate twenty-second CPU run presented all 2880 expected source frames with zero drops. CPU pacing varies with workload/system conditions. The longer run reached about 81.8 MiB by 0.5 seconds and stayed about 82.1 MiB at 19.5/20 seconds, rather than accumulating frames. Its final working set after framebuffer inspection was about 85.6 MiB. Queue high-water was six in every benchmark. These data support bounded memory, not an unlimited-duration leak claim.

Actual D3D11VA operations passed start/middle/end tests for all six real and four synthetic Session 0 sources, including rotation/VFR. The injected unavailable-acceleration test returned a CPU frame and retained the reason. All selected audio streams decoded/resampled, including multiple-stream files; details are in [Session 5](session-5.md).

## Presentation comparison

| Source / decode | Raster mean paint µs | OpenGL mean paint µs | Raster / OpenGL working set MiB |
|---|---:|---:|---:|
| Acu / D3D11VA | 399 | 1002 | 104.5 / 169.0 |
| Butiti II / D3D11VA | 388 | 918 | 110.4 / 174.8 |
| 4K H.264 / D3D11VA | 563 | 1292 | 82.2 / 140.1 |
| 4K HEVC10 / D3D11VA | 543 | 1282 | 93.9 / 152.6 |

OpenGL reports `AMD Radeon RX 9070` and matches the CPU image. D3D11VA video-engine peaks were approximately 25.9% for Acu/raster and 22.2% for Butiti/raster; combined D3D11VA/OpenGL peaks were approximately 26.7%, 25.9%, 18.6%, and 16.1% for the four sources. GL-only samples showed roughly 0.1–0.6% 3D-engine use. These are sparse engine samples, not precise averages.

## Actual encoder checks

Each check encoded one second at 30 fps, then independently probed and CPU-decoded the resulting MP4. Session 8 now implements CPU timeline export; separate-player human validation remains pending. See [Session 8](session-8.md).

| Encoder | Input/output test | Result | Elapsed ms, including setup |
|---|---|---|---:|
| libx264 | Real 1080p H.264 → H.264 8-bit | Passed | 510 |
| h264_amf | Real 1080p → AMD H.264 8-bit | Passed | 393 |
| hevc_amf | Real 1080p → AMD HEVC 8-bit | Passed | 327 |
| av1_amf | Real 1080p → AMD AV1 8-bit | Passed | 324 |
| hevc_amf / p010le | Synthetic 4K HEVC10 → AMD HEVC10 | Passed | 798 |

Outputs/logs are disposable evidence; sources are untouched. These small checks do not settle export quality, bitrate presets, AV sync, or packaging/licensing.

## Gates carried forward

The [Session 5 checklist](session-5.md#human-windows-checklist) establishes visible play/pause/seek, audible sync, interaction during playback, and QOpenGLWidget behavior. [Session 6](session-6.md) adds live timeline controls and basic title placement/text preview to the same description; edits pause and re-seek at the retained playhead. Session 7 expands title styling and basic audio controls; Session 8 reuses the same composition and mixing for CPU H.264/AAC export, including full-resolution title rasterization and independent frame/audio comparisons. Session 10 measures longer edited/4K sequences, caching/proxy needs, and GPU conversion/zero-copy improvements. The Session 5 benchmark above is the earlier playback baseline, not an edited/title-sequence performance measurement.

## Session 11 compositor extension

[Session 11](session-11.md) supersedes the prototype's single-winning-video and unevaluated-effect limits. Preview/export now decode all active video layers (at most eight) and share ordered raster composition of video/title clips with transform, crop, opacity, blend modes, content-relative hold/linear/eased keyframes and independent opacity/solid-color edge fades. CPU/D3D11VA source decoding and WASAPI mixing remain in place. Preview queues share their byte budget with a per-layer floor; export retains bounded queues per layer. Analytical colors/alpha, production offscreen playback and independently decoded reference MP4s verify the new boundary. This does not extend Session 10's measured real-time 4K promise to eight simultaneous 4K layers, HDR or sustained large effects graphs.


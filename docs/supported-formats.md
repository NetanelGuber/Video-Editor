# Formats, performance and compatibility limits

The package uses Qt 6.11.3 and Gyan FFmpeg 9.0.2 full shared. `build/ffmpeg-runtime.json` records encoders, muxers, pixel formats, linked library versions and configuration from the exact shipped DLLs. `build/ffmpeg-*.txt` adds decoder/filter/hardware and supplier-tool inventories. These are compiled capabilities; the export dialog also probes the current machine and exact settings.

| Workflow | Verified scope | Limits |
|---|---|---|
| Real source media | 30 target-PC MP4/MKV files, 1080p 8-bit H.264/HEVC, mostly 144 fps; AAC/FLAC/ALAC and multiple audio streams | Does not establish every possible combination in FFmpeg |
| Additional source coverage | Synthetic 4K, 10-bit SDR, variable frame rate, rotation and flash/beep timing references | Real 4K phone/HDR sources remain unverified |
| Simple export | H.264/AAC MP4, software x264; profile-dependent HEVC/software x265 choices; sizes fit without upscaling | Broad compatibility target imposes stricter size/FPS limits; device names are policy targets, not physical-device certification |
| Advanced export | Named encoders, compatible containers and convertible pixel storage; rational FPS, native quality/options and audio bitrate; independently inspected H.264/HEVC/AV1 and representative audio/container outputs in session evidence | Availability depends on bundled descriptors, exact probe and driver; custom combinations are not universally supported |
| Preview/export pixels | Shared layered SDR composition, titles/effects and frame timing; independent decoded export checks | Internal composition is 8-bit BT.709 SDR. 10-bit storage does not restore lost precision or produce HDR |
| Audio | 48 kHz stereo mix, gain/pan/fades/automation, buses, pitch-preserving speed changes | Hardware endpoint and subjective AV sync are separate from sample-exact tests |
| Target PC | Windows 11 x64, i7-13700F, RX 9070, 32 GB RAM; measured D3D11VA/OpenGL and AMD encoders in earlier sessions | Other GPUs/drivers/Windows versions and long-running stability need their own measurements |

Use **Simple → Desktop MP4** first for a normal H.264/AAC export. The exact automatic FPS can be the primary source's 144 fps; choose a compatibility target or manually set Advanced FPS if the player cannot handle it. A successful encode/decode proves the tested file, not playback on every phone, television, browser or editing application.

Hardware cache entries always require a new exact hardware probe. The environment fingerprint includes FFmpeg and GPU/driver identities. Unknown, corrupt, stale or mismatched entries fall back to detection; **Refresh encoder capabilities** bypasses saved results. Cached software settings can be reused only while their fingerprint matches.

Original-media 4K work was measured against the documented target-PC editing profile. Proxy generation was deferred because that measured profile met its gate. More layers, expensive masks/effects, speed processing or different footage can still be slow. Maximum-layer 4K memory usage has no sub-2-GiB guarantee. Keep enough disk space for source media, destination files, temporary exports, caches and backups; cancellation preserves the last completed destination.

The ZIP has app-local Qt/FFmpeg/MSVC libraries. Windows provides UCRT, system DLLs and graphics/audio drivers. Qt's optional Direct3D 12 shader compiler and software OpenGL renderer are omitted: this editor uses QPainter/OpenGL presentation with CPU fallback and D3D11VA decoding. The offscreen plugin is retained for repeatable automated verification. No Qt Test DLL, test executable, FFmpeg command-line tool or developer SDK is part of the release.

The package is unsigned and has no installer wizard, automatic update service, file associations or shortcuts. Portable installation means extracting the whole directory. It has been verified in a fresh extraction with isolated settings/cache/recovery data and a restricted PATH on the development PC; a separate Windows account, clean OS/VM and other computers have not been verified. Native Windows appearance/scaling and physical-device playback remain explicitly separate evidence gaps.

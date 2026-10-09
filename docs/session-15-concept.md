# Session 15 concept — complete before implementation

Completed 2026-10-08. User priority: desktop playback. Scope: capability discovery and compatible export profiles in the existing native dialog. Workspace reorganization and Simple/Advanced tabs remain Sessions 15.1/15.2.

## User behavior and targets

The export dialog adds a playback target and an encoder choice, plus a readable capability/conflict status. Existing dimensions, exact rational frame rate, quality, encoding speed and audio controls remain. All changes validate immediately; a background check tests the exact proposed encoding configuration. Export stays disabled until that check succeeds. A capability details button explains compiled support separately from actual device/driver and short-encode results. Refresh retries discovery after hardware/driver changes.

| Target | Container / codecs | Bounds and assumptions |
| --- | --- | --- |
| Desktop MP4 (default; existing compatible-mp4 ID) | MP4, H.264 High, AAC-LC optional | Even 2–4096 dimensions, up to 240 fps; 8-bit SDR BT.709, 48 kHz stereo. High rates/sizes require a capable player. |
| Desktop Matroska | MKV, H.264 High, AAC-LC optional | Same bounds. Intended for players with Matroska and H.264 support. |
| Broad MP4 | MP4, H.264 High Level 4.1, AAC-LC optional | At most 1920×1080 or portrait 1080×1920, 30 fps, bounded bitrate; software encoding to keep profile/level guarantees consistent. |
| HEVC MP4 | MP4, HEVC Main with hvc1 tag, AAC-LC optional | At most 4096×2160 or portrait equivalent, 60 fps; requires HEVC-capable playback. |

These are format targets, not certified TV/phone/player compatibility. No HDR, 10-bit, AV1, VP9, WebM, arbitrary codecs, or new audio format is promised in this slice. Representative samples cover all profiles, software and every usable exposed hardware encoder, fractional rate, video-only, 1080p and synthetic 4K. Independent FFprobe inspection and FFmpeg decoding establish container/codec/dimensions/rate/audio properties, not physical device playback.

## Capability model and implementation boundary

1. Enumerate the loaded libraries with av_codec_iterate, av_muxer_iterate, av_pix_fmt_desc_next, av_hwdevice_iterate_types, and avcodec_get_supported_config. Record library version/configuration/license, encoder pixel formats and muxer codec compatibility (avformat_query_codec). Inventory is broader than the small supported/export-tested matrix; enumeration alone never enables a choice.
2. Only adapter-backed H.264/HEVC encoders are offered: libx264/libx265, AMD AMF, NVIDIA NVENC and Intel QSV. Software receives yuv420p; hardware receives nv12. The encoder must advertise that pixel format, and the container must accept both codec IDs. AAC must support planar float, stereo and 48 kHz when enabled.
3. Use a separate local helper process linked to the same bundled libraries to open the exact encoder configuration, encode synthetic frames/audio and finish a memory-backed container. A short deadline and cancellation isolate driver stalls/crashes. Enumerated hardware device types are separately tested with av_hwdevice_ctx_create, with individual deadlines. Device creation and encoder usability remain separate facts: AMF/NVENC can manage their own device.
4. Discovery runs outside the GUI thread, with debounce/cancellation and generation ownership so stale results cannot validate edited controls. Export rechecks at job start, before opening the atomic output. No permanent capability cache or device assumptions are serialized.
5. Share the encoder configuration function between probes and exports. Software uses documented preset/crf/profile controls. Hardware maps the existing five speed choices onto verified per-encoder settings and uses bounded constant QP. The quality number has encoder-dependent behavior; explain that it is not a cross-encoder quality guarantee. Unknown/rejected options fail visibly.

## Fallbacks, conflicts and persistence

Save profile ID, requested encoder policy and pixel format intent in schema 7. Migrate schemas 1–6 through the existing pipeline, defaulting legacy exports to desktop MP4/software libx264, preserving their other values. Unknown choices remain loadable/editable and appear explicitly in the dialog, with export blocked; they are never silently replaced.

Automatic encoder policy tries compatible AMD/NVIDIA/Intel hardware, then a usable software encoder. Display the actual choice and software-fallback reason before export, and record both in the result. Explicit hardware selection fails with its driver/probe reason and the instruction to choose Automatic or Software; it never silently switches. Default Software favors reproducibility. Refresh or another computer rediscovers everything. A late device failure stops publication and advises retrying with software; it does not publish partial files or hide a codec change.

Changing a profile sets its fixed container/codec tuple and a suitable filename extension, while retaining dimensions/rate and warning if they exceed its limits. Saved conflicting tuple/extension/dimensions/pixel/encoder settings must be shown as conflicts on opening. The warning names both conflicting settings, explains the reason and the fix. Disabled unavailable rows retain tooltip explanations; the visible status explains a selected unavailable row. Empty/missing paths, missing media, source overwrite, unsupported effects and all existing worker safety checks remain enforced.

## Acceptance checks

Build and run all existing suites plus a focused capability suite. Test compiled versus usable inventory, timeout/crash/cancellation, no-GPU/software-only simulated resolution, explicit unsupported encoder and pixel/muxer/codec conflicts, immediate production-dialog warnings and disabled Export, edit-during-discovery, cross-computer stale selections, all schema migrations, exact persistence/undo, and independently inspect/decode representative outputs. Recheck the real target PC's hardware; never reuse Session 5 availability as current evidence. Retain source size/mtime verification. No computer use; human checks only for indispensable physical playback evidence, and no physical compatibility claims without it.

## Official sources consulted for the concept

- https://ffmpeg.org/doxygen/trunk/group__lavc__core.html — codec iteration and supported configuration API.
- https://ffmpeg.org/doxygen/trunk/group__lavc__encdec.html — send/receive and flushing.
- https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html — muxer compatibility, headers, interleaving and trailer.
- https://ffmpeg.org/doxygen/trunk/hwcontext_8h.html — device enumeration/creation, not an encoder-availability guarantee.
- https://ffmpeg.org/doxygen/trunk/pixdesc_8h.html — pixel-format descriptors.
- https://ffmpeg.org/ffmpeg-codecs.html — x264/x265/AAC and hardware encoder controls.
- https://ffmpeg.org/ffmpeg-formats.html — MP4/Matroska constraints.
- https://ffmpeg.org/legal.html — GPL components and distribution obligations; packaging/licensing review remains Session 16.

Bundled FFmpeg 9.0.2 CLI inventory consulted before implementation: libx264, libx265, AAC and H.264/HEVC AMF/NVENC/QSV are compiled; MP4 and Matroska muxers exist. AMF advertises nv12 and cqp/qp_i/qp_p/quality; NVENC and QSV option help was inspected. Installed headers and runtime AVOption/config queries will verify each adapter during implementation. Online trunk docs may differ from the pinned release; the installed headers and actual short encode are decisive.

# Encoder-specific export concept — complete before changes

2026-10-08. This extends Session 15 following the user's clarification: every bundled video encoder must be selectable, with settings that follow its actual controls. The former shared five speed labels and shared quality scale are insufficient.

## Scope and user behavior

Retain the four compatibility targets and add Custom encoder/container. List every compiled video encoder by its exact FFmpeg name and codec. Selecting a specific encoder switches to Custom, selects a muxer which accepts its codec, a convertible advertised pixel format and compatible audio where possible, and installs that encoder's own default controls. No codec is silently omitted. An encoder which fails the exact local short encode remains visible, with a reason and export blocked. Software/Automatic remain policies for the fixed targets.

Speed/quality controls come from per-encoder AVOptions and explicit semantics for common families. x264/x265 expose their real preset names and CRF scale; AMF exposes its own quality/speed constants and codec-specific QP range; NVENC exposes its native presets and QP; QSV uses its own presets and rate-control semantics; AV1/VP9 encoders expose their own speed/quality scales. A codec without a speed or quality control shows Encoder default and disables the irrelevant field. Enumerated option names/ranges/defaults never imply actual hardware support; the exact configuration probe is decisive. No CPU-label mapping substitutes for native choices.

A native Encoder options table exposes writable video encoding AVOptions, including constants, booleans, numeric/string/flag parameters and a bounded set of common codec-context controls. Empty values use FFmpeg defaults. Invalid strings/ranges/combinations receive immediate validation, followed by exact probing for driver-dependent restrictions. Changing an encoder resets its primary controls/private options explicitly and reports that defaults were applied; saved incompatible values remain visible and blocked until changed. The primary quality/speed keys are owned by their controls and cannot conflict silently with table overrides.

Container and pixel choices derive from the loaded build. Query codec compatibility and verify the entire output using a disposable helper; unknown muxer compatibility is resolved by actual header/packet/trailer testing. Host pixels are converted using swscale; hardware-only input uses av_hwdevice/frames contexts and frame upload where the runtime supports it. Palette-only encoders receive a defined RGB palette. Unavailable device/context/conversion, codec initialization, special stream constraints or missing external libraries explain why that choice cannot run. A compiled codec can therefore be selectable but unavailable for the current machine/settings.

Optional audio remains the shared 48 kHz stereo mix. Custom output chooses a compatible supported audio encoder (AAC, Opus, FLAC, MP3, AC-3 or PCM), adapts its sample format, and blocks unsupported audio/container combinations. Audio-off remains a valid way to export video-only formats. Persist encoder-specific controls/options/audio choice in schema 8; schemas 1–7 migrate deliberately. Legacy hardware speed intent is converted to the native value formerly used, preserving actual behavior. Capability evidence is rediscovered on every machine.

## Acceptance

Verify that every bundled video encoder appears in production UI/model inventory. Enumerate and validate option descriptors for all of them; perform bounded local probes and classify every encoder as usable or unavailable with a reason. Exercise software families, all usable hardware families, lossless/intraframe/palette/image codecs, container/audio/pixel adaptation, different quality scales, native speed constants, disabled irrelevant controls, invalid/stale settings, cancellation and persistence/undo. Independently inspect/decode representative outputs; run full regression. Do not claim that enumerating or probing a codec proves all its possible settings, hardware configurations or physical playback devices.

## Official sources checked

- https://ffmpeg.org/doxygen/trunk/group__avoptions.html — enumerate defaults, types, ranges, constants and setters.
- https://ffmpeg.org/doxygen/trunk/group__lavc__core.html — encoder iteration, supported pixel/sample configurations and hardware configurations.
- https://ffmpeg.org/ffmpeg-codecs.html — codec-specific controls and rate-control behavior.
- https://ffmpeg.org/doxygen/trunk/hwcontext_8h.html — hardware contexts and frame transfer.
- https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html — actual container validation.
- https://ffmpeg.org/legal.html — existing licensing/distribution boundaries.

The bundled 9.0.2 headers and option queries take precedence over online trunk descriptions. Its HEVC AMF option help includes speed, balanced, quality and a newer high_quality constant; actual driver acceptance, rather than assuming exactly three entries, decides whether the extra choice can be used.

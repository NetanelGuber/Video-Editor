# Dependency and license inventory

These pins describe the personal development environment selected in Session 0. [dependencies.lock.json](../dependencies.lock.json) is the machine-readable source for versions, archive hashes, and local paths. Session 16 adds a personal local portable package with exact runtime origins/hashes and supplied notices. [The release review](release-license-review.md) records external distribution blockers; no application distribution license is assigned.

| Component | Build | License / evidence |
|---|---|---|
| Qt Core/Gui/Widgets | Qt base 6.11.3, official MSVC 2022 x64 shared binaries | LGPLv3/GPL/commercial alternatives as recorded per module in the supplied SBOM; tools and third-party components have their own entries |
| FFmpeg | Gyan 9.0.2 full shared, Windows x64, GCC-built DLLs with MSVC import libraries | Binary reports GPLv3-or-later; `--enable-gpl --enable-version3 --enable-shared`, no `--enable-nonfree` |
| MSVC / Windows SDK | Compiler 19.51.36244, toolset 14.51.36231 / SDK 10.0.26100.0 | Existing Microsoft installations and terms; not copied into the project |
| CMake / Ninja | 4.2.3-msvc3 / 1.13.2, bundled with Visual Studio | BSD-3-Clause / Apache-2.0 upstream; keep bundled notices if tools are ever redistributed |

Upstream tool license references: [CMake licensing](https://cmake.org/licensing/) and [Ninja 1.13.2 COPYING](https://github.com/ninja-build/ninja/blob/v1.13.2/COPYING).

Qt was selected for native Widgets on Windows 11. Only qtbase is downloaded. Its supplied build summary, options, and [SPDX SBOM](../evidence/session-0/dependencies/qtbase-sbom.spdx.json) are retained; do not infer one license for every Qt tool or bundled library from the top-level package. [Qt licensing documentation](https://doc.qt.io/qt-6.11/licensing.html).

FFmpeg's shared development package provides the required C library interface and CPU codecs for the observed footage. This GPL-enabled selection includes x264/x265 and is suitable for the initial personal development scope. Dynamic linking does not remove its GPL status. Before sharing the app, decide the application's licensing and review the exact bundled components, source availability, notices, and codec obligations. If a future distribution requires an LGPL-only media dependency, select a different reviewed FFmpeg configuration and revalidate codec/encoder choices. [FFmpeg license/configuration guidance](https://ffmpeg.org/legal.html).

The Windows binary supplier publishes full/shared builds with development files and GPLv3 licensing. [Supplier build documentation](https://www.gyan.dev/ffmpeg/builds/) and [pinned 9.0.2 release](https://github.com/GyanD/codexffmpeg/releases/tag/9.0.2).

## Exact FFmpeg component evidence

The evidence directory retains the actual binary's inventory rather than a guessed list from the download name:

- [Version and library ABI numbers](../evidence/session-0/dependencies/ffmpeg-version.txt)
- [Complete configure flags](../evidence/session-0/dependencies/ffmpeg-buildconf.txt)
- [Decoders](../evidence/session-0/dependencies/ffmpeg-decoders.txt), [encoders](../evidence/session-0/dependencies/ffmpeg-encoders.txt), [formats](../evidence/session-0/dependencies/ffmpeg-formats.txt), [filters](../evidence/session-0/dependencies/ffmpeg-filters.txt)
- [Compiled hardware interfaces](../evidence/session-0/dependencies/ffmpeg-hwaccels.txt)
- [Binary license declaration](../evidence/session-0/dependencies/ffmpeg-license.txt), [supplier README and external library versions](../evidence/session-0/dependencies/ffmpeg-package-readme.txt), [supplied license text](../evidence/session-0/dependencies/ffmpeg-COPYING.txt)

Relevant enabled external components include x264/x265, libass/freetype/harfbuzz/fribidi for text, zimg for image conversion, soxr for resampling, and AMF/D3D11VA/D3D12VA/DXVA2 hardware interfaces. Built-in H.264/HEVC/AAC/FLAC/ALAC decoder lookups and sample CPU decoding passed. Compiled hardware interfaces are not evidence of successful RX 9070 acceleration.

Qt build metadata: [summary](../evidence/session-0/dependencies/qtbase-build-summary.txt), [options](../evidence/session-0/dependencies/qtbase-build-options.txt), [archive identity](../evidence/session-0/dependencies/qt-archive.json). The actual newer MSVC-to-Qt/FFmpeg link and runtime check is retained in [toolchain-check.txt](../evidence/session-0/dependencies/toolchain-check.txt).

## Session 1 application build

`VideoEditor.exe` directly imports Qt6Core, Qt6Gui, Qt6Widgets, and FFmpeg `avutil-61.dll`; FFmpeg is accessed through the app-owned `editor::media` interface for runtime identity only. Media probing/decoding is deferred. The source/build contains no Qt WebEngine, WebView, browser shell, HTML, or QML UI dependency.

The local build deploys Qt's Windows platform and modern Windows style plugins, image-format plugins selected by `windeployqt`, the offscreen platform for automated verification, and its selected DX compiler support DLLs. Unneeded generic touch/network/TLS plugin types are excluded. Qt's supplied SPDX inventory remains the source for individual bundled third-party component notices; Qt's top-level license is not a blanket license for them. MSVC/UCRT and Windows system runtime DLLs are prerequisites on this development PC and are not copied by the build.

No application distribution license or installer is established in Session 1. The existing GPL-enabled FFmpeg selection remains subject to the Session 0 distribution decision, even though the shell currently uses only avutil. Session 15 must inventory the exact package, include the appropriate component notices/license texts and source/relinking obligations, and validate runtime provisioning before distribution. [Session 1 runtime import/file evidence](../evidence/session-1/runtime-inventory.json) records the development output rather than claiming it is a finished package.

## Session 3 media build

The app-owned media library now links `avformat`, `avcodec`, `avutil`, and `swscale` for local inspection, CPU decode, and thumbnail conversion. The local build deploys `avformat-63.dll`, `avcodec-63.dll`, `avutil-61.dll`, `swscale-10.dll`, and the transitive `swresample-7.dll`, all from the existing pinned shared package. Qt Core/Gui/Widgets remain the only Qt application modules. No dependency downloads/version changes, browser UI, or hardware-acceleration promises are added. The supplier's existing GPL-enabled build and distribution-review policy still apply.

## Session 5 playback build

Playback also links `swresample` directly, plus Qt OpenGL/OpenGLWidgets from the same pinned Qt base archive. Their supplied SBOM/module licenses apply. Windows WASAPI/COM use system interfaces (`ole32`/`uuid`); the benchmark additionally uses Windows process-memory/timer APIs (`psapi`/`winmm`). No Qt Multimedia package or new dependency pin/download is added. The runtime deploys Qt6OpenGL/Qt6OpenGLWidgets beside the existing Qt/FFmpeg libraries. Actual RX 9070 decode/presentation/encoder evidence is recorded in [Session 5](session-5.md) and [rendering decision](rendering-decision.md), alongside the user's Session 5 acceptance.

## Session 6 timeline interface tests

`editor_timeline_ui` uses the existing Qt Widgets modules. The offscreen `TimelineUiTests` executable additionally links Qt Test from the same pinned qtbase archive, with `Qt6Test.dll` copied beside the test executable. The application does not link Qt Test. No new package/version, global setting, shell profile or browser UI is introduced. Basic title preview uses Qt Gui's QPainter/font rasterizer inside the shared rendering boundary.

Session 12 also links/deploys `avfilter.lib` / `avfilter-12.dll` from the same pinned FFmpeg shared package for streaming pitch-preserving `atempo` and `rubberband` filters. Rubber Band is already enabled inside the pinned FFmpeg binary; no separate runtime package is added. There is no new download, global installation or dependency version change. Existing FFmpeg license/redistribution constraints still apply.


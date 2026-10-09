# Prepared request: FFmpeg 9.0.2 full shared corresponding source

This is a draft for the supplier, not a sent message or a source offer.

We are preparing a GPL-3.0-or-later Windows video editor that directly links
six DLLs from your `ffmpeg-9.0.2-full_build-shared.7z` release. We want to provide
the matching source, notices and build materials with our binary distribution.

Our downloaded archive SHA256 is
`4d2060a8b34a940aa47d785142055bb92a63053781e55f2ace4546edd519a8f5`.
Your release points to FFmpeg revision `946fcce07b`, which resolves to
`946fcce07b6dcd0331c8cc609192aeff5e1924f8` in the official repository.
We have recovered that source and your configure/version inventories.

The shipped DLLs are:

- avcodec-63.dll
- avfilter-12.dll
- avformat-63.dll
- avutil-61.dll
- swresample-7.dll
- swscale-10.dll

We do not ship avdevice, ffmpeg, ffprobe or ffplay. Could you provide existing
public source/build locations, or a bundle, covering the following inputs for
these DLLs?

1. The build scripts/toolchain package inventory, configure environment,
   compiler/linker flags, pkg-config metadata and patches used for 9.0.2 full
   shared. The release reports GCC 16.2.0, MSYS2 Rev3.
2. Exact source revisions/archives, patches and copyright/license notices for
   the incorporated external libraries and their static transitive dependencies.
3. Missing version entries for bzlib, fontconfig, gmp, gnutls, iconv, libmysofa,
   libxml2, lzma, OpenCL-Headers and zlib. Please also identify transitive inputs
   such as the relevant crypto, font, graphics/shader and C++ runtime libraries.
4. The source repository/revision for the SVT-AV1
   `v4.2.0-cqp-extended-52-g0c1c4deca` build and any other supplier-specific forks
   or changes beyond the upstream versions listed in the README.
5. Link maps or equivalent dependency information sufficient to distinguish
   libraries incorporated into these six DLLs from device/CLI-only integrations.

We can use equivalent public source access with clear download directions;
the files do not have to be hosted by our application repository. We need to
identify the correct materials and preserve access/notices for our distribution.
We are not requesting a new custom binary build or access to unrelated tooling.

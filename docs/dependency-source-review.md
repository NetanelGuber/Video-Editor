# Dependency source review for 1.0.0

The 2026-10-09 review recovered the supplier-identified FFmpeg and Qt source
archives, verified their downloads, checked the shipped Qt code against the
supplier SPDX inventory, and collected the Qt and Microsoft runtime notices.
The existing FFmpeg binary build still lacks complete corresponding source.
This review was completed while the binary was a private GitHub draft; subsequent
publication is recorded in [release status](release-1.0.0.md).

## Exact source downloads

[dependency-sources.json](notices/dependency-sources.json) pins the download
URLs, full revisions, sizes and SHA256 values. Both archives were downloaded
and successfully extracted during this review:

| Component | Exact revision | Coverage |
|---|---|---|
| FFmpeg | `946fcce07b6dcd0331c8cc609192aeff5e1924f8` | FFmpeg source, configure and Makefiles; external library build inputs are not included |
| Qt base | `5a1194b2d368e2d72c230205c464b04a2f23eccd` | Qt base source, bundled third-party source and notice files |

Download and verify the archives without changing the project dependencies:

```powershell
$sources = @(./scripts/Get-DependencySources.ps1)
./scripts/Review-DependencySources.ps1 `
  -QtSourceDirectory ($sources | Where-Object component -eq 'Qt base').sourceDirectory `
  -FFmpegSourceDirectory ($sources | Where-Object component -eq 'FFmpeg').sourceDirectory
```

`-Offline` verifies previously downloaded archives. Each run extracts into a
new directory, preserving any edited source trees. Archives are kept under
`.tools/source-releases` and excluded from Git. These two archives must not be
described as the complete source bundle for the current FFmpeg binaries.

GPLv3 section 6(d) permits equivalent source access through another server when
the binary download provides clear directions to it. Self-hosting every archive
is not mandatory. We must still establish the correct source/build materials,
include required notices, and keep source access available for the distribution.
The archive URLs alone do not settle the missing supplier inputs.

## Qt identity and notices

All eleven shipped Qt DLLs/plugins are unchanged from their recorded package
origins and have valid Qt Company signatures. The supplier's SHA1 inventory
describes the unsigned PE files. Removing certificate data and resetting the PE
checksum/security-directory fields **in memory** reproduces all eleven supplier
hashes. The shipped files are never modified. Independently pinned binary
archive and payload SHA256 values remain the identity checks for deployment.

Following the selected files' `CONTAINS` and `DEPENDS_ON` SPDX relationships
produced 59 dependency records. Forty-three attribution records were located
in the exact source tree, including records whose supplier license field says
`NOASSERTION`. Their source attribution files supply the license/copyright
information; `NOASSERTION` alone is not an unresolved licensing finding.
The WrapAtomic dependency is a CMake interface to compiler-provided atomics;
its source implementation is `cmake/FindWrapAtomic.cmake` in the same archive.

[qt-runtime-source-coverage.json](notices/qt-runtime-source-coverage.json)
retains the selected source attributions.
[qt-runtime-attributions.txt](notices/qt-runtime-attributions.txt) includes their
copyright information and 32 distinct supplied notice/license files. The full
original supplier SBOM and existing full attribution inventory remain retained.
[The verification record](../evidence/source-review-1.0.0/dependency-source-review.json)
records the binary checks; this is an automated source/identity review, not a
fresh Qt rebuild.

For a local replacement Qt build, use the exact source archive with an x64 MSVC
developer environment and CMake/Ninja. A suitable qtbase configuration for the
shipped modules/plugins is:

```powershell
& (Join-Path ($sources | Where-Object component -eq 'Qt base').sourceDirectory 'configure.bat') `
  -prefix C:/Qt/rebuilt-6.11.3 -release -opensource -confirm-license `
  -nomake examples -nomake tests -qt-zlib -qt-pcre -qt-freetype `
  -qt-harfbuzz -qt-libpng -qt-libjpeg
cmake --build . --parallel
cmake --install .
```

Run this in a separate build directory using the source tree's configure.bat.
This is a proposed replacement configuration, not a tested rebuild or the
supplier's byte-for-byte recipe. The supplier configuration and summary are
retained in `evidence/session-0/dependencies/qtbase-build-options.txt` and
`qtbase-build-summary.txt`. They include additional unshipped tools/database
modules and supplier-local paths. Use the retained records to review differences.

To relink the application, set CMake's `CMAKE_PREFIX_PATH` to the replacement Qt
installation and the FFmpeg development prefix, then configure/build the normal
project. `scripts/Build.ps1` shows the application's compiler/CMake environment.
For compatible shared Qt builds, replace the five Qt module DLLs and the matching
plugin DLLs together in an extracted package, preserving `qt.conf`. The package
has no signature enforcement or lock preventing replacement; users may modify
and rebuild/relink it. Do not overwrite the original package during experiments.

## Microsoft runtime review

All ten shipped CRT DLLs are unchanged from the non-debug `VC/Redist` directory
and have valid Microsoft signatures. `vswhere` identifies the installed compiler
as stable `VisualStudio.18.Release`, with `isPrerelease=false`. Microsoft's
[2026 distributable list](https://learn.microsoft.com/en-us/visualstudio/releases/2026/redistribution)
lists unmodified files under that directory, excluding debug_nonredist and
preview components, subject to the developer's Visual Studio license terms.

The current official [Community terms](https://visualstudio.microsoft.com/license-terms/vs2026-ga-community/)
and [runtime terms](https://visualstudio.microsoft.com/license-terms/vs2026-ga-visualcpp-v14-redist-runtime/)
were retrieved. The exact runtime document and a readable text extraction are
retained under `docs/notices/Microsoft-VC14-Runtime.*` for the package. These
terms apply to Microsoft's files; the application's GPL does not relicense them.
The developer's distribution conditions and the runtime recipient terms remain
applicable. This review checks files and published terms, not legal entitlement
on behalf of other redistributors.

## Remaining FFmpeg inputs

[ffmpeg-external-review.json](notices/ffmpeg-external-review.json) now derives
from the actual runtime configuration, including cairo, fontconfig and lcms2.
It records supplier version labels without presenting them as verified complete
source/build coverage. Ten entries lack supplier versions: bzlib, fontconfig,
gmp, gnutls, iconv, libmysofa, libxml2, lzma, OpenCL headers and zlib. Static
transitive dependencies, patches and build scripts also remain unidentified.

libcaca, libcdio and OpenAL have direct integrations in the unshipped avdevice
library; SDL is used by the unshipped ffplay program. They are not automatically
counted as direct integrations in our six DLLs. Supplier link inputs are still
needed to rule out transitive inclusion. Hardware/compiler feature flags are
identified separately rather than treated as standalone GPL libraries.

Gyan's 9.0.2 release identifies the FFmpeg revision and supplies six binary
archives. It does not attach a dependency-source bundle or build recipe. A
current checkout of Gyan's older autobuild-suite fork is not evidence of the
recipe used for these binaries.

Two concrete routes remain: obtain the supplier's exact missing inputs, or
replace FFmpeg with a controlled build whose source, notices and build steps
we retain. A replacement should preserve the documented editing formats,
software H.264/HEVC exports, AV1/audio coverage and applicable hardware paths;
it may have fewer optional encoders/filters than the current full build. That
requires fresh regression and extracted-package tests before publishing.

[ffmpeg-supplier-source-request.md](ffmpeg-supplier-source-request.md) is the
prepared request. It has not been sent. No unsupported source offer is made.
The package retains `externalDistributionApproved=false` while the remaining
source/build coverage is unresolved.

Primary references: [GPLv3 section 6](https://www.gnu.org/licenses/gpl-3.0.html#section6),
[Gyan build information](https://www.gyan.dev/ffmpeg/builds/),
[supplier release 9.0.2](https://github.com/GyanD/codexffmpeg/releases/tag/9.0.2),
[Qt licensing](https://doc.qt.io/qt-6/licensing.html), and
[building Qt on Windows](https://doc.qt.io/qt-6/windows-building.html).

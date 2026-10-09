# Binary release license and source review

Reviewed on 2026-10-09 against the shipped binaries, supplier inventories,
recovered source archives and published license terms. The application source
is **GPL-3.0-or-later**, selected by the author; see [COPYRIGHT.md](../COPYRIGHT.md).
The source repository is public. The portable binary remains a **private draft**
because the current FFmpeg supplier build still lacks complete dependency source
and build material. [The detailed source review](dependency-source-review.md)
records what was recovered and verified.

| Component | Verified evidence | Remaining work |
|---|---|---|
| FFmpeg 9.0.2 Gyan full shared | Runtime declares GPL v3 or later; GPL/version3/shared enabled and nonfree absent. Supplier FFmpeg commit expanded to `946fcce07b6dcd0331c8cc609192aeff5e1924f8`; its official source archive downloaded, hashed and extracted. | Supplier patches/build scripts and complete static/transitive dependency source and notices remain incomplete. Obtain them or replace the DLLs with a controlled build. |
| FFmpeg external integrations | Actual configuration covers 96 entries, including cairo, fontconfig and lcms2; supplier versions and integration scope recorded in `notices/ffmpeg-external-review.json`. Hardware/compiler features and device/CLI integrations are distinguished from direct shipped integrations. | Ten relevant entries lack supplier versions. Version labels and reachable upstream links do not establish complete patched source/build/notices. Do not infer that every enabled flag is a separately shipped GPL library. |
| Eleven Qt module/plugin DLLs | All match recorded origins and have valid Qt signatures. Reconstructed unsigned SHA1 values match every supplier SBOM file. Exact Qt source commit `5a1194b2d368e2d72c230205c464b04a2f23eccd` downloaded/hashed/extracted. Selected SPDX closure has 59 records; 43 source attributions and 32 supplied notice files recovered. | Include source download/build/replacement directions and collected notices in the regenerated package. A fresh Qt rebuild was not performed. |
| MSVC app-local CRT | Ten unmodified x64 DLLs from the pinned non-debug VC/Redist directory have valid Microsoft signatures. Installed VS is stable Release, not prerelease. Current official distributable list, Community terms and recipient runtime terms retrieved. | Preserve separate runtime terms. Distribution remains subject to the developer's license and published conditions; the application's GPL does not relicense Microsoft's files. |
| Windows/UCRT/graphics/audio | Runtime dependency/import inventory | Provided by Windows and drivers; system DLLs are not copied into this package. |
| Application source/build | Public GPL-3.0-or-later source, tests, CMake, dependency pins and packaging scripts | Application source is available at v1.0.0. The repository's generated source ZIP does not contain all bundled third-party corresponding source. |

The original Qt SBOM, complete attribution inventory, custom terms and standard
license texts remain retained. The selected source review supplements NOASSERTION
supplier fields with the exact source attribution entries rather than treating
that field itself as a missing license. No commercial Qt entitlement is presumed.
Shared Qt modules use their available open-source terms; recipients can rebuild,
relink the application and replace compatible Qt modules/plugins.

The package intentionally omits Qt tools/Test/development libraries, FFmpeg CLI
programs/avdevice, optional DXC binaries and unrelated build outputs. The manifest
retains `externalDistributionApproved=false`. Automated build/export success does
not establish complete corresponding-source coverage.

## Before publishing the binary draft

1. Complete FFmpeg source/patch/build/notices coverage for the actual DLLs, or
   replace them with a controlled build whose inputs we retain.
2. Provide clear directions to exact application and dependency sources, with
   build/replacement information and required notices. GPLv3 section 6(d) allows
   equivalent source access on another server; hosting every archive ourselves
   is not mandatory. Ensure continued availability.
3. Regenerate the ZIP with collected Qt/Microsoft notices and current source
   review, verify it, and attach completed source materials/directions.
4. For changed dependency binaries, rebuild the application and repeat regression
   and extracted-package workflow checks before publishing.

[The prepared supplier request](ffmpeg-supplier-source-request.md) identifies
the missing inputs. It has not been sent. No unsupported written source offer is
made. The private draft stays unpublished until these steps are complete.

Primary references: [GPLv3 section 6](https://www.gnu.org/licenses/gpl-3.0.html#section6),
[FFmpeg legal guidance](https://ffmpeg.org/legal.html),
[Gyan build information](https://www.gyan.dev/ffmpeg/builds/),
[Qt licensing](https://doc.qt.io/qt-6/licensing.html), and
[Microsoft's distributable list](https://learn.microsoft.com/en-us/visualstudio/releases/2026/redistribution).
Codec patent obligations depend on use/jurisdiction and are not cleared by
successful encoding tests.

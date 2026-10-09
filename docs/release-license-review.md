# Binary release license and source review

Reviewed on 2026-10-09 against the pinned binary configuration, supplier notices, Qt SPDX inventory and upstream guidance. The application source is now **GPL-3.0-or-later**, explicitly selected by the author for 1.0.0; see [COPYRIGHT.md](../COPYRIGHT.md). The source repository is public. The portable binary remains a **private draft/local review package** until exact dependency corresponding-source/build material is complete. This is the successor to the Session 16 personal-use review, not a claim that the supplier-source gaps have been resolved.

| Component | Evidence included | Review and external-distribution requirement |
|---|---|---|
| FFmpeg 9.0.2 Gyan full shared | GPL text, supplier README/source commit `946fcce07b`, actual configure/library/codec/filter inventories | Runtime declares GPL v3 or later; `--enable-gpl --enable-version3 --enable-shared` is present and `--enable-nonfree` absent. x264/x265 and other GPL components make an LGPL-only interpretation invalid. GPL-3.0-or-later application licensing is selected. Complete corresponding source/build instructions for the exact binaries are still required before public binary distribution. Dynamic linking does not remove these obligations. |
| Every enabled FFmpeg external integration | `notices/ffmpeg-external-review.json` enumerates all `--enable-lib*` flags plus named external/hardware integrations from the actual configuration; the supplier README includes many library versions and abbreviated revisions | The archive and release assets do not provide a complete source/patch/build recipe and attribution set for these static dependencies. Some version entries are incomplete or blank. Obtain the exact source archives, supplier build recipe, patches and notices for each shipped component; a general upstream license guess or current autobuild-suite checkout cannot establish what produced these binaries. The top-level GPL text is not a substitute for permissive-library copyright notices. |
| Qt Core/Gui/Widgets/OpenGL/OpenGLWidgets and plugins | Exact Qt SBOM including component copyrights, licenses, source/download references; readable attribution inventory, custom license texts and standard SPDX texts | Shared modules offer LGPLv3/GPL/commercial alternatives as recorded per component. Inspect all selected module/plugin dependencies and their attributions, supply exact corresponding source and any required relinking/installation information, and preserve recipient rights. Tools recorded in the full supplier SBOM are not all shipped. No commercial entitlement is presumed. |
| MSVC app-local CRT | Exact redistributable DLL file hashes/version and installed Visual Studio `Redist.txt` | Only x64 files from the pinned `VC/Redist/MSVC/14.51.36231/x64/Microsoft.VC145.CRT` directory are copied. Review the applicable Visual Studio terms and current distributable-code list before external release. The compiler and SDK are not shipped. |
| Windows/UCRT/graphics/audio | Runtime dependency/import inventory | Supplied by Windows 11 and drivers; system DLLs are not copied into this package. |
| App source and build scripts | GPL-3.0-or-later license, version/resource, dependency pins, public source and SHA-256 source inventory | Author approved the application license. Source, test code, CMake, dependency restore and package scripts are committed together. This supplies the application source; it does not supply all third-party corresponding source. No unsupported source offer is made. |

`notices/qt-component-attributions.json` retains every SBOM package's supplied copyright/license/source information; the original SBOM remains authoritative. `notices/qt-custom-licenses.txt` retains all extracted custom terms. Standard license texts come from the SPDX license-list-data repository; their provenance is retained in `notices/license-text-sources.json`. The Qt commercial alternative is recorded but no commercial license text/entitlement is claimed. `NOASSERTION` and missing supplier metadata remain review gaps, not permissive licenses.

The archive intentionally has no Qt tools, Qt Test, development libraries, FFmpeg CLI programs, optional DXC binaries or unrelated build outputs. `build/package-manifest.json` inventories the exact payload with origin hashes and records `externalDistributionApproved=false`. Keeping the binary release as a private GitHub draft permits review without treating incomplete supplier metadata as distribution clearance.

## Required before publishing the binary draft

1. Retain exact FFmpeg and all linked third-party corresponding source, patches,
   build scripts and copyright/license notices for the pinned supplier DLLs; or
   replace them with a controlled build whose complete inputs are available.
2. Retain the exact Qt source/configuration material matching the shipped modules
   and required notices, together with build/relinking instructions.
3. Verify the pinned MSVC redistributable terms and distributable-code list.
4. Attach corresponding-source artifacts alongside the binary download, verify
   their coverage, then update the package review and regenerate/test the ZIP.

The public source repository and GPL license resolve the app-source choice only.
The GitHub-generated source ZIP contains this application repository; it is not
the corresponding source for all bundled Qt/FFmpeg dependencies.

Primary references consulted: [FFmpeg legal/configuration guidance](https://ffmpeg.org/legal.html), [Gyan build information](https://www.gyan.dev/ffmpeg/builds/), [Qt licensing](https://doc.qt.io/qt-6/licensing.html), [Qt Windows deployment](https://doc.qt.io/qt-6/windows-deployment.html), [Microsoft redistributing Visual C++ files](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170). Codec patent obligations depend on use/jurisdiction and have not been cleared by successful encoding tests.

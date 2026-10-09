# Video Editor

A native Windows video editor built with C++20, Qt Widgets and FFmpeg. The
workspace includes searchable Project Media, a live Sequence viewer, a multitrack
timeline and an Inspector. Editing, preview and export share frame-exact timing.

Version **1.0.1** is available from the [GitHub release](https://github.com/NetanelGuber/Video-Editor/releases/tag/v1.0.1).
The Windows binary was published with the remaining FFmpeg dependency source
and build gaps disclosed in the release notes. See [release status](docs/release-1.0.1.md) and
[the distribution review](docs/release-license-review.md).

## Features

- Import video/audio, preview sources, trim, split, move and undo/redo edits.
- Automatic sequence end follows the last clip; manual end controls preserve
  intentional duration or trailing space.
- Titles, effect stacks, keyframes, fades, color/LUTs, masks, chroma key and speed
  changes; audio mixing, automation and buses.
- Nested sequences and camera switching.
- Atomic project saves, autosave recovery and offline-media relinking.
- Simple and Advanced export with exact compatibility checks, hardware/software
  policies and persistent encoder capability discovery. Automatic export FPS
  follows Primary Video, falling back to the sequence.

## Portable Windows package

Download [VideoEditor-1.0.1-windows-x64.zip](https://github.com/NetanelGuber/Video-Editor/releases/download/v1.0.1/VideoEditor-1.0.1-windows-x64.zip). Extract the entire ZIP to a
writable folder and open `VideoEditor.exe`. Keep its DLLs, helper executable,
plugin folders and `qt.conf` together. No developer tools, administrator access
or PATH changes are needed to run. Windows 11 x64 is the tested platform. The
package is unsigned.

- [First-use quick start](docs/quick-start.md)
- [Format and performance limits](docs/supported-formats.md)
- [1.0.1 release preparation and validation](docs/release-1.0.1.md)
- [Changelog](CHANGELOG.md)

## Build from source

Use PowerShell 7 and the pinned Visual Studio, MSVC, Windows SDK and CMake
versions in [dependencies.lock.json](dependencies.lock.json). Build dependencies
are restored under `.tools`; no global configuration changes are required.

```powershell
git clone https://github.com/NetanelGuber/Video-Editor.git
cd Video-Editor
./scripts/Install-Dependencies.ps1
./scripts/Build.ps1
Start-Process -FilePath './build/1.0.1/Release/VideoEditor.exe'

# Create a fresh draft package; refuses existing package outputs.
./scripts/Prepare-Session5Tests.ps1
./scripts/New-WindowsPackage.ps1
```

Full regression checks currently also require the original developer's real
footage inventories and generated 4K fixtures. That footage is not included in
this repository or the ZIP. See [build and test instructions](docs/build.md) and
[fixture preparation](fixtures/README.md). The application build and portable
package do not require those private originals.

The repository retains implementation reports, textual validation evidence,
dependency inventories and synthetic project fixtures. Downloads, build outputs,
generated media and backup directories are ignored. Earlier `docs/session-*.md`
reports describe historical versions and local paths; they are not current
release commands.

- [Project format and migration](docs/project-format.md)
- [Development plan and acceptance history](plan.md)
- [Rendering decisions and target-PC benchmarks](docs/rendering-decision.md)
- [Dependency inventory](docs/dependencies.md)

## License

Original application source is **GPL-3.0-or-later**; see [LICENSE](LICENSE) and
[COPYRIGHT.md](COPYRIGHT.md). Third-party components retain their own licenses.
Application licensing is complete; it does not resolve the remaining binary
dependency-source requirements documented in the distribution review.

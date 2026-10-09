# 1.0.0 release preparation

The author approved a public `NetanelGuber/Video-Editor` source repository and
GPL-3.0-or-later application license on 2026-10-09. The binary release is a
**private GitHub draft** pending the dependency corresponding-source/build
material described in [the distribution review](release-license-review.md).
There is no public binary download yet.

## Artifacts and commands

```powershell
./scripts/Build.ps1
./scripts/Test-Application.ps1
./scripts/New-WindowsPackage.ps1
./scripts/Test-WindowsPackage.ps1 -Archive out/release/VideoEditor-1.0.0-windows-x64.zip
./scripts/Collect-ReleaseEvidence.ps1
```

The application version, generated BuildInfo header and Windows executable
resource are 1.0.0. The project remains schema 11. Build and test scripts now
default to `build/1.0.0`. The package uses the existing pinned dependencies and
includes the application license, quick start, format limits, third-party
notices, synthetic flash/beep example, runtime inventory and payload hashes.

The release assets are `VideoEditor-1.0.0-windows-x64.zip` and its `.zip.sha256`
sidecar. Extract the entire ZIP and open `VideoEditor.exe`. The binary is
unsigned and Windows 11 x64 is the tested platform.

## Verification

The [release verification record](../evidence/release-1.0.0/verification.json)
retains the fresh build, nineteen-suite regression and extracted-package
workflow results. The evidence collector requires passing regression and
package reports with matching versions and an unchanged ZIP. The workflow uses a
fresh profile and Windows-only PATH, verifies every shipped file and x64 import,
exercises production project/edit/export/recovery behavior with the extracted
runtime, independently inspects/decodes the exports and verifies the packaged
bytes remain unchanged.

The full local regression uses the developer's original footage and synthetic
4K fixtures. Original footage is not committed or included in the ZIP.
Historical JSON inventories and acceptance reports remain evidence, not media
downloads or fresh tests on other computers. Automated offscreen checks do not
establish fresh-OS behavior, native display scaling, perceived AV sync, real
phone/HDR support or maximum-layer 4K memory usage. No computer-use testing is
performed for this release preparation.

## Repository scope

Commit the application source, tests, scripts, dependency pins, project fixtures,
license/notices, documentation and textual/screenshot evidence. Exclude `.tools`,
builds, package outputs, generated video and backups. Old session build/package
outputs may be removed after the verified 1.0.0 replacement is ready; preserve
source and historical acceptance records.

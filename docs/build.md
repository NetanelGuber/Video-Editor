# Build and run the native application

The app uses C++20, CMake, MSVC, and Qt Widgets. Version 1.0.1 adds sequence end controls to the GPL-3.0-or-later source release and portable Windows package; see [release status](release-1.0.1.md). [Session 16](session-16.md) describes the original packaging implementation. [Session 15.2](session-15.2.md) covers Simple/Advanced export; [Session 15.1](session-15.1.md) covers the workspace and Inspector. Earlier session reports retain detailed feature evidence and limits.

## Prerequisites and pins

Use Windows x64 and the exact installations in [dependencies.lock.json](../dependencies.lock.json): Visual Studio Community 18.6.1 with the C++ desktop workload, MSVC directory 14.51.36231/compiler 19.51.36244, Windows SDK 10.0.26100.0, and bundled CMake 4.2.3-msvc3. The build uses the Visual Studio 18 2026 generator. Ninja is inventoried in Session 0 but not used by this build.

Restore Qt base 6.11.3 and Gyan FFmpeg full shared 9.0.2 locally if `.tools` is absent:

```powershell
# Run from your Video-Editor checkout.
./scripts/Install-Dependencies.ps1
```

Restoration verifies archive SHA-256 values before extraction. The build reads the lock, checks tool/compiler versions, verifies the full MSVC compiler directory and SDK, finds the exact Qt package, and checks Qt/FFmpeg release versions again at runtime. Reinstall from the pinned archives if extracted dependencies have been modified. Paths are project-local except for the existing Microsoft tools; no global PATH or shell profile edits are required.

## Configure, build, verify, launch

Run in ordinary PowerShell; a Developer PowerShell prompt is not required:

```powershell
./scripts/Build.ps1
./scripts/Test-Application.ps1
Start-Process -FilePath './build/1.0.1/Release/VideoEditor.exe'
```

`Build.ps1` configures and builds Release, then deploys Qt runtime libraries/plugins and the pinned FFmpeg avformat/avcodec/avutil/swscale/swresample/avfilter DLLs beside the executable. Keep the Release directory together, including ExportCapabilityProbe.exe for isolated runtime driver checks. This development directory also contains test executables/libraries; it is not the release package. [Session 16](session-16.md) provides an allowlisted portable ZIP with app-local MSVC runtime, inventory and notices. Windows 11 supplies UCRT and system/driver DLLs.

For an independent clean configure/build, choose a directory that does not already exist:

```powershell
./scripts/Build.ps1 -BuildDirectory build/my-clean-check
./scripts/Test-Application.ps1 -BuildDirectory build/my-clean-check
```

The scripts never delete an existing build directory. `configure.log`, `build.log`, and `test.log` stay in the chosen directory. Changing generator, architecture, or toolset requires a new directory. CMake fails on pin drift rather than silently selecting another toolchain.

`Test-Application.ps1` prepares disposable Session 3 media, then runs nineteen CTest checks. Native `ProjectTests` (30-second timeout) covers model/migration/filesystem/interrupted-save behavior. The actual app runs with Qt's offscreen platform (20-second timeout) for project lifecycle/startup/error/layout checks. `MediaTests` (300-second timeout) compares Session 0 samples with independent FFprobe/FFmpeg processes and drives offscreen import, folder recursion, duplicate detection, save/reopen, relink, cancellation, and UI heartbeat checks; it also imports all 30 real files. Keep the Session 0 footage and synthetic fixtures available (see [fixtures](../fixtures/README.md)). Reports are `project-test-data/project-result.json`, `smoke-data/smoke-result.json`, and `media-test-data/media-result.json` in the chosen build directory. The native `TimelineTests` suite (60-second timeout) verifies editing/selection/history transactions, deterministic replay, boundaries and overflow, and repeated real save/reopen cycles; its report is `timeline-test-data/timeline-result.json`. Settings/logs use isolated test directories; normal app settings are untouched. These checks do not establish Windows rendering, native file dialogs, mouse interaction, or display scaling.

The pinned qtbase archive does not contain translation catalogs. `windeployqt` warns about the missing `translations/catalogs.json` even with translations disabled; the English shell builds and runs. Optional Vulkan headers are absent and not required by this Widgets shell. The offscreen test may log font-directory/size-hint warnings; it does not use the native Windows presentation plugin.

`PlaybackTests` adds native CPU/D3D11VA start/middle/end seeks, audio resampling, bounded queues, cancellation, fallback, and offscreen monitor playback checks; its report is `playback-test-data/playback-result.json`. `Test-Application.ps1` prepares a flash/beep source and saved sequence under `fixtures/generated/session-5/`. The default audio endpoint is exercised, but audible sync and visible presentation require the [Session 5 checklist](session-5.md#human-windows-checklist). Separate benchmark/encoder/evidence scripts are listed in that report.

`TimelineUiTests` is the sixth suite (90-second timeout). It sends offscreen Qt mouse/drop/keyboard events to the production editing widgets and checks the live viewer, mixed undo/redo, title text/rendering, dirty-state behavior and save/reopen. It writes `timeline-ui-test-data/timeline-ui-result.json` and prepares the disposable `fixtures/generated/session-6/first-cut.veproject`. Qt Test comes from the same pinned base archive and is linked only by this test executable. Follow the [Session 6 checklist](session-6.md#human-windows-checklist) for Windows interaction and visual acceptance; automated events do not replace those checks. `AudioTitleTests` is the seventh suite (120-second timeout). It checks known PCM samples against an independent overlap/gain/fade oracle, fractional frame scheduling, seeks, continuous audio across video cuts/gaps, error handling, bounded cancellation, production property dialogs, and exact undo/persistence/title rendering. It loads installed Windows fonts explicitly for offscreen glyph tests. Its report is `audio-title-test-data/audio-title-result.json`. The new soundtrack/title project is `fixtures/generated/session-7/audio-titles.veproject`; follow the [Session 7 checklist](session-7.md#human-windows-checklist). `ExportTests` is the eighth suite (240-second timeout). It independently decodes exported frames and audio, checks metadata and timing, all ten representative sources, atomic cancellation/retry, errors and the production dialog. Its report is `export-test-data/export-result.json`; follow the [Session 8 checklist](session-8.md#human-windows-checklist). Collect all twelve reports and artifact/source hashes with `./scripts/Collect-Session12Evidence.ps1` after verification.

`EffectsTests` is the eleventh suite (180-second timeout), covering effects, animated titles, independent fades, native dialogs, multilayer preview and independently decoded exports. Its report is `effects-test-data/effects-result.json`; see [Session 11](session-11.md). Existing recovery and CPU/D3D11VA 4K suites remain enabled.

## Settings and diagnostics

- App identity/version: `LocalVideoTools / VideoEditor`, version `1.0.1`; also shown in the title, executable resource and **Help → About Video Editor**.
- Layout settings: QSettings INI format, normally `%APPDATA%\LocalVideoTools\VideoEditor.ini`. Window geometry, dock/toolbar state, and preview/timeline splitter position are saved on a normal close.
- **View → Time display → Frames / Seconds** changes time units throughout the editor and remembers the choice on normal close. Frames is the default. Seconds mode covers the timeline ruler and position, sequence duration, source/sequence monitor clocks, title duration, audio/video fades, and effect keyframe times. Frame rates remain fps, and previous/next-frame buttons still step one frame.
- The seconds ruler chooses readable decimal intervals as you zoom, including fractions of a second. Timing fields accept decimal seconds and round edits to the nearest sequence frame (half-frame ties round up). Accepting unchanged fields preserves their exact stored frames, including fractional frame rates. Project timing and undo history are unaffected by changing display units.
- Logs: normally `%LOCALAPPDATA%\LocalVideoTools\VideoEditor\logs\video-editor-<UTC timestamp>-<PID>.log`. Each launch creates a file; there is no automatic retention policy yet.
- **Help → Diagnostics locations** displays the actual paths for the current launch.
- Qt and application messages use the same timestamped logging handler, with severity and category, flushed after each message. Startup records app/runtime/build versions, platform, PID, and settings/log locations. Shutdown records the exit code.
- Application errors use `Diagnostics::reportError`: error log, status message, and a plain-text modal dialog containing the reason and log path. Failure to save layout settings uses this path too. If logging cannot initialize, startup reports the path/failure and exits; failures before Qt can initialize remain platform/loader errors.
- Background media failures use nonmodal bin states/inspection notes and warning logs, with a batch summary in the status bar, so a bad file does not interrupt an import with repeated dialogs.

## Human Windows acceptance checklist

The user confirmed the original Session 1 checklist and the Session 2 project checklist on 2026-10-06; see the [Session 1 acceptance record](../evidence/session-1/manual-acceptance.json) and [Session 2 acceptance record](../evidence/session-2/manual-acceptance.json). The reference below reflects the current toolbar.

The original Session 1 checklist below remains a historical regression reference. Session 9's recovery/relink behavior is verified automatically; see [Session 9](session-9.md) for the remaining evidence boundaries. No repetition of earlier manual checklists is requested.

The Session 15.1 production-widget suite covers menus, states, Inspector commands, narrow layout, persistence and fallback dialogs. A native Windows review covers readable text and layout on this computer. See [Session 15.1](session-15.1.md) for the current evidence and explicit limits. No repeated technical checklist is requested.

RecoveryTests is the ninth suite (120-second timeout). It kills and restarts separate processes using production MainWindow, drives the recovery prompt under Qt offscreen, compares latest multitrack edits and last-good bytes, and verifies backup retention, unsaved-state decisions, migrations and recursive relinking. Reports and rendered captures are under build/session-10/recovery-test-data/. Collect all ten suites with ./scripts/Collect-Session10Evidence.ps1. See [Session 9](session-9.md) for behavior and evidence limits.


`PerformanceTests` is the tenth suite (240-second timeout). It verifies persistent aid-cache hits/invalidation/corruption/retention, byte-limited queues and cancellation, CPU fallback, exact 4K edits/persistence, production offscreen quality/memory controls, 20-second CPU/RX 9070 montage playback and independently decoded full-resolution original-media export. Its report is `performance-test-data/performance-result.json`. `Test-Application.ps1` isolates media caches under the build directory. `Measure-Session10Playback.ps1` compares all ten sources against the actual Session 9 executable and the Session 5 historical baseline; `Collect-Session10Evidence.ps1` records all reports and artifact/runtime hashes. See [Session 10](session-10.md) for the user-confirmed visible smoothness check and completion record; no repeat manual checks are requested.



`AdvancedEffectsTests` is the twelfth suite (240-second timeout). It covers color/LUT pixels, mask/key edges, speed source mapping, trim/split, native controls, moving-video preview/export, bounded pitch-preserving audio and independently decoded AAC. Report: `advanced-effects-test-data/advanced-effects-result.json`. The pitch filter uses the pinned `avfilter-12.dll`, deployed by the build script. See [Session 12](session-12.md) for its explicit color, audio-quality and long-audio-seek limitations.



`ExportTabsTests` exercises the production Simple/Advanced dialog, cross-tab synchronization, exact automatic/manual FPS intent, stale-setting warnings, audio dependencies, native options and undo/save/reopen. It independently inspects and decodes exports from both tabs. Reports and offscreen layout captures are in `export-tabs-test-data/`; `CapabilityCacheTests` also verifies the persistent export cache. Collect the current eighteen-suite evidence using `./scripts/Collect-Session15_2Evidence.ps1`.


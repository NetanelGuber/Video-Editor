# Session 1: native application shell

Completed 2026-10-06. Automated configure/build and shell checks passed, and the user confirmed that the full manual Windows checklist passed. Scope is limited to Session 1.

## Deliverable

`build/session-1/Release/VideoEditor.exe`, version 0.1.0, is a Windows x64 C++20/Qt Widgets executable. The main workspace includes File/View/Help menus, a movable toolbar, dockable media bin, persistent sequence-viewer/timeline split, placeholder video/audio rows, and status/progress area. Placeholders explicitly identify the later sessions that supply import, playback, and editing.

Normal close stores the window geometry, docks/toolbar, and splitter using QSettings INI format. View can recover the default workspace. About shows application, Qt, FFmpeg, compiler, and SDK versions. Diagnostics locations shows the current settings/log paths.

Logging captures application/Qt messages with UTC timestamps, severity, category, startup context, intentional errors, layout saves, and exit code. The shared application-error path logs the reason, updates the status bar, and displays a dialog with the log location. The Help menu provides an intentional test error. FFmpeg data types remain behind an app-owned media interface; this shell checks runtime identity only.

## Verification

| Check | Evidence / result |
|---|---|
| Fresh Windows x64 configure/build using all toolchain pins | Passed; [configure log](../evidence/session-1/configure.log), [build log](../evidence/session-1/build.log) |
| Actual executable startup, widget structure, dock toggle/reset | Passed with Qt offscreen platform; [test log](../evidence/session-1/test.log), [result JSON](../evidence/session-1/smoke-result.json) |
| Real Help action, modal intentional-error dialog, status update, startup/error log content | Passed offscreen; [application log](../evidence/session-1/application.log) |
| Settings written and reopened with window size/right dock recovered | Passed with isolated test settings, no normal user settings written |
| Native dependency boundary | Source and PE imports inspected; [runtime inventory](../evidence/session-1/runtime-inventory.json); Qt Core/Gui/Widgets, avutil, Microsoft runtimes/system DLLs; no browser UI component |
| PowerShell script syntax and local documentation links | [Static verification](../evidence/session-1/verification.json) |
| Human rendering/scaling, mouse docking/resizing, normal Windows plugin startup | Passed per user confirmation of the full [manual checklist](build.md#human-windows-acceptance-checklist), 2026-10-06; [manual acceptance record](../evidence/session-1/manual-acceptance.json) |

The fresh build warns that the qtbase archive lacks translation catalogs; deployment explicitly disables translations and the English shell works. Optional Vulkan headers are absent and unused. Offscreen-plugin font/size-hint warnings do not establish a native rendering defect or visual acceptance.

No agent computer-use automation was performed. Human visual and interaction acceptance is recorded from the user's confirmation, separately from the automated offscreen evidence. No media files were opened or modified by the implementation work, and no global environment/profile configuration was changed. Session 2 project persistence and later media features are deferred. The runtime directory is a local development output, with packaging/licensing review and clean-machine installation deferred to Session 15.

See [build instructions](build.md) and the updated [dependency/license inventory](dependencies.md).

# Session 15.1 — organized native editing workspace

Complete and accepted by the user on 2026-10-08. Version 0.15.2, project schema 9 unchanged. The completed concept and wireframe in [plan.md](../plan.md) were the implementation specification. Acceptance is recorded in [manual-acceptance.json](../evidence/session-15.1/manual-acceptance.json).

## Workspace and workflows

- File, Edit, Media, Sequence, View and Help group commands by task. The main toolbar offers New/Open/Save, Import media and Export video. Ctrl+I and Ctrl+E remain available, along with the existing timeline shortcuts.
- **Project Media** has a single Import media button (Files… / Folder…), search, Online/Offline states, selected-media preview/details and contextual Locate file. Explicit Import files/folder, refresh, relink and cancel actions remain in Media. Inline task counts, cancellation and retry instructions stay visible after the status-bar message changes.
- **Sequence** and **Source** are explicit viewer tabs. Preview source opens Source; Return to Sequence, timeline activation, edits and seeks return to Sequence. The sequence summary includes name, rate, duration and current position. The viewer remains above the timeline.
- The dockable **Inspector** shows selected media, clip/title or track context. Clip shows timing and track enabled/locked controls; titles can be edited inline. Effects shows the ordered stack with undoable enable toggles. Audio / Track provides separate track and clip volume, balance and mute controls, plus track Solo. Applying untouched controls preserves exact existing values, automation, fades and routing.
- Longer title styling, effect parameters/keyframes, routing, fades and automation use the existing focused native forms. Edit/context-menu Clip, Effects and Track audio commands choose the Inspector section when it is visible and open the existing form when it is hidden. Double-click remains a direct focused-form entry. These forms remain available from Inspector buttons.
- Sequence management and nesting live in Sequence. Only relevant Open child or camera controls appear in the sequence header. The timeline keeps editing/creation tools and the media-placement/zoom row.
- Below 1,100 logical pixels, the docked Inspector automatically hides to preserve the viewer and timeline; it returns when space permits. Both side panels remain hideable/dockable through View. Reset workspace restores their default locations. The new dock-layout key is `window/state15_1`.
- Recovery keeps Recover selected / Later / Discard selected… distinct. Opening a recovery copy displays a persistent notice to review it and save a working copy. Successful save resolves the notice. Project open/save failures explain preservation and recovery, with technical diagnostics behind Show Details.
- **Export video** summarizes destination, target and audio. Encoder/format/tuning controls are behind Encoder and format details; native options have another expandable group. Custom mode and invalid configurations reveal the relevant controls. Capability checking, ready, blocked, exporting, cancellation, failure and completion keep the existing capability gate and atomic-output behavior. Completion names the actual encoder and software fallback warning. Export details contains probe diagnostics. Failed probes refer to the checked configuration and computer.
- Getting started and Keyboard shortcuts replace the production intentional-error command. The offscreen smoke test still exercises error reporting directly.

This session changes presentation and command routing. Project records, exact timing, playback/rendering, codec support, runtime capability rules and per-track ripple semantics remain the existing implementation. Simple/Advanced tabs and their final choice boundaries remain Session 15.2.

## Build and evidence

```powershell
./scripts/Build.ps1
./scripts/Test-Application.ps1
./scripts/Collect-Session15_1Evidence.ps1
Start-Process -FilePath './build/session-15.1/Release/VideoEditor.exe'
```

The sixteen-suite regression includes `WorkspaceTests`, which drives the production MainWindow and verifies menus/toolbars, state changes, import/search/offline/retry feedback, source/sequence routing, selection context, inline Inspector edits, exact undo, locks, dialog fallback, narrow geometry, persistence, recovery notice and real export completion. The existing suites retain independent video/audio export inspection, recovery and timeline interaction, per-track ripple, effects, nesting/multicamera, 4K workloads and encoder probes. A previous Session 15 assertion was updated to mark its deliberately invalid frame rate as a manual override, matching the already-existing schema-9 behavior.

The final Release build passed all sixteen suites in 158.41 seconds. WorkspaceTests passed 58 checks with both the offscreen and native Windows Qt platforms. Agent visual review inspected the current Windows workspace, clip selection/Inspector, empty/offline states, export-ready dialog and the 1,400 × 900 / 850 × 700 layouts. The narrow layout was exercised by the native widget test.

Current reports, source/artifact hashes, screenshots and evidence limits are collected under [evidence/session-15.1](../evidence/session-15.1/verification.json). Native Windows appearance is reviewed on the current computer separately from Qt offscreen tests. Other display scales/screens, subjective appearance, sustained playback smoothness and perceived AV sync are not established by these UI checks. Existing physical-device and long hardware-session limits remain.

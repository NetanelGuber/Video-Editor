# Session 2: project model and safe persistence

Completed 2026-10-06, version 0.2.0. Native automated tests passed, and the user confirmed that the full manual Windows checklist passed. See the [manual acceptance record](../evidence/session-2/manual-acceptance.json). Human acceptance is reported by the user, separately from automated evidence.

## Deliverable

`build/session-2/Release/VideoEditor.exe` supports New/Open/Save/Save As, project/sequence summaries, offline media references, dirty-document Save/Discard/Cancel protection, and separate autosave snapshots. The typed Qt Core project library defines project, sequence, track, media/stream, clip, title, effect, and export settings. Rational time and int64 frame/tick/sample values have explicit conversion/rounding rules. See [format and persistence policy](project-format.md).

Writes use QSaveFile without direct-write fallback, a validated previous-save `.bak`, and per-transaction locks. Unknown/invalid records fail usefully; missing source files remain referenced and offline. The checked-in v1 fixture exercises an explicit migration to schema 2. The UI shows loaded records as summaries; importing media, editing/rendering these records, automatic recovery discovery, and interactive relinking remain in their planned sessions.

## Verification

| Check | Result / evidence |
|---|---|
| Fresh configure/build with pinned Windows x64 MSVC/Qt/FFmpeg | Passed; [configure](../evidence/session-2/configure.log), [build](../evidence/session-2/build.log) |
| Exact multiple-sequence/track/media/title/effect/export model round-trip, including int64 above 2^53 and 100 cycles | Passed native model tests; [project results](../evidence/session-2/project-result.json) |
| v1 migration, field-qualified errors for malformed/unsupported/invalid files, reference/source/sequence bounds | Passed native model tests |
| Rational frame/sample rounding, ties, long durations, invalid inputs and overflow | Passed native model tests |
| Disk round-trip, byte-exact previous-save backup, independent autosave, Save As path rebasing, offline sources, source protection and competing lock | Passed native filesystem tests |
| Backup failure, invalid primary/model, invalid destination preserve last good primary/backup | Passed native filesystem tests |
| Kill actual process after half-written temporary file, verify old primary/backup, save again after stale lock | Passed native process-termination test |
| New action, save/open multiple tracks, offline summaries, migration dirty state, autosave/cleanup, Cancel close, failed load/save state preservation | Passed actual app with Qt offscreen platform; [UI results](../evidence/session-2/smoke-result.json) |
| Existing startup/error/logging, dock reset and layout persistence | Passed offscreen regression check; [test log](../evidence/session-2/test.log), [application log](../evidence/session-2/application.log) |
| Native Windows file dialogs/menu/toolbar, visual summaries, human save/reopen/cancel behavior | Passed per user confirmation of the full checklist, 2026-10-06; [manual acceptance](../evidence/session-2/manual-acceptance.json) |

The existing missing translation-catalog warning remains in the pinned qtbase deployment. Optional Vulkan headers remain absent and unused. Automated offscreen results do not establish Windows rendering, display scaling, or media playback. No computer-use automation was performed; tests drive app-owned methods/actions inside an isolated offscreen process. Original footage was not modified.

## Human Windows checklist

The user confirmed all checks below passed on 2026-10-06. This records the checklist used for Session 2. Future sessions must follow the simpler human-testing instructions in [the plan](../plan.md#cross-session-engineering-rules).

Run the application yourself:

```powershell
Start-Process -FilePath './build/session-2/Release/VideoEditor.exe'
```

1. Select **File → New project** (Ctrl+N); confirm an Untitled title with `*`, Sequence 1 at 30/1 fps/48 kHz, and empty Video 1/Audio 1 tracks. Save (Ctrl+S) to a new disposable folder as `test.veproject`. Confirm the title loses `*` and the status shows the saved path.
2. Save again. Confirm `test.veproject.bak` exists and both files contain complete JSON. Close/relaunch, use **File → Open** (Ctrl+O), and reopen `test.veproject` with the same sequence/tracks.
3. Open `fixtures/projects/multitrack-v2.veproject`. Confirm two offline media rows (paths in tooltips), four active-sequence tracks with clip counts, and 30000/1001 fps/450 frames. **Save As** (Ctrl+Shift+S) to your disposable folder, close/reopen that copy, and confirm the same summaries. Treat the repository fixtures as read-only test inputs: save copies in your disposable folder.
4. Open `fixtures/projects/multitrack-v1.veproject`. Confirm the migration status and `*`. Save As to another disposable filename. Confirm schemaVersion is 2 and duration/frame fields are strings. The original fixture should remain schema 1.
5. New project, then try New/Open/Close while dirty: **Cancel** preserves the document/window; **Save** followed by cancelling its file dialog preserves the dirty document; **Discard** permits the action. Save before exiting if you want to keep the new project.
6. New project and leave it dirty for just over two minutes. Confirm a recovery-snapshot status message/path. Open that `.autosave` manually: it should load dirty and request a primary location on Save. A startup recovery prompt is planned for Session 9.
7. Create a disposable malformed file containing `{truncated`, then open it. Confirm a useful error/dialog/log and that the previous project remains open. Try saving into a location you cannot write: confirm the same error behavior and that no good project is truncated.

Report any dialog, shortcut, dirty-state, offline-summary, or reopen problems. Playback, media import, timeline editing, and export are not part of these checks.

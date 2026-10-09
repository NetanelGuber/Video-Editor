# Session 4: timeline editing model and undo/redo

Implemented 2026-10-06, version 0.4.0. The deterministic editing library, synthetic fixture, clean pinned build, and automated acceptance checks are complete. This session implements the model; mouse/keyboard editing controls remain Session 6, and playback remains Session 5. No computer-use automation or human visual acceptance is claimed.

## Deliverable

[Timeline.h](../src/timeline/Timeline.h) exposes `TimelineEditor`, typed commands, selection, gap queries, source-boundary mapping, and frame occupancy. [Timeline.cpp](../src/timeline/Timeline.cpp) implements them in `editor_timeline`, which depends only on `editor_project` and Qt Core. It has no Widgets, drawing, FFmpeg, or filesystem dependency. The existing schema-2 project records already store everything needed; no migration or schema change is necessary.

Commands add/remove video, audio, and title tracks; enable/disable and lock/unlock tracks; select tracks/clips; insert, delete, split, trim, and move clips; close a gap; and explicitly resize the sequence. Each successful change is one named undoable transaction. Callers provide IDs for created records and split effects once, using the existing `newId()` outside the command if needed. Identical initial models and command payloads produce identical results. Redo restores recorded results, including IDs, without rerunning ID generation.

The [editing fixture](../fixtures/projects/editing-v2.veproject) contains a 30000/1001 fps sequence, 24000/1001 and 144 fps video sources, 48 kHz and 44.1 kHz audio, a title, an opaque effect, an overlap, leading/internal/trailing gaps, and a disabled track. Its source metadata is synthetic. Referenced media is intentionally absent; this is a deterministic model fixture, not decoded footage.

## Editing rules

| Operation | Behavior |
|---|---|
| Normal insert/move | Place at the requested integer sequence frame. Other clips remain where they are; overlaps are permitted. |
| Split | Cut strictly inside the clip. Preserve the left ID, create the right ID, and partition both timeline and source intervals without losing duration. Effects retain their parameters/order; copied right effects receive distinct IDs. A selected clip selects both halves. |
| Normal trim | Change both timeline edges and the corresponding source edges, including extensions within available source bounds. Preserve identity and effects. |
| Normal delete/remove track | Remove the requested record(s), leave a gap, and prune deleted selection IDs. Media/title records remain in the project. |
| Ripple insert/delete | Insert/remove the occupied interval and shift fully later clips by its frame count on the edited track. Existing gaps are retained. |
| Ripple trim | Change only the out point. Later clips shift by the signed duration difference. Use a normal trim to change the in point. |
| Ripple move | Remove the original interval on the source track, then insert on the destination track. The requested destination frame is measured **after** removal. Cross-track moves affect only those two tracks. |
| Close gap | Remove a specified empty interval inside the sequence, shifting fully later clips on that track to the left. Partial gaps may be closed. |
| Enable/lock | Disabled tracks remain editable and occupy their stored intervals, but default frame queries exclude them. Locked tracks reject edits, removal, enabled-state changes, and moves into/out of them. Selection and unlocking remain possible. |
| Sequence duration | Insert/move/extension grows the sequence only as needed. Delete, shortening, gap closure, and track removal retain its explicit end, including trailing silence/black. Resize explicitly to shorten; cutting off a clip is rejected. |

The user selected ripple on the edited track only. Other tracks, including audio that might be related to a video source, do not shift implicitly. Linked/grouped clip editing is not implemented in this session.

Normal overlaps are preserved in vector order; splitting inserts the right half next to the left, same-track moves preserve ordering, and new/cross-track clips append. `clipsAtFrame` returns IDs in sequence-track/clip-vector order. This establishes deterministic occupancy, not a compositing, transition, or mixing policy; rendering is addressed in later sessions. `gaps` computes the complement of the union of clip intervals, so overlapping clips do not invent gaps.

Ripple removal rejects another clip intersecting the removed interval. Ripple insertion rejects a clip crossing the insertion boundary. Choose a gap/clip edge or resolve the overlap first. Failure after any intermediate removal/shift rolls back the entire candidate model and leaves both history branches intact.

## Exact time and source boundaries

Timeline positions/durations are int64 sequence frames; intervals are `[start, end)`. Negative starts, empty clips, arithmetic overflow, missing/duplicate IDs, incompatible track/stream kinds, and out-of-source ranges fail with a useful error. A split at either clip edge is rejected. Placement at frame zero is allowed; clips ending at the sequence end are valid. Inserting at that end grows it. Fractional timeline edit positions are not part of this API; future UI seeking/snapping must choose an explicit rounding mode.

Media clips already persist independent timeline and source intervals. This session maps a relative frame boundary `f` to:

```text
sourceInTicks + round(f * sourceDurationTicks / durationFrames)
```

The calculation uses an exact 128-bit intermediate, with nearest rounding and half-tick ties away from zero for signed trim offsets. A split calculates one shared source boundary: the left exclusive out equals the right in, and source durations sum exactly to the original. An edit that rounds a media segment to zero source ticks is rejected. Title clips keep zero source fields.

Trim extension extrapolates the stored interval ratio and then checks the selected stream bounds. This respects schema-2 clips whose source/timeline durations were specified independently; it does not silently normalize them to a nominal source frame rate. The new `framesToTicks`/`ticksToFrames` helpers support converting new source selections using the sequence rate and stream timebase, with explicit Floor/Ceil/Nearest rounding. The source's average frame rate is metadata rather than an edit grid, so 23.976/144 fps footage and audio can share the sequence grid.

Persisted endpoints remain the authoritative source range after splitting/trimming. Sub-tick precision is not stored: successive fractional-tick trims may round differently from one equivalent combined trim. Saving/reopening does not add rounding or change the result of the next edit. Actual decoded VFR frame selection, effects with clip-local keyframes, audio resampling, and playback/export sampling belong to later sessions.

## Undo, selection, and document lifecycle

`execute` works on a candidate `State`, validates all project references and selection, and publishes only on success. The command history stores before/after states using Qt's implicitly shared values. Undo/redo restores the complete typed model, exact ordering, media references, explicit sequence duration, effect payloads, and selection. Successful edits after undo discard the old redo branch. Failed operations and no-ops retain it.

Selection uses stable IDs in one sequence; it allows selecting locked/disabled tracks and clips. It is undoable editor state and is deliberately absent from project serialization. A future UI should mark the document dirty only when the project changes, not for a selection-only command. `replaceProject` validates before replacing and clears history/selection on success; failed replacements preserve everything. This is the document-switch hook for Session 6, when the current application project actions will be routed through the editor.

History retains 128 commands by default, configurable at construction. Evicting the oldest entry leaves the remaining undo base coherent. This is a bounded snapshot implementation for the first editing slice; large-project memory/latency and command coalescing have not been benchmarked. UI undo shortcuts and dirty-state integration are Session 6.

## Verification and use

Run from the workspace in PowerShell:

```powershell
./scripts/Build.ps1
./scripts/Test-Application.ps1
```

The pinned clean Release build is `build/session-4/Release`. The test script runs four CTest suites, including [TimelineTests.cpp](../tests/TimelineTests.cpp). The timeline suite covers all command types, exact undo/redo, mixed commands, branch/no-op behavior, history eviction/document switches, overlaps/gaps, locked/disabled tracks, source/media references, split effects/selection, integer overflow, boundaries, and edits above 2^53. It also repeats whole-history undo/redo 100 times, serialization 100 times, and real atomic save/reopen 50 times. Fractional audio source edits continue identically after reopen.

Reports and clean build/test logs are saved under [evidence/session-4](../evidence/session-4/verification.json). Previous project persistence/process-interruption, offscreen application lifecycle, and real-file media import/inspection checks run as regressions. Those checks do not establish native rendering, playback, GPU behavior, or interactive timeline editing.

No human test is required to establish this model-only session's acceptance. If you want to check the updated app text yourself, launch `B:\Coding\02-Projects\Tools\Video-Editor\build\session-4\Release\VideoEditor.exe`, choose **Help → About Video Editor**, and expect version **0.4.0** with the Session 4 description. The timeline still shows its existing track summary and says editing controls are planned for Session 6. The existing [Session 3 checklist](session-3.md#human-windows-checklist) remains available for manual import regression testing.

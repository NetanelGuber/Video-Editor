# Session 6: timeline interface and basic edit workflow

Complete, 2026-10-06, version 0.6.1. The pinned build and all six automated suites pass, and the user accepted Session 6 and authorized marking it complete after the drag-entry fix. See [Session 6 evidence](../evidence/session-6/verification.json) and [user acceptance record](../evidence/session-6/manual-acceptance.json). Individual manual checklist outcomes were not separately reported. No computer-use automation was performed.

The user reported that step 4 failed in 0.6.0. Version 0.6.1 corrects drag entry through track labels and the ruler: a known media drag is accepted into the whole timeline viewport, then movement/drop validate the actual track body, type and lock state. The old test injected a drop directly into a valid body and missed entry routing. The regression now uses the real media-bin model's MIME payload and enters through labels before moving/dropping into Video 2; ruler entry and rejected label/locked-track/unknown-media drops are also checked. [Qt's drag-entry contract](https://doc.qt.io/qt-6/qdragenterevent.html#details) requires acceptance at entry to receive later movement. The initial 0.6.0 evidence is retained in `evidence/session-6/initial-0.6.0/`; the user's [manual feedback](../evidence/session-6/manual-feedback.json) records the issue and subsequent Session 6 acceptance.

## Editing workspace

The timeline draws the active sequence's ordered video, audio and title tracks, clip names, in/out handles, selection, frame ruler, playhead and explicit sequence end. Zoom, horizontal/vertical scroll and view changes do not alter project timing. Positions are integer sequence frames; pointer positions round to the nearest frame. Snapping chooses a nearby playhead, sequence boundary or clip edge within eight pixels. Both move edges can snap. Zoom and scroll use pixels only for display; saved edit positions remain exact.

Drag an imported media item onto a compatible unlocked track. A video track chooses a video stream; an audio track chooses an audio stream. The stream selector lets you choose among multiple streams, and **Add at playhead** inserts that stream on the selected compatible track, then the first available track, or a new track. Insertion uses the reported stream duration rounded down to a whole sequence frame. Video and audio are separate clips; drag placement does not implicitly link them. Source files stay untouched.

Drag a clip's middle to move it, including to another compatible track. Drag either edge to trim within its source bounds. A dashed rectangle shows the pending edit; releasing commits one command. **Escape** cancels a pending drag. Selection uses stable IDs; Ctrl+click selects multiple clips for deletion, splitting, trimming or frame nudges. A middle drag moves one clip. **Add track** creates video, audio or title tracks. In each track header, click **E: on/off** to enable/disable preview, or **L: open/locked** to lock/unlock editing. Rejected edits leave the entire model and history unchanged and show a reason in the status bar.

**Ripple** shifts fully later clips only on the edited track, following the [Session 4 rules](session-4.md). Ripple out trims are supported; ripple in trims are rejected. Ripple moves use their destination frame after removing the old interval. Overlap conflicts are rejected transactionally. **Close gap** removes the selected track's empty interval under the playhead. Deletion and shortening retain the explicit sequence end, including trailing black/silence.

The sequence viewer stays above the timeline in a noncollapsible splitter. Entering the timeline returns to the **Sequence** tab and pauses source playback. Sequence playback and the monitor's seek/frame controls update the timeline playhead; timeline ruler clicks/scrubs and arrow keys seek the viewer. Playback follows the playhead horizontally when necessary. Edits pause playback, rebuild the shared description and seek back to the retained playhead; they do not reset it to zero. Scrub requests coalesce before asynchronous decoding.

Every project edit, track control, media import/relink and selection change goes through `TimelineEditor`. New batch commands make a multi-clip operation or title placement one atomic undo transaction. New/open clears history and resets the playhead; explicit save retains history. Selection-only commands are undoable but do not dirty the document. Undoing back to the saved project clears its changed marker. Titles, track changes, edits and media records retain the schema-2 format; no migration was needed.

## Keyboard controls

Click the timeline before using these shortcuts. Undo/redo buttons are also in its toolbar. A narrow window may put toolbar actions in the right-hand overflow menu.

| Key | Action |
|---|---|
| Space | Play/pause the project sequence |
| Left / Right | Seek one sequence frame backward/forward |
| Home / End | Seek the start/explicit sequence end |
| S | Split selected unlocked clips under the playhead; with no selected clips, split unlocked clips crossing it |
| Delete | Delete selected clips |
| [ / ] | Trim selected clips' in/out edges to an interior playhead frame |
| Ctrl+Left / Ctrl+Right | Move selected clips by one frame |
| Ctrl+Z / Ctrl+Y | Undo/redo, including selection changes |
| R / N | Toggle ripple/snapping |
| Escape | Cancel a pending drag and clear selection |
| Ctrl+wheel | Zoom |
| Shift+wheel | Scroll horizontally |

## Basic title preview and carried limits

**Add title** places a three-second title at the playhead, creating its track as needed. Double-click a title clip to edit its text. Record, track, clip and selection changes undo atomically. The shared rendering helper draws enabled titles above the active video, or on black in a gap, using their saved font/color/alignment/position/background/shadow values. Text, timing and basic placement are usable now so Session 6's viewer acceptance includes titles. The full styling/duration dialog remains Session 7. Split title halves reference the same title record, so editing their shared text changes both; locked uses reject text edits.

The Session 5 preview still shows the last active video clip and one active audio clip. Overlapping video clips are allowed by the model but are not blended; multiple audio clips are not mixed. Title overlays and their timing are evaluated now. Audio gain/mute/solo UI, fades, title styling and mixing remain later sessions. There is no export yet. Preview images remain bounded to 1280×720; title text uses Qt's rasterizer. Export must reuse this title rendering policy or document/verify its differences in Session 8. Large-project history/drawing, long edited playback and real 4K performance remain Session 10; the horizontal scrollbar uses coarser units for exceptionally large frame ranges.

## Automated verification

Run in PowerShell from the workspace:

```powershell
./scripts/Build.ps1
./scripts/Test-Application.ps1
./scripts/Collect-Session6Evidence.ps1
```

The six suites cover native project persistence/migration/interrupted saves, the offscreen application lifecycle, real media import/inspection/relink/cancellation, the deterministic timeline model, native playback/decode, and the new offscreen timeline interface. [TimelineUiTests.cpp](../tests/TimelineUiTests.cpp) sends Qt mouse/drop/keyboard events to the production widgets. It checks exact clip placement/trim/move, selection, snapping, scroll/zoom, lock/enable, split/delete/undo/redo, Escape cancellation, ripple isolation, gap closing, frame nudges, title dialog/pixels/timing, failed-batch rollback, complete mixed-history reversal, dirty state and atomic save/reopen. It also checks the production live viewer's graph, visibility and asynchronous decoded/title frames. These are automated behavioral checks; they do not establish native display scaling, pointer feel, audible sync or perceived playback smoothness.

The test prepares `fixtures/generated/session-6/first-cut.veproject` automatically. It references the generated twelve-second flash/beep video, with three-second clips at the start of Video 1 and Audio 1, an empty Video 2, and a twelve-second sequence. Generated fixtures are disposable; rerunning the tests recreates this starting project. Evidence records original footage size/mtime and pinned FFmpeg DLL hashes without changing sources.

## Human Windows checklist

Use these exact files:

```text
App: B:\Coding\02-Projects\Tools\Video-Editor\build\session-6\Release\VideoEditor.exe
Project: B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\session-6\first-cut.veproject
Your test copy: B:\Coding\02-Projects\Tools\Video-Editor\fixtures\generated\session-6\my-first-cut.veproject
```

The ruler's numbers are frames: this project uses 30 frames per second. The red line is the playhead, meaning the frame currently shown in the viewer. Keep **Ripple** off initially. Keep the **Sequence** tab visible. If a toolbar button is hidden, widen the window or open the arrow at that toolbar's right edge.

1. Launch the app above. Choose **File → Open project…** and open the project above. Expect Video 1, Video 2 and Audio 1, two short clips, and the test pattern in the viewer.
2. Choose **File → Save project as…** and save to the test-copy path above. Expect that filename to save successfully without changing the clips.
3. Move the timeline's **Zoom** slider toward the left until frames 0–500 fit. Expect the clips to get narrower while the viewer remains visible.
4. Drag **flash-beep.mp4** from the media bin onto the empty timeline area to the right of the **Video 2** label, around frame 120. Expect a new video clip and the Sequence viewer to remain above the timeline.
5. Click the ruler around frame 180, inside the new clip. Expect the red line and test pattern to update to that position.
6. Drag the new clip's right edge left, stopping around frame 300. Expect its length to shorten and the viewer to retain the playhead position.
7. Drag its left edge right a little. Expect the left edge to move while the right edge stays fixed.
8. Drag the clip's middle down/up onto **Video 1**, after its original short clip. Expect the same selected clip to move tracks, with Audio 1 staying in place.
9. Click the ruler inside the moved clip, then press **S**. Expect two selected clip halves at the red line; the viewer should keep showing the same position.
10. Press **Delete**. Expect both selected halves to disappear and the viewer to show the resulting gap at that position.
11. Press **Ctrl+Z**. Expect both halves and their selection to return.
12. Press **Ctrl+Y**. Expect the deletion to happen again.
13. Press **Ctrl+Z** once more to restore the halves. Turn **Ripple** on, select the original short Video 1 clip at frame 0, and press **Delete**. Expect the later Video 1 clips to shift left; Audio 1 should stay at frame 0.
14. Press **Ctrl+Z** to restore that clip, then turn **Ripple** off. Click the ruler around frame 30 and click **Add title**. Enter `Session 6` and click **OK**. Expect a title track/clip and that text over the pattern.
15. Double-click the title clip. Change the text to `Edited title` and click **OK**. Expect the text in the viewer and timeline clip to update.
16. Click **L: open** on the title track. Try dragging its clip. Expect it to stay put. Click **L: locked** to unlock it again.
17. Click **E: on** on the title track. Expect the title to disappear from the viewer. Click **E: off** to show it again.
18. Click the timeline, then press **Space**. Expect playback and the red line to advance. Press **Space** again to pause, then **Left** and **Right** to step the picture one frame.
19. Drag the horizontal scrollbar and change Zoom. Expect the timeline view to move/resize while the edit and viewer remain available. Resize the app and drag the divider between viewer and timeline; check for clipped or unreadable controls at your Windows scaling.
20. Press **Ctrl+S**, close the app, launch it again and open your test copy. Expect the saved clip positions, title text and track settings to match. Click the ruler to confirm its viewer still works.

The user accepted Session 6 after the 0.6.1 fix. This checklist remains available for regression testing; report any failed step number and what appeared.

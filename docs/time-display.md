# Time display setting

Choose **View → Time display → Frames** (the default) or **Seconds**. The choice applies across the editor and is saved in the existing application preferences as `display/timeFormat` on normal close.

Seconds mode displays total seconds in the timeline ruler, playhead position, sequence duration and both monitor clocks. Title duration, audio fades, video fades, effect keyframe times, and audio track/clip automation key positions also show and accept seconds. Track automation seconds are measured from sequence start; clip automation seconds are relative to original clip-content start. Values round to the nearest sequence frame, while stored positions remain exact integer frames and unchanged values preserve their exact timing. Frame rates remain fps; previous/next-frame and arrow-key stepping keep their one-frame behavior.

The ruler selects decimal intervals in the 1/2/5 sequence, using the sequence's rational frame rate, zoom and measured text width. Zooming in reveals smaller intervals, down to readable fractions of a second. Intervals stay at least one sequence frame apart. Scrolling keeps labels relative to the sequence origin.

Timing edits are converted from decimal seconds using integer rational arithmetic, rounded to the nearest sequence frame with half-frame ties upward. Unchanged fields retain their original frame counts. Switching units does not modify the project, seek playback, mark the document dirty or add undo entries.

## Manual check

1. Launch `B:\Coding\02-Projects\Tools\Video-Editor\build\session-13\Release\VideoEditor.exe` and open `B:\Coding\02-Projects\Tools\Video-Editor\build\session-13\export-test-data\reference.veproject`.
2. Choose **View → Time display → Seconds**. Check that the ruler, playhead, sequence duration and source/sequence monitor clocks display seconds.
3. Move the timeline **Zoom** slider toward the right, or use Ctrl + mouse wheel. Check that labels show smaller decimal intervals and remain readable without overlapping. Scroll sideways and check that times continue from the sequence origin.
4. Check title duration, audio fades, and **Effects and fades**. In **Track audio**, open **Edit track automation…** and change the Soundtrack volume key from 6 seconds to 4 seconds. Accept the automation and audio dialogs. Then open **Edit clip automation…** for `soundtrack.wav` and change its volume key from 3 seconds to 2 seconds; accept both dialogs. Track times are from sequence start; clip times are from original content start.
5. Choose **Frames** and reopen both automation editors. The edited keys should now read 120 and 60 frames respectively (the fixture is 30 fps). This confirms the entered seconds round to exact sequence frames. Close without saving the disposable project edits, reopen the editor, and check that the last time-display choice is retained.

Automated verification exercises the production Qt widgets offscreen; visible Windows rendering and display scaling need the manual check above.

## Verification on 2026-10-08

`scripts/Build.ps1` succeeded and `scripts/Test-Application.ps1` passed all eleven suites for the original Session 11 time-display implementation. The expanded timeline interface suite passed 112 checks, including display persistence, no project/history changes from switching units, zoom/scroll spacing at 24, 30, 30000/1001 and 60 fps, decimal timing inputs in production dialogs, nearest-frame rounding, unchanged-value preservation and invalid/overflow input rejection. Logs are in `build/session-11/time-display-build.log` and `build/session-11/time-display-verification.log`; the interface report is `build/session-11/timeline-ui-test-data/timeline-ui-result.json`. Session 13 extends the setting to audio automation keys; that follow-up compiles but has not had its regression suites rerun. See the [correction record](../evidence/session-13/time-display-correction.json).

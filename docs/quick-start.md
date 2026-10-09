# Video Editor 1.0.1 quick start

## Install and open

Extract the entire `VideoEditor-1.0.1-windows-x64.zip` into a folder you own, such as `Documents\Video Editor`. Open `VideoEditor.exe` inside that folder. Windows 11 x64 is the tested platform. You do not need Visual Studio, Qt, FFmpeg, an administrator account or a PATH change. Keep the DLLs, plugin folders, `qt.conf` and `ExportCapabilityProbe.exe` beside the app. The package is unsigned.

To update, extract the new version into a new folder. To remove the app, delete its extracted folder; projects and source media stay where you saved them. Layout, logs, caches and recovery snapshots stay in your Windows profile. **Help → Diagnostics locations** shows the actual settings/log paths. Back up any recovery work before removing profile data.

## Make a first video

1. Choose **File → New project**. Choose **Media → Import files…** (Ctrl+I) and select your video. For a small example, use `examples\flash-beep.mp4` inside the extracted folder.
2. In **Project Media**, select the imported file. Use **Preview source**, then **Add selected media at playhead**, or drag it onto a video track. Add its audio stream to an audio track if you want sound.
3. Select a timeline clip and move the playhead inside it. Click **Split** or press **S**. Drag the clip edges to trim, or drag the clip to move it. **Ctrl+Z** undoes an edit. **Ripple** affects only the edited track.
4. The **Sequence** viewer shows your edits. Press **Space** to play or pause. Selecting a clip exposes its properties in **Inspector**. **View → Reset workspace** restores the panels.
5. Choose **File → Save project** (Ctrl+S), choose a writable folder outside the app folder, and save a `.veproject`. Media is referenced rather than copied: keep the original files. **File → Open project…** reopens it.
6. Choose **File → Export video…** (Ctrl+E), select a destination, and use **Simple** for ordinary video. Wait for the compatibility check to finish, then click **Export**. **Cancel export** leaves an existing destination unchanged. A successful export replaces it atomically.

## Choose where the video ends

New projects and sequences use **Automatic end (fit to clips)** by default.
The dashed end marker follows the last clip after trims, moves, inserts,
deletes and track removal. Preview and export end at that same point.
Audio, titles, nested clips, and disabled or locked tracks all count.

For a specific end, click the timeline ruler at the desired position, then use
**Sequence → Set sequence end to playhead**. Alternatively, choose **Set sequence
end manually…** and enter a duration, including a longer end with an intentional
trailing gap. The field uses **View → Time display** units (frames or seconds).
Both commands switch to manual mode. Clips extending past the chosen end must be
trimmed or deleted first. Shortening clips then keeps your manual end; adding or
extending clips beyond it still grows the sequence to fit them.

Choose **Sequence → Automatic end (fit to clips)** to resume following clips.
The same commands are in the **Sequence end: Auto/Manual** menu above the timeline.
All end and mode changes support **Ctrl+Z**, redo and project save/reopen.
An empty automatic sequence has zero duration. Older projects open in manual
mode to preserve their saved duration; enable automatic mode to remove their tail.

## Simple or Advanced export

**Simple** offers Playback target, Video size, Picture quality and Include audio. **Desktop MP4** is the ordinary choice; broad compatibility restricts size/rate for older players. Both use software encoding with audio mixed to 48 kHz stereo. Automatic FPS follows **Primary Video**, or the sequence when no primary is selected. Select a timeline video and choose **Set Primary Video** to designate it; changing primary never moves or retimes clips.

Use **Advanced** for a named encoder or hardware policy, container, exact FPS fraction, custom dimensions, quality/preset, pixel storage, audio codec/bitrate and native encoder options. A compiled encoder can still be unavailable on your hardware. The dialog checks the exact choices before enabling Export; **Refresh encoder capabilities** reruns detection. Resolve the named conflict in the message if Export is disabled. **Export details…** shows diagnostics.

Switching tabs preserves settings. If Advanced settings cannot be represented by Simple, Simple explains this and disables its small control set. **Use compatible defaults** explicitly resets overrides and returns to Simple. Selecting 10-bit output stores the existing SDR composition in a 10-bit format; it does not make HDR.

## Missing media and interrupted work

Offline media remains listed. Use **Locate file…** on the source or **Media → Relink offline media from folder…** after moving originals. Relinking changes paths without changing edits.

Unsaved changes prompt on New/Open/Close. Autosave runs every two minutes; two prior explicit-save backups are retained. After a crash, reopening the app offers recoverable snapshots. Recover and save to a new project path before continuing. Autosave cannot recover edits made since the latest snapshot or repair lost source media.

Read [format and performance limits](supported-formats.md) before relying on a new format or playback device. Application source is GPL-3.0-or-later. The [license review](release-license-review.md) records the remaining dependency-source/build gaps disclosed with the public binary release.

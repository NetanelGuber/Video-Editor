# Session 13 — More capable audio mixing

Implemented 2026-10-08 in version 0.13.0, project schema 5. The editor now saves and mixes clip/track volume and pan automation, supports named audio buses, reports preview peaks and clipping, and includes mix metrics with exports. All 12 automated application suites passed; the collected reports are in [Session 13 evidence](../evidence/session-13/verification.json).

After completion, a follow-up corrected automation key time entry so it follows the selected **View → Time display** mode. The editor and test targets compile after this change. The earlier 12-suite report predates the correction; it has not been rerun. See the [correction record](../evidence/session-13/time-display-correction.json).

## Controls

Use **Track audio** in the timeline toolbar to edit the selected audio track or clip. Track properties include gain, pan, mute/solo, a bus route, and **Edit track automation…**. Clip properties include gain, pan, mute, fades, and **Edit clip automation…**. Automation key positions follow **View → Time display**: **Frames** shows sequence-frame positions; **Seconds** shows decimal seconds. Track times are measured from sequence start, while clip times are measured from original clip-content start. Values entered in seconds round to the nearest sequence frame; stored positions remain exact integer frames, and switching the display mode does not alter them. Keys also include values and outgoing **hold**, **linear**, or **eased** curves. Values between keys interpolate continuously; the first and last key values extend beyond their key positions. Volume keys range from 0 to 16 and multiply the clip, track, and bus gains. Pan keys range from -1 (left) to +1 (right) and add to static clip/track/bus pan before clamping.

Clip automation uses content-frame positions. Trimming the left side and splitting a clip advance the right-hand automation offset, so the remaining content keeps its original curve. Track keys use absolute sequence-frame positions. Keyframes and routing are saved with the project and covered by schema 4→5 migration.

Choose **Audio buses…** in the timeline toolbar to add or edit a bus's name, gain, pan, mute, and solo. A track routes to one bus or directly to Master. Buses feed Master directly; bus removal moves its tracks to Master. Track and bus solos can be active together; a muted track, clip, or bus remains silent. Buses provide shared static controls; automation is on clips and tracks.

## Mix and preview behavior

Source streams are decoded and converted with FFmpeg `libswresample` to 48 kHz, float, stereo before mixing. The mixer processes bounded 1024-sample blocks and uses the same compiled sequence description for preview and export. The Windows output requests WASAPI shared mode at 48 kHz float stereo with `AUTOCONVERTPCM` and default-quality sample-rate conversion. Its initialization asks for a 100 ms buffer; this is a request, not a measured end-to-end latency guarantee. Playback position follows the endpoint's submitted samples and padding.

Sequence clips occupy half-open frame intervals `[startFrame, startFrame + durationFrames)`. Their corresponding 48 kHz sample intervals are also half-open, so a clip contributes no samples at or beyond its end. Existing clip fade-in/fade-out durations remain frame-based and apply as linear-amplitude ramps. Gain/pan automation evaluates at each output sample using sequence-frame time. Center pan leaves both channels unchanged; moving pan attenuates the opposite channel with a cosine balance curve. The summed master signal is hard-clamped to [-1, 1], with no automatic normalization or limiter.

The status bar reports the rolling master peak in dBFS during preview, updates roughly every 100 ms, and shows a latched **CLIP** sample count when the unclamped mix exceeded full scale. The count persists through the playback run and resets when seeking or restarting the audio pipeline. Its tooltip includes track and bus peaks. A decoder or output error is shown in the status bar. Export completion reports the peak and clipped-sample count from the same mixer; missing media or unsupported audio prevents export at preflight, while a preview decode failure produces silence for the missing portion and reports the error.

Optional per-clip audio effects are deferred. This slice does not promise a constant-power pan law, loudness normalization, a dynamics processor, or a measured target-device latency.

## Verification and limits

Run `./scripts/Build.ps1`, `./scripts/Test-Application.ps1`, then `./scripts/Collect-Session13Evidence.ps1`. The audio suite checks automation evaluation, bus/track routing, clipping and meters, migration/round-trip, content-relative trim/split behavior, and native offscreen controls. The export suite saves/reopens the routed and automated reference mix, checks independently decoded audio against the shared mixer (reported RMS error 0.000106), and verifies output metadata and video frames. The UI suite drives the native status meter and verifies peak, clipping, and error display. Earlier suites continue to cover import, recovery, editing, effects, and the 4K cache profile.

The test run exercised the host's WASAPI shared-output API and recorded no preview underruns in its playback reference. That does not measure physical speaker/headphone latency or perceived audio quality. The automated native controls do not establish Windows display scaling or visual polish. Human visual/audibility acceptance and end-to-end output latency were not measured.

For a quick listening check, open `build/session-13/export-test-data/reference.veproject` in `build/session-13/Release/VideoEditor.exe` and play the sequence. It contains multiple audio tracks with bus routing, volume/pan automation, fades, and a title; the independently decoded export is `build/session-13/export-test-data/reference.mp4`.

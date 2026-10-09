# Changelog

## 1.0.1 — 2026-10-09

- New projects and sequences automatically end at the last clip after every edit.
  **Sequence → Automatic end (fit to clips)** enables this for older projects.
- Added **Set sequence end to playhead** and **Set sequence end manually…**,
  also available from **Sequence end: Auto/Manual** above the timeline. These
  switch to manual mode and support undo/redo and project save/reopen.
- Automatic end includes video, audio, titles and nested clips on every track, including
  disabled or locked tracks. Setting the end inside a clip asks you to trim or
  delete that clip first, preserving existing edits.
- Updated the Windows x64 portable package and executable version to 1.0.1.
- Project schema 12 stores the end mode; older projects retain their saved manual
  duration when opened. Save to a new file if retaining compatibility with 1.0.0.

## 1.0.0 — 2026-10-09

First versioned source release and portable Windows x64 draft package.

- Native C++20 / Qt Widgets workspace with searchable Project Media, a live
  Sequence viewer, multitrack timeline and Inspector.
- Frame-exact editing, undo/redo, titles, effects and keyframes, color/LUTs,
  speed changes, masks, chroma key and audio mixing.
- Nested sequences and camera switching; project save/recovery and offline
  media relinking.
- Simple and Advanced export, encoder compatibility probes, cached capability
  discovery and automatic FPS following Primary Video.
- Portable ZIP with app-local runtime DLLs, quick-start guide, notices, runtime
  inventories and SHA-256 checksum.
- GPL-3.0-or-later application source. Binary release remains a private GitHub
  draft while exact dependency corresponding-source/build material is resolved.

This release changes versioning and release preparation; project schema 11 and
the previously accepted editing behavior are retained. Earlier implementation
and acceptance records remain in `docs/session-*.md` and `evidence/`.

# Session 9 — Project recovery and media relinking

Version 0.9.0 implements recovery and moved-media workflows. The user's selected policy is **two-minute autosave and two backups**. No project schema changes are needed: schema 3 remains current, with the schema 1/2 migration examples retained.

**Status:** Complete, 2026-10-07. The user explicitly authorized completion; see the [acceptance record](../evidence/session-9/manual-acceptance.json). All nine automated suites pass, including 76 recovery/relink checks. Individual manual or visual results were not reported, so the evidence boundaries below remain unchanged.

## Save and recovery behavior

- While a project is dirty, the running autosave timer writes its latest model atomically every 120 seconds. Named and untitled projects both use the diagnostics app-data root's `recovery/` directory, normally `%LOCALAPPDATA%\LocalVideoTools\VideoEditor\recovery`. Autosave never writes the primary or backups and never clears the dirty indicator.
- Explicit saves retain `<project>.bak` and `<project>.bak.2`. The first is the preceding good primary; the second retains the preceding different backup. Repeated saves of unchanged bytes do not age out the older backup. Invalid existing primaries or backups stop the save and direct the user to a new location. Source-path and symbolic-link guards include both backup destinations.
- Startup offers **Recover project** when abandoned snapshots exist. The dialog orders them newest first and offers **Recover selected**, **Discard selected…**, and **Later**. **File → Recover project…** opens the same workflow later. Corrupt snapshots are listed with a diagnostic and remain available for inspection; recovering one fails transactionally.
- Recovery opens an unsaved document with its exact media paths, clips, audio/title properties, and settings. **Save project as…** chooses the reviewed project's primary destination. Recovery never automatically replaces the last good save. Backups and legacy `.autosave` sidecars can still be opened using **File → Open project…** and also open unsaved.
- Saving, discarding the document, or undoing back to the saved model cleans only that editor's known, validated, matching-ID snapshot. Save/Discard/Cancel applies to New/Open/Close. Cancel and failed saves retain both edits and recovery data. A corrupt snapshot is retained even when cleanup cannot validate it.
- Each owned snapshot has a live-session QLockFile lease. Recovery skips other running editors' snapshots, and terminated-process leases become recoverable. Starting another edit of the same project preserves any unresolved snapshot under a separate name. Discovery validates up to the 100 newest abandoned snapshots per dialog.

## Relinking moved originals

**File → Relink offline media from folder…** searches the selected folder and its subfolders in the background. Each offline record's original filename must have exactly one match. Missing and ambiguous names remain offline with a diagnostic in the inspector; use **Relink selected media…** to resolve ambiguity. Candidates are probed and checked against original stream format/timing, duplicate references, and every clip's source bounds before commit.

Successful replacements preserve stable media IDs, all sequences, clip placement/trims, title records, audio gain/fades, effects, and export settings. Each successful file replacement has an undo entry. A batch may retain successful replacements while skipping failures; cancellation keeps completed edits and rejects subsequent/stale results. The search stops safely at 10,000 files and asks for a smaller folder instead of accepting a potentially ambiguous partial search. Batches are limited to 1,000 records.

The media bin reports **Offline** for missing, non-file, or unreadable sources. Timeline clips show **OFFLINE ·** and a red fill; titles stay independent of media availability. A five-second refresh also detects availability changes while the project stays open. Sources are never moved or modified by relinking. Matching metadata is a compatibility check, not a cryptographic proof of file identity; select the moved originals.

## Automated verification and artifacts

Run `./scripts/Build.ps1` and `./scripts/Test-Application.ps1`. The default output is `build/session-9/Release/VideoEditor.exe`. All nine CTest suites must pass, including `project-recovery-relink`; collect the reports and binary/source hashes with `./scripts/Collect-Session9Evidence.ps1`.

`RecoveryTests` uses isolated temporary settings/app data and production MainWindow/widgets. It kills a child editor process after the production autosave timer has written a newer edit, starts a fresh child, drives the actual recovery prompt, and compares the multitrack project's complete model and last-good primary/backup bytes. It also verifies invalid recovery, Later and confirmed discard, live-session exclusion, autosave failures, unsaved New/Open/Close decisions, stale snapshot cleanup, retained backups, v1/v2/v3 migration examples, background recursive relinking, ambiguity, incompatible replacements, cancellation, exact undo/redo, and persistence.

Reports and offscreen rendered UI captures are in `evidence/session-9/`. A disposable edited offline project and moved-original copies are prepared at `fixtures/generated/session-9/recovery-offline.veproject` and `fixtures/generated/session-9/moved/`. These allow inspecting the workflow without touching real footage.

The evidence covers process termination, persistence, and production Qt behavior under the offscreen platform. It does not establish sudden power-loss durability, native Windows dialog appearance/display scaling, or long-session/large-project performance. No computer use was performed. No blanket human checklist is requested: the Session 9 acceptance behavior is automated; Windows appearance remains an explicitly unverified visual boundary.

## Limits

Changes made after the latest two-minute snapshot can be lost on forced termination. Unsaved snapshots are not silently pruned by age; resolve them with Save or Discard. Legacy snapshots beside project files are available through Open rather than a scan of arbitrary drives. Save locks serialize writes but do not merge independently edited projects or detect every external modification. Recovery validates bounded JSON synchronously; autosave serialization/file commits and periodic availability checks also run on the UI thread. Large-project and slow/network-storage profiling remains Session 10. Preview/export limits from Sessions 5–8 remain unchanged.

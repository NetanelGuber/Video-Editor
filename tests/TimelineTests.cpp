#include "timeline/Timeline.h"
#include "project/ProjectStore.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <cstdio>
#include <limits>
#include <stdexcept>

using namespace editor::timeline;
namespace {
QString id(int n) { return QStringLiteral("00000000-0000-4000-8000-%1").arg(n, 12, 10, QLatin1Char('0')); }
void write(const QString& path, const QByteArray& bytes) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) throw std::runtime_error("Cannot write report.");
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 3) return 2;
    QJsonArray checks, failures;
    auto check = [&](bool ok, const QString& name) {
        checks.append(name);
        if (!ok) { failures.append(name); std::fprintf(stderr, "FAIL: %s\n", qPrintable(name)); }
    };
    try {
        const auto fixture = ProjectStore::load(args[1]);
        check(bool(fixture), "Load synthetic editing fixture: " + fixture.error);
        if (!fixture) throw std::runtime_error("Fixture did not load.");
        const auto p = *fixture.project;
        const auto sid = p.activeSequenceId;
        const auto vid = id(30), audio = id(31), music = id(32), title = id(33), film = id(40), later = id(44), overlap = id(45);
        auto clip = p.sequences[0].tracks[0].clips[0];
        auto exec = [&](TimelineEditor& e, const Command& command, const QString& name) {
            const auto before = e.state();
            const auto bytes = serialize(before.project);
            const auto count = e.undoCount();
            const auto error = e.execute(command);
            check(error.isEmpty(), name + ": " + error);
            if (!error.isEmpty()) throw std::runtime_error((name + ": " + error).toStdString());
            const auto after = e.state();
            check(e.undoCount() == count + 1 && !e.undoLabel().isEmpty(), name + " creates one named command");
            check(validate(after.project).isEmpty() && after.project.media == before.project.media &&
                after.project.titles == before.project.titles, name + " preserves media/title records and valid references");
            check(e.undo() && e.state() == before && serialize(e.state().project) == bytes,
                name + " undo restores every typed field, serialized bytes, order and selection exactly");
            check(e.redo() && e.state() == after, name + " redo restores exact result");
        };
        auto reject = [&](TimelineEditor& e, const Command& command, const QString& name) {
            const auto before = e.state();
            const auto undo = e.undoCount(), redo = e.redoCount();
            const auto undoLabel = e.undoLabel(), redoLabel = e.redoLabel();
            const auto error = e.execute(command);
            check(!error.isEmpty() && e.state() == before && e.undoCount() == undo && e.redoCount() == redo &&
                e.undoLabel() == undoLabel && e.redoLabel() == redoLabel, name + " fails usefully without altering state/history: " + error);
        };
        auto straight = p;
        {
            TimelineEditor automatic(p);
            exec(automatic, SetAutomaticSequenceEnd{sid}, "Enable automatic end at fractional sequence FPS");
            qint64 expectedEnd = 0;
            for (const auto& track : automatic.state().project.sequences[0].tracks)
                for (const auto& c : track.clips) expectedEnd = std::max(expectedEnd, c.startFrame + c.durationFrames);
            check(automatic.state().project.sequences[0].automaticEnd && automatic.state().project.sequences[0].durationFrames == expectedEnd,
                "Automatic end counts title, video and audio clips without rounding fractional frame rates");
            exec(automatic, ResizeSequence{sid, expectedEnd + 30}, "Switch automatic sequence to a manual trailing gap");
            check(!automatic.state().project.sequences[0].automaticEnd && automatic.state().project.sequences[0].durationFrames == expectedEnd + 30,
                "Manual mode retains the exact requested gap");
            reject(automatic, ResizeSequence{sid, expectedEnd - 1}, "Manual end cannot cut the final clip");
        }
        straight.sequences[0].tracks[0].clips.removeLast(); // Separate non-overlapping lane for ripple checks.
        check(gaps(p.sequences[0], p.sequences[0].tracks[0]) == QVector<FrameRange>{{0, 10}, {90, 10}, {130, 50}},
            "Gap query unions overlapping clips and includes leading/trailing gaps");
        const auto hits = clipsAtFrame(p.sequences[0], 60);
        check(hits == QStringList{film, overlap, id(41)} && clipsAtFrame(p.sequences[0], 180).isEmpty() &&
            clipsAtFrame(p.sequences[0], -1).isEmpty(), "Frame query preserves track/vector order, respects enabled tracks and exclusive end");
        check(clipsAtFrame(p.sequences[0], 60, false) == QStringList{film, overlap, id(41), id(42)}, "Disabled clips remain available to edit/hit-test queries");
        check(!clipsAtFrame(p.sequences[0], 70).contains(film) && clipsAtFrame(p.sequences[0], 69).contains(film), "Clip out point is exclusive");
        check(framesToTicks(1, {30000, 1001}, {1, 90000}, Rounding::Nearest) == 3003 &&
            ticksToFrames(180180, {1, 90000}, {30000, 1001}, Rounding::Nearest) == 60, "Exact mixed-rate sequence/source time conversion");
        check(framesToTicks(1, {30000, 1001}, {1, 144000}, Rounding::Floor) == 4804 &&
            framesToTicks(1, {30000, 1001}, {1, 144000}, Rounding::Ceil) == 4805, "Fractional source tick rounding is explicit");
        check(scaleTime(1, 1, 2, Rounding::Nearest) == 1 && scaleTime(9007199254740993LL, 3, 3, Rounding::Nearest) == 9007199254740993LL,
            "Nearest ties upward and integer precision survives values above 2^53");
        check(!framesToTicks(-1, {30, 1}, {1, 1000}, Rounding::Nearest) &&
            !ticksToFrames(1, {0, 1}, {30, 1}, Rounding::Nearest) &&
            !framesToTicks(1, {60, 2}, {1, 1000}, Rounding::Nearest) &&
            !scaleTime(1, 1, 0, Rounding::Nearest) &&
            !scaleTime(std::numeric_limits<qint64>::max(), 2, 1, Rounding::Nearest), "Invalid rates/negative times/overflow are rejected");
        auto tiny = clip; tiny.sourceInTicks = 10; tiny.sourceDurationTicks = 3; tiny.durationFrames = 6;
        check(sourceBoundary(tiny, 1) == 11 && sourceBoundary(tiny, -1) == 9 && !sourceBoundary(tiny, -100),
            "Source boundary ties round away from zero for signed trim offsets; negative source rejected");
        check(!sourceBoundary(tiny, std::numeric_limits<qint64>::min()) &&
            !sourceBoundary(p.sequences[0].tracks[3].clips[0], 1), "Minimum signed offset and title source mapping rejected safely");

        auto primaryProject = editor::project::newProject("Primary video selection"); primaryProject.media = p.media;
        auto primaryClip = p.sequences[0].tracks[0].clips[0]; primaryClip.id = id(900); primaryClip.startFrame = 0;
        auto& primarySequence = primaryProject.sequences[0]; const auto primarySequenceId = primarySequence.id;
        const auto primaryVideoTrackId = primarySequence.tracks[0].id;
        TimelineEditor primaryEditor(primaryProject); const auto emptyPrimaryState = primaryEditor.state();
        check(primaryEditor.execute(InsertClip{primarySequenceId, primaryVideoTrackId, primaryClip}).isEmpty() &&
            primaryEditor.state().project.sequences[0].primaryVideoMediaId == primaryClip.mediaId &&
            primaryEditor.state().project.sequences[0].primaryVideoStreamIndex == primaryClip.streamIndex,
            "First inserted external video source becomes the sequence Primary Video");
        const auto insertedPrimaryState = primaryEditor.state();
        check(primaryEditor.undo() && primaryEditor.state() == emptyPrimaryState && primaryEditor.redo() && primaryEditor.state() == insertedPrimaryState,
            "Automatic Primary Video selection follows insertion undo and redo");
        QString alternateMediaId; int alternateStreamIndex = -1;
        for (const auto& media : p.media) for (const auto& stream : media.streams)
            if (stream.kind == "video" && (media.id != primaryClip.mediaId || stream.index != primaryClip.streamIndex)) {
                alternateMediaId = media.id; alternateStreamIndex = stream.index; break;
            }
        const auto beforePrimaryChange = primaryEditor.state().project;
        const auto* beforeSequence = &beforePrimaryChange.sequences[0];
        auto expectedPrimaryChange = beforePrimaryChange;
        expectedPrimaryChange.sequences[0].primaryVideoMediaId = alternateMediaId;
        expectedPrimaryChange.sequences[0].primaryVideoStreamIndex = alternateStreamIndex;
        const auto primaryChange = alternateMediaId.isEmpty() ? QString("No alternate source") :
            primaryEditor.execute(SetPrimaryVideo{primarySequenceId, alternateMediaId, alternateStreamIndex});
        check(!alternateMediaId.isEmpty() && primaryChange.isEmpty() && primaryEditor.state().project == expectedPrimaryChange &&
            primaryEditor.state().project.sequences[0].tracks == beforeSequence->tracks &&
            primaryEditor.state().project.sequences[0].frameRate == beforeSequence->frameRate &&
            primaryEditor.state().project.sequences[0].durationFrames == beforeSequence->durationFrames,
            "Changing Primary Video preserves all clips, tracks, sequence FPS and duration");
        check(primaryEditor.undo() && primaryEditor.state().project == beforePrimaryChange && primaryEditor.redo() &&
            primaryEditor.state().project == expectedPrimaryChange, "Explicit Primary Video change is undoable and redoable");
        const auto beforePrimaryClear = primaryEditor.state().project;
        const auto clearPrimaryError = primaryEditor.execute(SetPrimaryVideo{primarySequenceId, {}, -1});
        auto expectedPrimaryClear = beforePrimaryClear; expectedPrimaryClear.sequences[0].primaryVideoMediaId.clear(); expectedPrimaryClear.sequences[0].primaryVideoStreamIndex = -1;
        check(clearPrimaryError.isEmpty() && primaryEditor.state().project == expectedPrimaryClear &&
            primaryEditor.state().project.sequences[0].tracks == beforePrimaryClear.sequences[0].tracks,
            "Clearing Primary Video leaves all timeline edits intact and restores sequence-FPS fallback");
        check(primaryEditor.undo() && primaryEditor.state().project == beforePrimaryClear && primaryEditor.redo() &&
            primaryEditor.state().project == expectedPrimaryClear, "Clearing Primary Video is undoable and redoable");

        TimelineEditor e(p);
        exec(e, SetSelection{{sid, {vid}, {film}}}, "Select track and clip");
        reject(e, SetSelection{{sid, {}, {film, film}}}, "Duplicate selection");
        reject(e, SetSelection{{sid, {}, {id(999)}}}, "Missing selection");
        reject(e, SetSelection{{{}, {vid}, {}}}, "Selection without sequence");
        exec(e, SplitClip{sid, vid, film, 31, id(60), {id(61)}}, "Split mixed-rate video with opaque effect");
        const auto& splitClips = e.state().project.sequences[0].tracks[0].clips;
        check(splitClips[0].durationFrames == 21 && splitClips[0].sourceDurationTicks == 63063 &&
            splitClips[1].startFrame == 31 && splitClips[1].durationFrames == 39 && splitClips[1].sourceInTicks == 153153 &&
            splitClips[1].sourceDurationTicks == 117117 && splitClips[1].effects[0].parameters == clip.effects[0].parameters &&
            e.state().selection.clipIds == QStringList{film, id(60)}, "Split partitions both ranges without loss and selects both halves");
        check(e.state().project.sequences[0].durationFrames == 180, "Split preserves sequence duration");
        reject(e, SplitClip{sid, vid, film, 10, id(70), {id(71)}}, "Split at in point");
        reject(e, SplitClip{sid, vid, film, 31, id(70), {id(71)}}, "Split at out point");
        reject(e, SplitClip{sid, vid, film, 20, id(70), {}}, "Split missing effect IDs");
        reject(e, SplitClip{sid, vid, film, 20, later, {id(71)}}, "Split duplicate clip ID");
        reject(e, SplitClip{sid, vid, film, 20, id(70), {clip.effects[0].id}}, "Split duplicate effect ID");
        exec(e, DeleteClip{sid, vid, id(60)}, "Delete selected split half");
        check(e.state().selection.clipIds == QStringList{film}, "Deletion prunes only deleted selection IDs");

        TimelineEditor trim(p);
        exec(trim, TrimClip{sid, vid, film, 20, 40}, "Trim both ends");
        const auto trimmed = trim.state().project.sequences[0].tracks[0].clips[0];
        check(trimmed.sourceInTicks == 120120 && trimmed.sourceDurationTicks == 120120 && trimmed.startFrame == 20 &&
            trimmed.durationFrames == 40 && trimmed.effects == clip.effects, "Trim updates exact source interval and preserves clip/effects identity");
        trim.undo();
        exec(trim, TrimClip{sid, vid, film, 0, 80}, "Extend both source edges");
        check(trim.state().project.sequences[0].tracks[0].clips[0].sourceInTicks == 60060 &&
            trim.state().project.sequences[0].tracks[0].clips[0].sourceDurationTicks == 240240, "Extension uses stored range ratio without floating point");
        reject(trim, TrimClip{sid, vid, film, -1, 10}, "Negative frame start");
        reject(trim, TrimClip{sid, vid, film, 0, 0}, "Empty clip duration");
        reject(trim, TrimClip{sid, vid, film, 0, 100000}, "Trim beyond available source");
        reject(trim, TrimClip{sid, vid, film, 1, 10, EditMode::Ripple}, "Ripple in-point trim");
        exec(trim, TrimClip{sid, title, id(43), 10, 45}, "Trim title timing");
        check(trim.state().project.sequences[0].tracks[3].clips[0].sourceDurationTicks == 0, "Titles keep zero source fields");
        exec(trim, SplitClip{sid, title, id(43), 20, id(72), {}}, "Split title");
        exec(trim, SplitClip{sid, music, id(42), 41, id(73), {}}, "Split 44.1 kHz audio with fractional source ticks");
        const auto& musicClips = trim.state().project.sequences[0].tracks[2].clips;
        check(musicClips[0].sourceDurationTicks == 16186 && musicClips[1].sourceInTicks == 16186 &&
            musicClips[0].sourceDurationTicks + musicClips[1].sourceDurationTicks == 176576, "Audio split shares one rounded boundary without a gap or duplicated source ticks");

        TimelineEditor tracks(p);
        Track added; added.id = id(80); added.name = "Video 2";
        exec(tracks, AddTrack{sid, added, 1}, "Add ordered video track");
        exec(tracks, MoveClip{sid, vid, film, added.id, 175}, "Move clip across tracks beyond sequence end");
        check(tracks.state().project.sequences[0].durationFrames == 235 &&
            tracks.state().project.sequences[0].tracks[1].clips[0].sourceInTicks == clip.sourceInTicks &&
            tracks.state().project.sequences[0].tracks[1].clips[0].effects == clip.effects, "Move grows sequence and preserves source/effects");
        reject(tracks, MoveClip{sid, added.id, film, audio, 0}, "Move to incompatible track");
        exec(tracks, SetTrackEnabled{sid, added.id, false}, "Disable track");
        exec(tracks, MoveClip{sid, added.id, film, added.id, 0}, "Edit a disabled track");
        exec(tracks, SetTrackLocked{sid, added.id, true}, "Lock track");
        reject(tracks, DeleteClip{sid, added.id, film}, "Delete from locked track");
        reject(tracks, TrimClip{sid, added.id, film, 0, 20}, "Trim locked track");
        reject(tracks, SplitClip{sid, added.id, film, 10, id(90), {id(91)}}, "Split locked track");
        reject(tracks, MoveClip{sid, added.id, film, vid, 0}, "Move out of locked track");
        reject(tracks, MoveClip{sid, vid, later, added.id, 0}, "Move into locked track");
        reject(tracks, InsertClip{sid, added.id, clip}, "Insert into locked track");
        reject(tracks, CloseGap{sid, added.id, 60, 10}, "Ripple locked track");
        reject(tracks, RemoveTrack{sid, added.id}, "Remove locked track");
        reject(tracks, SetTrackEnabled{sid, added.id, true}, "Change enabled state on locked track");
        exec(tracks, SetTrackLocked{sid, added.id, false}, "Unlock track");
        exec(tracks, SetSelection{{sid, {added.id}, {film}}}, "Select moved clip");
        exec(tracks, RemoveTrack{sid, added.id}, "Remove populated track");
        check(tracks.state().selection.trackIds.isEmpty() && tracks.state().selection.clipIds.isEmpty() &&
            tracks.state().project.sequences[0].durationFrames == 235, "Removing track prunes selection and retains explicit sequence end");
        exec(tracks, ResizeSequence{sid, 150}, "Set sequence end explicitly");
        reject(tracks, ResizeSequence{sid, 100}, "Shorten sequence through clips");
        reject(tracks, ResizeSequence{sid, -1}, "Negative sequence duration");
        reject(tracks, AddTrack{sid, added, 99}, "Bad track index");
        added.id = vid;
        reject(tracks, AddTrack{sid, added}, "Duplicate track ID");
        added.id = id(80); added.kind = "unknown";
        reject(tracks, AddTrack{sid, added}, "Unsupported track kind");
        added.kind = "audio";
        exec(tracks, AddTrack{sid, added}, "Add audio track");
        reject(tracks, SetTrackLocked{id(999), vid, true}, "Unknown sequence");
        reject(tracks, SetTrackLocked{sid, id(999), true}, "Unknown track");
        reject(tracks, DeleteClip{sid, vid, id(999)}, "Unknown clip");

        TimelineEditor ripple(straight);
        auto inserted = clip; inserted.id = id(100); inserted.effects.clear(); inserted.startFrame = 70; inserted.durationFrames = 20;
        const auto otherTracks = ripple.state().project.sequences[0].tracks;
        exec(ripple, InsertClip{sid, vid, inserted, EditMode::Ripple}, "Ripple insert at boundary");
        check(ripple.state().project.sequences[0].tracks[0].clips[1].startFrame == 120 &&
            ripple.state().project.sequences[0].tracks[1] == otherTracks[1] && ripple.state().project.sequences[0].tracks[2] == otherTracks[2],
            "Ripple insertion preserves existing gap and every other track");
        exec(ripple, DeleteClip{sid, vid, inserted.id, EditMode::Ripple}, "Ripple delete inserted clip");
        check(ripple.state().project == straight, "Ripple insert/delete restores original model including gaps");
        exec(ripple, TrimClip{sid, vid, film, 10, 50, EditMode::Ripple}, "Ripple shorten out point");
        check(ripple.state().project.sequences[0].tracks[0].clips[1].startFrame == 90 &&
            ripple.state().project.sequences[0].durationFrames == 180, "Ripple shortening shifts later clip by exact delta and retains sequence end");
        exec(ripple, TrimClip{sid, vid, film, 10, 70, EditMode::Ripple}, "Ripple extend out point");
        check(ripple.state().project.sequences[0].tracks[0].clips[1].startFrame == 110, "Ripple extension shifts later clip by exact delta");
        exec(ripple, CloseGap{sid, vid, 80, 30}, "Close explicit gap");
        check(ripple.state().project.sequences[0].tracks[0].clips[1].startFrame == 80, "Close gap places following clip at the gap start");
        reject(ripple, CloseGap{sid, vid, 79, 2}, "Close gap over clip edge");
        reject(ripple, CloseGap{sid, vid, 179, 2}, "Gap beyond sequence end");
        exec(ripple, MoveClip{sid, vid, film, vid, 100, EditMode::Ripple}, "Ripple move using post-removal destination");
        check(ripple.state().project.sequences[0].tracks[0].clips[0].startFrame == 100 &&
            ripple.state().project.sequences[0].tracks[0].clips[1].startFrame == 10,
            "Ripple move removes old occupied range before inserting at requested frame");
        Track destination; destination.id = id(101); destination.name = "Ripple destination";
        exec(ripple, AddTrack{sid, destination}, "Add ripple destination");
        exec(ripple, MoveClip{sid, vid, later, destination.id, 0, EditMode::Ripple}, "Ripple move across same-kind tracks");
        check(ripple.state().project.sequences[0].tracks[0].clips[0].startFrame == 70 &&
            ripple.state().project.sequences[0].tracks.last().clips[0].startFrame == 0,
            "Cross-track ripple closes source range and inserts destination range without touching other tracks");

        TimelineEditor overlaps(p);
        auto normalInsert = inserted; normalInsert.startFrame = 20;
        exec(overlaps, InsertClip{sid, vid, normalInsert}, "Normal placement may overlap clips");
        check(overlaps.state().project.sequences[0].tracks[0].clips.last().startFrame == 20 &&
            overlaps.state().project.sequences[0].tracks[0].clips[1].startFrame == 100, "Normal placement leaves all existing positions intact");
        reject(overlaps, DeleteClip{sid, vid, film, EditMode::Ripple}, "Ripple removal intersecting overlap");
        auto badInsert = inserted; badInsert.id = id(102); badInsert.startFrame = 60;
        reject(overlaps, InsertClip{sid, vid, badInsert, EditMode::Ripple}, "Ripple insertion inside existing clips");
        reject(overlaps, TrimClip{sid, vid, film, 10, 45, EditMode::Ripple}, "Ripple trim intersecting another clip");
        reject(overlaps, MoveClip{sid, vid, later, vid, 60, EditMode::Ripple}, "Ripple move failing after source removal is fully transactional");
        badInsert.startFrame = 140; badInsert.mediaId = id(999);
        reject(overlaps, InsertClip{sid, vid, badInsert}, "Missing media reference on insertion");
        badInsert.mediaId = clip.mediaId; badInsert.id = film;
        reject(overlaps, InsertClip{sid, vid, badInsert}, "Duplicate clip ID on insertion");
        auto sparse = straight;
        sparse.sequences[0].tracks[0].clips[0].sourceDurationTicks = 1;
        TimelineEditor sparseEditor(sparse);
        reject(sparseEditor, SplitClip{sid, vid, film, 11, id(103), {id(104)}}, "Split into empty source tick range");
        reject(sparseEditor, TrimClip{sid, vid, film, 10, 1}, "Trim into empty source tick range");
        auto atZero = straight; atZero.sequences[0].tracks[0].clips[0].sourceInTicks = 0;
        TimelineEditor zeroEditor(atZero);
        reject(zeroEditor, TrimClip{sid, vid, film, 0, 70}, "Extend in point before available source");
        auto huge = inserted; huge.id = id(105); huge.startFrame = std::numeric_limits<qint64>::max();
        reject(overlaps, InsertClip{sid, vid, huge}, "Clip end integer overflow");
        auto maxProject = straight;
        maxProject.sequences[0].durationFrames = std::numeric_limits<qint64>::max();
        maxProject.sequences[0].tracks[0].clips[1].startFrame = std::numeric_limits<qint64>::max() - 30;
        TimelineEditor maxEditor(maxProject);
        reject(maxEditor, InsertClip{sid, vid, inserted, EditMode::Ripple}, "Ripple shift integer overflow");
        reject(maxEditor, MoveClip{sid, vid, film, vid, std::numeric_limits<qint64>::max()}, "Move end integer overflow");
        exec(maxEditor, MoveClip{sid, vid, film, vid, 9007199254740993LL}, "Move above floating-point exact integer range");
        check(maxEditor.state().project.sequences[0].tracks[0].clips[0].startFrame == 9007199254740993LL,
            "Frame placement above 2^53 remains exact");

        TimelineEditor history(straight);
        exec(history, MoveClip{sid, vid, film, vid, 11}, "First history move");
        exec(history, MoveClip{sid, vid, film, vid, 12}, "Second history move");
        history.undo();
        reject(history, MoveClip{sid, vid, film, audio, 0}, "Rejected edit preserves pending redo branch");
        const auto beforeNoOp = history.state();
        check(history.execute(MoveClip{sid, vid, film, vid, 11}).isEmpty() && history.state() == beforeNoOp && history.canRedo() &&
            history.undoCount() == 1, "No-op preserves redo branch and does not create history");
        exec(history, SetTrackEnabled{sid, vid, false}, "Branching command after undo");
        check(!history.canRedo(), "Successful new command discards stale redo branch");
        while (history.undo()) {}
        check(history.state().project == straight && !history.canUndo() && history.undoLabel().isEmpty(), "Undo to beginning is bounded and exact");
        while (history.redo()) {}
        check(!history.redo() && history.redoLabel().isEmpty(), "Redo to end is bounded");
        auto invalid = straight; invalid.name.clear(); invalid.id.clear();
        const auto beforeReplace = history.state();
        check(!history.replaceProject(invalid).isEmpty() && history.state() == beforeReplace && history.canUndo(), "Invalid document replacement preserves model/history");
        check(history.replaceProject(p).isEmpty() && history.state() == State{p, {}} && !history.canUndo() && !history.canRedo(),
            "Valid document replacement clears selection and both history branches");
        TimelineEditor bounded(straight, 2);
        bounded.execute(MoveClip{sid, vid, film, vid, 11}); bounded.execute(MoveClip{sid, vid, film, vid, 12}); bounded.execute(MoveClip{sid, vid, film, vid, 13});
        check(bounded.undoCount() == 2 && bounded.undo() && bounded.undo() && !bounded.undo() &&
            bounded.state().project.sequences[0].tracks[0].clips[0].startFrame == 11, "History limit retains latest complete transactions and coherent base state");

        QVector<Command> commands{SetSelection{{sid, {vid}, {film}}}, SplitClip{sid, vid, film, 31, id(110), {id(111)}},
            TrimClip{sid, vid, id(110), 35, 30}, MoveClip{sid, vid, later, vid, 145},
            SetTrackEnabled{sid, music, true}, DeleteClip{sid, title, id(43)}, CloseGap{sid, vid, 70, 20}};
        TimelineEditor first(straight), second(straight);
        QVector<State> states{first.state()};
        for (const auto& command : commands) {
            const auto a = first.execute(command), b = second.execute(command);
            check(a.isEmpty() && b.isEmpty() && first.state() == second.state(), "Same commands/IDs produce identical deterministic state");
            states.append(first.state());
        }
        const auto edited = first.state().project;
        for (qsizetype i = states.size() - 1; i > 0; --i) check(first.undo() && first.state() == states[i - 1], "Mixed command sequence undo matches complete previous state");
        for (qsizetype i = 1; i < states.size(); ++i) check(first.redo() && first.state() == states[i], "Mixed command sequence redo matches complete next state");
        for (int i = 0; i < 100; ++i) { while (first.undo()) {} while (first.redo()) {} }
        check(first.state().project == edited, "100 whole-history undo/redo cycles preserve exact edited result");
        auto roundtrip = edited;
        for (int i = 0; i < 100; ++i) {
            const auto loaded = deserialize(serialize(roundtrip));
            if (!loaded) throw std::runtime_error(loaded.error.toStdString());
            roundtrip = *loaded.project;
        }
        check(roundtrip == edited, "100 serialization cycles preserve mixed-rate frame edits, IDs, source ticks, effect parameters");
        QDir().mkpath(args[2]);
        const auto savePath = QDir(args[2]).filePath("edited-roundtrip.veproject");
        for (int i = 0; i < 50; ++i) {
            const auto error = ProjectStore::save(roundtrip, savePath);
            const auto reopened = ProjectStore::load(savePath);
            if (!error.isEmpty() || !reopened) throw std::runtime_error((error + reopened.error).toStdString());
            roundtrip = *reopened.project;
        }
        check(roundtrip == edited && serialize(roundtrip) == serialize(edited), "50 real atomic save/reopen cycles preserve exact duration, placement, source intervals and media references");
        // Verify the edit continuation from a reopened fractional-tick audio clip is identical.
        const auto audioEdited = trim.state().project;
        auto audioReopened = deserialize(serialize(audioEdited));
        TimelineEditor continued(audioEdited), reopenedEditor(*audioReopened.project);
        const Command nextAudioTrim = TrimClip{sid, music, id(73), 42, 100};
        check(continued.execute(nextAudioTrim).isEmpty() && reopenedEditor.execute(nextAudioTrim).isEmpty() &&
            continued.state() == reopenedEditor.state(), "Post-reopen source-boundary edits match the in-memory result exactly");
    } catch (const std::exception& error) {
        failures.append(QString::fromUtf8(error.what())); std::fprintf(stderr, "%s\n", error.what());
    }
    QDir().mkpath(args[2]);
    const QJsonObject report{{"passed", failures.isEmpty()}, {"evidence", "native deterministic model and real project filesystem"},
        {"checkCount", checks.size()}, {"checks", checks}, {"failures", failures}};
    write(QDir(args[2]).filePath("timeline-result.json"), QJsonDocument(report).toJson());
    std::printf("Timeline checks: %lld, failures: %lld\n", static_cast<long long>(checks.size()), static_cast<long long>(failures.size()));
    return failures.isEmpty() ? 0 : 1;
}

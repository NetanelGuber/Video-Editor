#pragma once
#include "project/Project.h"
#include <variant>

namespace editor::timeline {
using namespace editor::project;
enum class EditMode { Normal, Ripple };
struct Selection {
    QString sequenceId;
    QStringList trackIds, clipIds;
    bool operator==(const Selection&) const = default;
};
struct State {
    Project project;
    Selection selection; // Editor state: undoable, but not saved in the project document.
    bool operator==(const State&) const = default;
};
struct AddTrack { QString sequenceId; Track track; qsizetype index = -1; };
struct RemoveTrack { QString sequenceId, trackId; };
struct SetTrackEnabled { QString sequenceId, trackId; bool enabled = true; };
struct SetTrackLocked { QString sequenceId, trackId; bool locked = true; };
struct SetTrackAudio { QString sequenceId, trackId; double gain = 1; bool muted = false, solo = false; double pan = 0; QString audioBusId; AudioAutomation automation; };
struct SetClipAudio { QString sequenceId, trackId, clipId; double gain = 1; bool muted = false; qint64 fadeInFrames = 0, fadeOutFrames = 0; double pan = 0; AudioAutomation automation; };
struct UpsertAudioBus { QString sequenceId; AudioBus bus; };
struct RemoveAudioBus { QString sequenceId, busId; };
struct SetClipEffects { QString sequenceId, trackId, clipId; QVector<Effect> effects; };
struct SetSelection { Selection selection; };
struct InsertClip { QString sequenceId, trackId; Clip clip; EditMode mode = EditMode::Normal; };
struct DeleteClip { QString sequenceId, trackId, clipId; EditMode mode = EditMode::Normal; };
// IDs are supplied by the caller once, so replay and redo never generate new identities.
struct SplitClip { QString sequenceId, trackId, clipId; qint64 frame = 0; QString rightClipId; QStringList rightEffectIds; };
struct TrimClip { QString sequenceId, trackId, clipId; qint64 startFrame = 0, durationFrames = 1; EditMode mode = EditMode::Normal; };
// Ripple destination is measured AFTER removing the old interval on the source track.
struct MoveClip { QString sequenceId, trackId, clipId, destinationTrackId; qint64 startFrame = 0; EditMode mode = EditMode::Normal; };
struct CloseGap { QString sequenceId, trackId; qint64 startFrame = 0, durationFrames = 1; };
struct ResizeSequence { QString sequenceId; qint64 durationFrames = 0; };
struct SetAutomaticSequenceEnd { QString sequenceId; };
struct UpsertMedia { Media media; };
struct UpsertTitle { Title title; };
struct RenameProject { QString name; };
struct SetExportSettings { ExportSettings settings; };
struct SetPrimaryVideo { QString sequenceId, mediaId; int streamIndex = -1; };
struct AddSequence { Sequence sequence; };
struct ActivateSequence { QString sequenceId; };
struct RemoveSequence { QString sequenceId; };
struct SetMulticam { QString sequenceId; std::optional<Multicam> group; };
struct SwitchCamera { QString sequenceId, trackId; qint64 frame = 0; };
using Command = std::variant<AddTrack, RemoveTrack, SetTrackEnabled, SetTrackLocked, SetTrackAudio, SetClipAudio, UpsertAudioBus, RemoveAudioBus, SetClipEffects, SetSelection,
    InsertClip, DeleteClip, SplitClip, TrimClip, MoveClip, CloseGap, ResizeSequence, UpsertMedia, UpsertTitle, RenameProject, SetExportSettings,
    AddSequence, ActivateSequence, RemoveSequence, SetMulticam, SwitchCamera, SetPrimaryVideo, SetAutomaticSequenceEnd>;
struct FrameRange {
    qint64 startFrame = 0, durationFrames = 0;
    bool operator==(const FrameRange&) const = default;
};
QVector<FrameRange> gaps(const Sequence& sequence, const Track& track);
QStringList clipsAtFrame(const Sequence& sequence, qint64 frame, bool enabledOnly = true);
// Offset is relative to the clip's timeline start, including negative offsets for trim extensions.
std::optional<qint64> sourceBoundary(const Clip& clip, qint64 frameOffset);

class TimelineEditor {
public:
    explicit TimelineEditor(Project project, qsizetype historyLimit = 128);
    const State& state() const { return state_; }
    QString replaceProject(Project project); // Validated document switch: clears selection and history.
    QString execute(const Command& command); // Empty on success (including no-op); failure is transactional.
    QString executeBatch(const QVector<Command>& commands, const QString& label); // One atomic gesture/undo entry.
    bool canUndo() const { return cursor_ > 0; }
    bool canRedo() const { return cursor_ < history_.size(); }
    bool undo();
    bool redo();
    QString undoLabel() const;
    QString redoLabel() const;
    qsizetype undoCount() const { return cursor_; }
    qsizetype redoCount() const { return history_.size() - cursor_; }
private:
    struct Entry { QString label; State before, after; };
    State state_;
    QVector<Entry> history_;
    qsizetype cursor_ = 0, historyLimit_;
};
}

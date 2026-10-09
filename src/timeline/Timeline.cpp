#include "Timeline.h"
#include "project/Effects.h"
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace editor::timeline {
namespace {
constexpr auto MaxTime = std::numeric_limits<qint64>::max();
void require(bool ok, const QString& message) {
    if (!ok) throw std::invalid_argument(message.toStdString());
}
qint64 end(qint64 start, qint64 duration) {
    require(start >= 0 && duration > 0 && duration <= MaxTime - start,
        "Timeline range must have a nonnegative start and positive duration without overflow.");
    return start + duration;
}
qint64 end(const Clip& c) { return end(c.startFrame, c.durationFrames); }
Sequence& sequence(Project& p, const QString& id) {
    for (auto& s : p.sequences) if (s.id == id) return s;
    throw std::invalid_argument("Sequence ID does not exist.");
}
Track& track(Sequence& s, const QString& id, bool editing = true) {
    for (auto& t : s.tracks) if (t.id == id) {
        require(!editing || !t.locked, "Track is locked; unlock it before editing.");
        return t;
    }
    throw std::invalid_argument("Track ID does not exist.");
}
qsizetype clipIndex(const Track& t, const QString& id) {
    for (qsizetype i = 0; i < t.clips.size(); ++i) if (t.clips[i].id == id) return i;
    throw std::invalid_argument("Clip ID does not exist on the requested track.");
}
void grow(Sequence& s) {
    for (const auto& t : s.tracks) for (const auto& c : t.clips)
        s.durationFrames = std::max(s.durationFrames, end(c));
}
// A ripple cut cannot ambiguously cut through an overlapping clip. Fully later clips shift intact.
void removeInterval(Track& t, qint64 start, qint64 duration) {
    const auto stop = end(start, duration);
    for (auto& c : t.clips) {
        require(end(c) <= start || c.startFrame >= stop,
            "Ripple removal intersects another clip; resolve the overlap first.");
        if (c.startFrame >= stop) c.startFrame -= duration;
    }
}
void insertInterval(Track& t, qint64 start, qint64 duration) {
    end(start, duration);
    for (auto& c : t.clips) {
        require(!(c.startFrame < start && end(c) > start),
            "Ripple insertion cuts through another clip; choose a gap or clip boundary.");
        if (c.startFrame >= start) {
            require(end(c) <= MaxTime - duration, "Ripple shift exceeds the supported frame range.");
            c.startFrame += duration;
        }
    }
}
void pruneSelection(State& state) {
    auto& sel = state.selection;
    if (sel.sequenceId.isEmpty()) return;
    auto& s = sequence(state.project, sel.sequenceId);
    QSet<QString> tracks, clips;
    for (const auto& t : s.tracks) {
        tracks.insert(t.id);
        for (const auto& c : t.clips) clips.insert(c.id);
    }
    sel.trackIds.removeIf([&](const auto& id) { return !tracks.contains(id); });
    sel.clipIds.removeIf([&](const auto& id) { return !clips.contains(id); });
}
void validateSelection(State& state) {
    auto& sel = state.selection;
    require(!sel.sequenceId.isEmpty() || (sel.trackIds.isEmpty() && sel.clipIds.isEmpty()),
        "Selection requires a sequence ID.");
    if (sel.sequenceId.isEmpty()) return;
    auto& s = sequence(state.project, sel.sequenceId);
    QSet<QString> seen;
    for (const auto& id : sel.trackIds) {
        track(s, id, false);
        require(!seen.contains(id), "Selection contains duplicate IDs."); seen.insert(id);
    }
    for (const auto& id : sel.clipIds) {
        bool found = false;
        for (const auto& t : s.tracks) for (const auto& c : t.clips) found |= c.id == id;
        require(found && !seen.contains(id), "Selection contains missing or duplicate clip IDs."); seen.insert(id);
    }
}
void trimSource(Clip& c, qint64 start, qint64 duration) {
    const auto stop = end(start, duration);
    if (c.kind != "title") {
        const auto in = sourceBoundary(c, start - c.startFrame);
        const auto out = sourceBoundary(c, stop - c.startFrame);
        require(in && out && *out > *in,
            "Trim exceeds source time bounds or rounds to an empty source interval.");
        c.sourceInTicks = *in; c.sourceDurationTicks = *out - *in;
    }
    const auto effectError = trimEffects(c, start - c.startFrame, duration);
    require(effectError.isEmpty(), effectError);
    if (!c.audioAutomation.volume.isEmpty() || !c.audioAutomation.pan.isEmpty()) {
        const auto offset = start - c.startFrame;
        require((offset >= 0 || c.audioAutomation.timeOffsetFrames >= std::numeric_limits<qint64>::min() - offset) &&
            (offset <= 0 || c.audioAutomation.timeOffsetFrames <= std::numeric_limits<qint64>::max() - offset),
            "Audio automation content origin exceeds the supported time range.");
        c.audioAutomation.timeOffsetFrames += offset;
    }
    c.startFrame = start; c.durationFrames = duration;
    c.fadeInFrames = std::min(c.fadeInFrames, duration); c.fadeOutFrames = std::min(c.fadeOutFrames, duration);
}
QString apply(State& state, const Command& command) {
    return std::visit([&](const auto& op) -> QString {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T, AddSequence>) {
            state.project.sequences.append(op.sequence); return "Add sequence";
        } else if constexpr (std::is_same_v<T, ActivateSequence>) {
            sequence(state.project, op.sequenceId);
            state.project.activeSequenceId = op.sequenceId;
            state.selection = {op.sequenceId, {}, {}}; return "Open sequence";
        } else if constexpr (std::is_same_v<T, RemoveSequence>) {
            require(op.sequenceId != state.project.activeSequenceId, "Open a different sequence before deleting this one.");
            sequence(state.project, op.sequenceId);
            for (const auto& s : state.project.sequences) for (const auto& t : s.tracks) for (const auto& c : t.clips)
                require(c.sequenceId != op.sequenceId, "Sequence is nested in another sequence; remove its instances first.");
            state.project.sequences.removeIf([&](const auto& s) { return s.id == op.sequenceId; });
            if (state.selection.sequenceId == op.sequenceId) state.selection = {};
            return "Delete sequence";
        } else if constexpr (std::is_same_v<T, SetSelection>) {
            state.selection = op.selection; validateSelection(state); return "Select";
        } else if constexpr (std::is_same_v<T, UpsertMedia>) {
            for (auto& m : state.project.media) if (m.id == op.media.id) { m = op.media; return "Update media"; }
            state.project.media.append(op.media); return "Import media";
        } else if constexpr (std::is_same_v<T, UpsertTitle>) {
            for (const auto& s : state.project.sequences) for (const auto& t : s.tracks) if (t.locked)
                for (const auto& c : t.clips) if (c.titleId == op.title.id) {
                    for (const auto& title : state.project.titles) if (title.id == op.title.id)
                        require(title == op.title, "Title is used on a locked track; unlock it before editing.");
                }
            for (auto& title : state.project.titles) if (title.id == op.title.id) { title = op.title; return "Edit title"; }
            state.project.titles.append(op.title); return "Add title";
        } else if constexpr (std::is_same_v<T, RenameProject>) {
            state.project.name = op.name; return "Name project";
        } else if constexpr (std::is_same_v<T, SetExportSettings>) {
            state.project.exportSettings = op.settings; return "Export settings";
        } else {
            auto& s = sequence(state.project, op.sequenceId);
            if constexpr (std::is_same_v<T, SetMulticam>) {
                for (const auto& t : s.tracks) require(!t.locked, "Unlock tracks before changing the camera group.");
                s.multicam = op.group; return op.group ? "Group cameras" : "Ungroup cameras";
            } else if constexpr (std::is_same_v<T, SwitchCamera>) {
                require(s.multicam && s.multicam->cameraTrackIds.contains(op.trackId), "Select a camera in this sequence.");
                require(op.frame >= 0 && op.frame < s.durationFrames, "Camera switch must be inside the sequence.");
                for (const auto& t : s.tracks) if (s.multicam->cameraTrackIds.contains(t.id)) require(!t.locked, "Unlock camera tracks before switching.");
                auto& cuts = s.multicam->cuts;
                const auto found = std::lower_bound(cuts.begin(), cuts.end(), op.frame, [](const auto& cut, auto frame) { return cut.frame < frame; });
                if (found != cuts.end() && found->frame == op.frame) found->trackId = op.trackId;
                else cuts.insert(found, CameraCut{op.frame, op.trackId});
                // Keep a minimal hold curve, retaining all later intentional switches.
                for (qsizetype i = cuts.size() - 1; i > 0; --i) if (cuts[i].trackId == cuts[i - 1].trackId) cuts.removeAt(i);
                return "Switch camera";
            } else if constexpr (std::is_same_v<T, SetPrimaryVideo>) {
                if (op.mediaId.isEmpty()) {
                    require(op.streamIndex == -1, "Clear primary video with stream index -1.");
                    s.primaryVideoMediaId.clear(); s.primaryVideoStreamIndex = -1;
                    return "Clear primary video";
                }
                const Media* media = nullptr;
                for (const auto& candidate : state.project.media) if (candidate.id == op.mediaId) media = &candidate;
                require(media, "Select imported video media as the primary video.");
                bool found = false;
                for (const auto& stream : media->streams) if (stream.index == op.streamIndex && stream.kind == "video") found = true;
                require(found, "Select a video stream as the primary video.");
                s.primaryVideoMediaId = op.mediaId; s.primaryVideoStreamIndex = op.streamIndex;
                return "Set primary video";
            } else if constexpr (std::is_same_v<T, AddTrack>) {
                require(op.track.clips.isEmpty(), "Add track requires an empty track; insert clips separately.");
                const auto index = op.index == -1 ? s.tracks.size() : op.index;
                require(index >= 0 && index <= s.tracks.size(), "Track insertion index is out of bounds.");
                s.tracks.insert(index, op.track); return "Add track";
            } else if constexpr (std::is_same_v<T, UpsertAudioBus>) {
                require(!op.bus.name.trimmed().isEmpty() && op.bus.name.size() <= 256, "Audio bus name must contain 1–256 characters.");
                for (auto& bus : s.audioBuses) if (bus.id == op.bus.id) { bus = op.bus; return "Edit audio bus"; }
                require(s.audioBuses.size() < 128, "A sequence can contain at most 128 audio buses.");
                s.audioBuses.append(op.bus); return "Add audio bus";
            } else if constexpr (std::is_same_v<T, RemoveAudioBus>) {
                const auto found = std::find_if(s.audioBuses.begin(), s.audioBuses.end(), [&](const auto& bus) { return bus.id == op.busId; });
                require(found != s.audioBuses.end(), "Audio bus does not exist.");
                s.audioBuses.erase(found);
                for (auto& t : s.tracks) if (t.audioBusId == op.busId) t.audioBusId.clear();
                return "Remove audio bus";
            } else if constexpr (std::is_same_v<T, ResizeSequence>) {
                require(op.durationFrames >= 0, "Sequence duration cannot be negative.");
                for (const auto& t : s.tracks) for (const auto& c : t.clips)
                    require(end(c) <= op.durationFrames, "Sequence end would cut off an existing clip.");
                s.durationFrames = op.durationFrames; s.automaticEnd = false; return "Set manual sequence end";
            } else if constexpr (std::is_same_v<T, SetAutomaticSequenceEnd>) {
                s.automaticEnd = true; return "Use automatic sequence end";
            } else {
                auto& t = track(s, op.trackId, !std::is_same_v<T, SetTrackLocked>);
                if constexpr (std::is_same_v<T, RemoveTrack>) {
                    for (qsizetype i = 0; i < s.tracks.size(); ++i) if (s.tracks[i].id == op.trackId) {
                        s.tracks.removeAt(i); break;
                    }
                    pruneSelection(state); return "Remove track";
                } else if constexpr (std::is_same_v<T, SetTrackLocked>) {
                    t.locked = op.locked; return "Lock/unlock track";
                } else if constexpr (std::is_same_v<T, SetTrackEnabled>) {
                    t.enabled = op.enabled; return "Enable/disable track";
                } else if constexpr (std::is_same_v<T, SetTrackAudio>) {
                    require(t.kind == "audio", "Audio controls require an audio track.");
                    t.gain = op.gain; t.muted = op.muted; t.solo = op.solo; t.pan = op.pan;
                    t.audioBusId = op.audioBusId; t.audioAutomation = op.automation; return "Edit track audio";
                } else if constexpr (std::is_same_v<T, InsertClip>) {
                    end(op.clip);
                    require(op.clip.kind == t.kind, "Clip kind must match destination track.");
                    if (op.mode == EditMode::Ripple) insertInterval(t, op.clip.startFrame, op.clip.durationFrames);
                    const bool alreadyHasVideo = std::any_of(s.tracks.cbegin(), s.tracks.cend(), [](const auto& existingTrack) {
                        return existingTrack.kind == "video" && !existingTrack.clips.isEmpty();
                    });
                    if (s.primaryVideoMediaId.isEmpty() && !alreadyHasVideo && op.clip.kind == "video" && op.clip.sequenceId.isEmpty() && !op.clip.mediaId.isEmpty()) {
                        s.primaryVideoMediaId = op.clip.mediaId; s.primaryVideoStreamIndex = op.clip.streamIndex;
                    }
                    t.clips.append(op.clip); grow(s); return "Insert clip";
                } else if constexpr (std::is_same_v<T, CloseGap>) {
                    require(end(op.startFrame, op.durationFrames) <= s.durationFrames, "Gap extends beyond sequence end.");
                    removeInterval(t, op.startFrame, op.durationFrames); return "Close gap";
                } else {
                    const auto index = clipIndex(t, op.clipId);
                    auto c = t.clips[index];
                    if constexpr (std::is_same_v<T, SetClipEffects>) {
                        require(op.effects.size() <= 64, "The effect stack limit is 64 effects per clip.");
                        const auto oldSpeed = speedEffect(c) ? std::optional<Effect>(*speedEffect(c)) : std::nullopt;
                        c.effects = op.effects;
                        int speeds = 0;
                        for (const auto& e : c.effects) {
                            require(c.kind != "audio" || !supportedEffect(e) || e.type == "speed", "Audio clips support only speed effects.");
                            require(c.kind != "title" || e.type != "speed", "Speed requires a media clip.");
                            require(e.type != "speed" || ++speeds <= 1, "Use one speed effect per clip.");
                            const auto error = validateEffect(e, c.durationFrames); require(error.isEmpty(), error);
                        }
                        const auto newSpeed = speedEffect(c) ? std::optional<Effect>(*speedEffect(c)) : std::nullopt;
                        if (oldSpeed != newSpeed) {
                            const Stream* selected = nullptr;
                            for (const auto& m : state.project.media) if (m.id == c.mediaId) for (const auto& st : m.streams) if (st.index == c.streamIndex) selected = &st;
                            require(selected != nullptr, "Speed requires a valid source stream.");
                            const auto duration = speedDuration(c, s.frameRate, selected->timeBase);
                            require(duration.has_value(), "Speed range exceeds supported duration."); c.durationFrames = *duration;
                            const auto error = trimEffects(c, 0, c.durationFrames); require(error.isEmpty(), error);
                            c.fadeInFrames = std::min(c.fadeInFrames, c.durationFrames); c.fadeOutFrames = std::min(c.fadeOutFrames, c.durationFrames);
                        }
                        t.clips[index] = c; grow(s); return "Edit clip effects";
                    } else if constexpr (std::is_same_v<T, SetClipAudio>) {
                        require(c.kind == "audio", "Audio controls require an audio clip.");
                        c.gain = op.gain; c.muted = op.muted;
                        c.fadeInFrames = op.fadeInFrames; c.fadeOutFrames = op.fadeOutFrames;
                        c.pan = op.pan; c.audioAutomation = op.automation;
                        t.clips[index] = c; return "Edit clip audio";
                    } else if constexpr (std::is_same_v<T, DeleteClip>) {
                        t.clips.removeAt(index);
                        if (op.mode == EditMode::Ripple) removeInterval(t, c.startFrame, c.durationFrames);
                        pruneSelection(state); return "Delete clip";
                    } else if constexpr (std::is_same_v<T, SplitClip>) {
                        require(op.frame > c.startFrame && op.frame < end(c), "Split must be strictly inside the clip.");
                        require(op.rightEffectIds.size() == c.effects.size(), "Split needs one new ID for each copied effect.");
                        auto right = c; right.id = op.rightClipId;
                        for (qsizetype i = 0; i < right.effects.size(); ++i) right.effects[i].id = op.rightEffectIds[i];
                        const auto leftDuration = op.frame - c.startFrame;
                        const auto stop = end(c);
                        if (c.kind != "title") {
                            const auto boundary = sourceBoundary(c, leftDuration);
                            require(boundary && *boundary > c.sourceInTicks &&
                                *boundary - c.sourceInTicks < c.sourceDurationTicks,
                                "Split rounds to an empty source interval.");
                            right.sourceInTicks = *boundary;
                            right.sourceDurationTicks = c.sourceDurationTicks - (*boundary - c.sourceInTicks);
                            c.sourceDurationTicks = *boundary - c.sourceInTicks;
                        }
                        c.durationFrames = leftDuration;
                        right.startFrame = op.frame; right.durationFrames = stop - op.frame;
                        c.fadeInFrames = std::min(c.fadeInFrames, c.durationFrames); c.fadeOutFrames = 0;
                        right.fadeInFrames = 0; right.fadeOutFrames = std::min(right.fadeOutFrames, right.durationFrames);
                        const auto effectError = splitEffects(c, right, leftDuration);
                        require(effectError.isEmpty(), effectError);
                        if (!right.audioAutomation.volume.isEmpty() || !right.audioAutomation.pan.isEmpty())
                            right.audioAutomation.timeOffsetFrames += leftDuration;
                        t.clips[index] = c; t.clips.insert(index + 1, right);
                        auto& sel = state.selection;
                        if (sel.sequenceId == s.id && sel.clipIds.contains(c.id))
                            sel.clipIds.insert(sel.clipIds.indexOf(c.id) + 1, right.id);
                        return "Split clip";
                    } else if constexpr (std::is_same_v<T, TrimClip>) {
                        const auto oldEnd = end(c);
                        const auto newEnd = end(op.startFrame, op.durationFrames);
                        require(op.mode != EditMode::Ripple || op.startFrame == c.startFrame,
                            "Ripple trim changes only the out point; use a normal trim for the in point.");
                        trimSource(c, op.startFrame, op.durationFrames);
                        if (op.mode == EditMode::Ripple) {
                            t.clips.removeAt(index);
                            if (newEnd > oldEnd) insertInterval(t, oldEnd, newEnd - oldEnd);
                            if (newEnd < oldEnd) removeInterval(t, newEnd, oldEnd - newEnd);
                            t.clips.insert(index, c);
                        } else t.clips[index] = c;
                        grow(s); return "Trim clip";
                    } else if constexpr (std::is_same_v<T, MoveClip>) {
                        auto& destination = track(s, op.destinationTrackId);
                        require(c.kind == destination.kind, "Clip kind must match destination track.");
                        end(op.startFrame, c.durationFrames);
                        if (op.destinationTrackId == op.trackId && op.startFrame == c.startFrame) return "Move clip";
                        t.clips.removeAt(index);
                        if (op.mode == EditMode::Ripple) {
                            removeInterval(t, c.startFrame, c.durationFrames);
                            insertInterval(destination, op.startFrame, c.durationFrames);
                        }
                        c.startFrame = op.startFrame;
                        // Keep compositing order on the same track; cross-track moves append on the destination.
                        if (op.destinationTrackId == op.trackId) destination.clips.insert(index, c);
                        else destination.clips.append(c);
                        grow(s); return "Move clip";
                    }
                }
            }
        }
    }, command);
}
}

std::optional<qint64> sourceBoundary(const Clip& c, qint64 offset) {
    if (c.kind == "title" || c.sourceInTicks < 0 || c.sourceDurationTicks <= 0 || c.durationFrames <= 0 || offset == std::numeric_limits<qint64>::min()) return {};
    std::optional<qint64> delta;
    if (speedEffect(c)) {
        const auto ticks = std::abs(sourceFraction(c, offset)) * c.sourceDurationTicks;
        if (!std::isfinite(ticks) || ticks >= static_cast<long double>(MaxTime)) return {};
        delta = static_cast<qint64>(std::round(ticks));
    } else delta = scaleTime(offset < 0 ? -offset : offset, c.sourceDurationTicks, c.durationFrames, Rounding::Nearest);
    if (!delta) return {};
    if (offset < 0) {
        if (*delta > c.sourceInTicks) return {};
        return c.sourceInTicks - *delta;
    }
    if (*delta > MaxTime - c.sourceInTicks) return {};
    return c.sourceInTicks + *delta;
}
QVector<FrameRange> gaps(const Sequence& s, const Track& t) {
    QVector<FrameRange> result;
    QVector<Clip> clips = t.clips;
    std::sort(clips.begin(), clips.end(), [](const auto& a, const auto& b) { return a.startFrame < b.startFrame; });
    qint64 cursor = 0;
    for (const auto& c : clips) {
        if (c.startFrame > cursor) result.append({cursor, c.startFrame - cursor});
        cursor = std::max(cursor, end(c));
    }
    if (cursor < s.durationFrames) result.append({cursor, s.durationFrames - cursor});
    return result;
}
QStringList clipsAtFrame(const Sequence& s, qint64 frame, bool enabledOnly) {
    QStringList result;
    if (frame < 0 || frame >= s.durationFrames) return result;
    for (const auto& t : s.tracks) if (!enabledOnly || t.enabled)
        for (const auto& c : t.clips) if (frame >= c.startFrame && frame < end(c)) result.append(c.id);
    return result;
}
TimelineEditor::TimelineEditor(Project p, qsizetype limit) : historyLimit_(limit) {
    require(limit > 0, "History limit must be positive.");
    const auto error = replaceProject(std::move(p)); require(error.isEmpty(), error);
}
QString TimelineEditor::replaceProject(Project p) {
    const auto error = validate(p);
    if (!error.isEmpty()) return error;
    state_ = {std::move(p), {}}; history_.clear(); cursor_ = 0; return {};
}
QString TimelineEditor::execute(const Command& command) {
    return executeBatch({command}, {});
}
QString TimelineEditor::executeBatch(const QVector<Command>& commands, const QString& requestedLabel) {
    auto next = state_;
    QString label;
    try {
        for (const auto& command : commands) label = apply(next, command);
        // Recompute in the same transaction as the edit so undo restores both
        // the clips and their end. Manual ends retain the existing grow policy.
        for (auto& s : next.project.sequences) if (s.automaticEnd) {
            qint64 lastEnd = 0;
            for (const auto& t : s.tracks) for (const auto& c : t.clips) lastEnd = std::max(lastEnd, end(c));
            s.durationFrames = lastEnd;
        }
        if (!requestedLabel.isEmpty()) label = requestedLabel;
        const auto error = validate(next.project);
        require(error.isEmpty(), error);
        validateSelection(next);
    } catch (const std::invalid_argument& error) {
        return QString::fromUtf8(error.what());
    }
    if (next == state_) return {};
    history_.resize(cursor_);
    history_.append({label, state_, next});
    if (history_.size() > historyLimit_) history_.removeFirst();
    cursor_ = history_.size(); state_ = std::move(next); return {};
}
bool TimelineEditor::undo() {
    if (!canUndo()) return false;
    state_ = history_[--cursor_].before; return true;
}
bool TimelineEditor::redo() {
    if (!canRedo()) return false;
    state_ = history_[cursor_++].after; return true;
}
QString TimelineEditor::undoLabel() const { return canUndo() ? history_[cursor_ - 1].label : QString{}; }
QString TimelineEditor::redoLabel() const { return canRedo() ? history_[cursor_].label : QString{}; }
}

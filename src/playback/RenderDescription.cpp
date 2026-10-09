#include "RenderDescription.h"
#include "VisualEffects.h"
#include <algorithm>
#include <cmath>
#include <QPainter>
#include <QFontMetricsF>

namespace editor::playback {
namespace {
qint64 ticksUs(qint64 ticks, project::Rational base) {
    return project::scaleTime(ticks, base.numerator * 1000000, base.denominator, project::Rounding::Nearest).value_or(0);
}
qint64 frameUs(qint64 frames, project::Rational fps) {
    return project::framesToTicks(frames, fps, {1, 1000000}, project::Rounding::Nearest).value_or(0);
}
double automationValue(const QVector<project::Keyframe>& keys, long double frame, double fallback = 1.0) {
    if (keys.isEmpty()) return fallback;
    if (frame <= keys.first().frame) return keys.first().value;
    if (frame >= keys.last().frame) return keys.last().value;
    auto right = std::upper_bound(keys.begin(), keys.end(), frame, [](long double f, const auto& key) { return f < key.frame; });
    const auto& left = *(right - 1);
    const long double t = (frame - left.frame) / (right->frame - left.frame);
    const long double curve = left.curve == "hold" ? 0 : left.curve == "eased" ? t * t * (3 - 2 * t) : t;
    return static_cast<double>(left.value + (right->value - left.value) * curve);
}
long double sequenceFrameAtSample(qint64 sample, project::Rational rate) {
    return static_cast<long double>(sample) * rate.numerator / (48000.0L * rate.denominator);
}
}
static RenderDescription compileSequenceGraph(const project::Project& project, const QString& sid, int depth, int& nodes) {
    RenderDescription result;
    if (depth > 8 || ++nodes > 512) { result.graphError = "Expanded sequence graph exceeds eight nesting levels or 512 instances."; return result; }
    for (const auto& sequence : project.sequences) if (sequence.id == sid) {
        result.sequence = sequence; result.media = project.media; result.titles = project.titles;
        result.frameRate = sequence.frameRate; result.width = sequence.width; result.height = sequence.height;
        result.durationUs = frameUs(sequence.durationFrames, sequence.frameRate);
        const bool busSolo = std::any_of(sequence.audioBuses.begin(), sequence.audioBuses.end(), [](const auto& b) { return b.solo; });
        const bool solo = busSolo || std::any_of(sequence.tracks.begin(), sequence.tracks.end(), [](const auto& t) { return t.enabled && t.solo && t.kind == "audio"; });
        for (const auto& track : sequence.tracks) {
            if (!track.enabled) continue;
            const project::AudioBus* bus = nullptr;
            for (const auto& candidate : sequence.audioBuses) if (candidate.id == track.audioBusId) { bus = &candidate; break; }
            for (const auto& clip : track.clips) {
                if (clip.kind == "title") continue;
                if (clip.kind == "audio" && (track.muted || clip.muted || (bus && bus->muted) || (solo && !track.solo && !(bus && bus->solo)))) continue;
                if (!clip.sequenceId.isEmpty()) {
                    auto child = std::make_shared<RenderDescription>(compileSequenceGraph(project, clip.sequenceId, depth + 1, nodes));
                    result.children[clip.id] = child;
                    if (!child->graphError.isEmpty()) result.graphError = child->graphError;
                    if (const auto error = visualGraphError(*child); !error.isEmpty()) result.graphError = error;
                    const auto startUs = frameUs(clip.startFrame, sequence.frameRate), endUs = frameUs(clip.startFrame + clip.durationFrames, sequence.frameRate);
                    const auto offsetUs = frameUs(clip.sourceInTicks, sequence.frameRate) - startUs;
                    const auto startSample = project::framesToSamples(clip.startFrame, sequence.frameRate, 48000, project::Rounding::Nearest).value_or(0);
                    const auto endSample = project::framesToSamples(clip.startFrame + clip.durationFrames, sequence.frameRate, 48000, project::Rounding::Nearest).value_or(0);
                    const auto offsetSamples = project::framesToSamples(clip.sourceInTicks, sequence.frameRate, 48000, project::Rounding::Nearest).value_or(0) - startSample;
                    Source envelope; envelope.startUs = startUs; envelope.endUs = endUs; envelope.startSample = startSample; envelope.endSample = endSample;
                    envelope.gain = clip.gain * track.gain * (bus ? bus->gain : 1); envelope.pan = clip.pan + track.pan + (bus ? bus->pan : 0);
                    envelope.sequenceRate = sequence.frameRate; envelope.startFrame = clip.startFrame;
                    envelope.clipAutomation = clip.audioAutomation; envelope.clipAutomationOffsetFrames = clip.audioAutomation.timeOffsetFrames; envelope.trackAutomation = track.audioAutomation;
                    envelope.fadeInSamples = project::framesToSamples(clip.fadeInFrames, sequence.frameRate, 48000, project::Rounding::Nearest).value_or(0);
                    envelope.fadeOutSamples = project::framesToSamples(clip.fadeOutFrames, sequence.frameRate, 48000, project::Rounding::Nearest).value_or(0);
                    auto& output = clip.kind == "audio" ? result.audio : result.video;
                    const auto& inputs = clip.kind == "audio" ? child->audio : child->video;
                    for (const auto& input : inputs) {
                        Source source = input;
                        source.clipId = clip.id + "/" + input.clipId;
                        source.nestedSource = std::make_shared<Source>(input); source.nestedEnvelope = std::make_shared<Source>(envelope);
                        source.nestedOffsetUs = offsetUs; source.nestedOffsetSamples = offsetSamples;
                        source.startUs = std::max(startUs, input.startUs - offsetUs); source.endUs = std::min(endUs, input.endUs - offsetUs);
                        source.startSample = std::max(startSample, input.startSample - offsetSamples); source.endSample = std::min(endSample, input.endSample - offsetSamples);
                        source.trackId = track.id + "/" + input.trackId; source.busId = track.audioBusId;
                        if (source.endUs > source.startUs) output.append(source);
                        if (result.video.size() + result.audio.size() > 4096) { result.graphError = "Expanded source limit is 4096."; return result; }
                    }
                    continue;
                }
                for (const auto& media : project.media) if (media.id == clip.mediaId)
                    for (const auto& stream : media.streams) if (stream.index == clip.streamIndex) {
                        Source source{clip.id, media.path, stream.index, stream.rotation,
                            frameUs(clip.startFrame, sequence.frameRate), frameUs(clip.startFrame + clip.durationFrames, sequence.frameRate),
                            ticksUs(clip.sourceInTicks, stream.timeBase), clip.gain * track.gain * (bus ? bus->gain : 1.0)};
                        source.trackId = track.id; source.busId = track.audioBusId; source.startFrame = clip.startFrame;
                        source.camera = sequence.multicam && sequence.multicam->cameraTrackIds.contains(track.id);
                        source.sequenceRate = sequence.frameRate; source.pan = std::clamp(clip.pan + track.pan + (bus ? bus->pan : 0.0), -1.0, 1.0);
                        source.clipAutomationOffsetFrames = clip.audioAutomation.timeOffsetFrames;
                        source.clipAutomation = clip.audioAutomation; source.trackAutomation = track.audioAutomation;
                        source.sourceSpanUs = ticksUs(clip.sourceDurationTicks, stream.timeBase);
                        if (project::speedEffect(clip)) source.timeClip = clip;
                        if (clip.kind == "audio") {
                            source.startSample = project::framesToSamples(clip.startFrame, sequence.frameRate, 48000, project::Rounding::Nearest).value_or(0);
                            source.endSample = project::framesToSamples(clip.startFrame + clip.durationFrames, sequence.frameRate, 48000, project::Rounding::Nearest).value_or(0);
                            source.fadeInSamples = project::framesToSamples(clip.fadeInFrames, sequence.frameRate, 48000, project::Rounding::Nearest).value_or(0);
                            source.fadeOutSamples = project::framesToSamples(clip.fadeOutFrames, sequence.frameRate, 48000, project::Rounding::Nearest).value_or(0);
                        }
                        (clip.kind == "audio" ? result.audio : result.video).append(source);
                    }
            }
        }
        break;
    }
    if (const auto error = visualGraphError(result); !error.isEmpty()) result.limitations.append(error);
    result.limitations.append("Raster SDR compositing; at most eight simultaneous video layers. Audio uses 48 kHz float stereo, sequence-frame automation and linear clip fades; final output hard-clips above unity.");
    result.limitations.removeDuplicates();
    return result;
}
RenderDescription compileSequence(const project::Project& project) {
    int nodes = 0;
    if (const auto error = project::validate(project); !error.isEmpty()) { RenderDescription d; d.graphError = error; return d; }
    return compileSequenceGraph(project, project.activeSequenceId, 0, nodes);
}
RenderDescription compileSource(const project::Media& media) {
    RenderDescription result;
    qint64 origin = 0; bool found = false;
    auto signedTicksUs = [](const auto& s) {
        const auto magnitude = ticksUs(s.startTicks < 0 ? -s.startTicks : s.startTicks, s.timeBase);
        return s.startTicks < 0 ? -magnitude : magnitude;
    };
    for (const auto& s : media.streams) {
        const auto start = signedTicksUs(s);
        if (!found || start < origin) origin = start;
        found = true;
    }
    for (const auto& s : media.streams) {
        auto& sources = s.kind == "video" ? result.video : result.audio;
        if (!sources.isEmpty()) continue;
        const auto start = signedTicksUs(s) - origin, end = start + ticksUs(s.durationTicks, s.timeBase);
        sources.append({media.id, media.path, s.index, s.rotation, start, end, 0, 1});
        result.durationUs = std::max(result.durationUs, end);
        if (s.kind == "video") { result.frameRate = s.frameRate; result.width = s.width; result.height = s.height; }
    }
    return result;
}
std::optional<Source> activeSource(const QVector<Source>& sources, qint64 positionUs) {
    // Later tracks are visually above earlier ones, matching the future compositor's order.
    for (auto it = sources.crbegin(); it != sources.crend(); ++it)
        if (positionUs >= it->startUs && positionUs < it->endUs) return *it;
    return {};
}
qint64 sequenceFrameAt(const RenderDescription& d, qint64 us) {
    // A frame boundary rounded to microseconds must map back to that exact sequence frame.
    // Round-nearest identifies that boundary, then compare before selecting floor for between-frame times.
    const auto nearest = project::ticksToFrames(std::max<qint64>(0, us), {1, 1000000}, d.frameRate, project::Rounding::Nearest).value_or(0);
    return nearest > 0 && frameUs(nearest, d.frameRate) > us ? nearest - 1 : nearest;
}
QVector<Source> activeVideoSources(const RenderDescription& d, qint64 us) {
    QVector<Source> result;
    for (const auto& source : d.video) if (us >= source.startUs && us < source.endUs) result.append(source);
    return result;
}
QString visualGraphError(const RenderDescription& d) {
    if (!d.graphError.isEmpty()) return d.graphError;
    if (!d.sequence) return {};
    for (const auto& sources : {d.video, d.audio}) for (const auto& source : sources) if (source.timeClip && (source.sourceSpanUs <= 0 || source.endUs <= source.startUs)) return "Speed interval is below renderer timestamp precision.";
    QVector<QPair<qint64, int>> edges;
    for (const auto& source : d.video) { edges.append({source.startUs, 1}); edges.append({source.endUs, -1}); }
    for (const auto& t : d.sequence->tracks) if (t.enabled) for (const auto& c : t.clips) {
        if (c.effects.size() > 64) return "The effect stack limit is 64 effects per clip.";
        for (const auto& e : c.effects) if (e.enabled) {
            if (!project::supportedEffect(e) || (c.kind == "audio" && e.type != "speed")) return "Unsupported enabled effect: " + e.type + " v" + QString::number(e.version) + ". Bypass it to render.";
            if (const auto error = project::validateEffect(e, c.durationFrames); !error.isEmpty()) return "Effect " + e.id + ": " + error;
        }
    }
    std::sort(edges.begin(), edges.end()); int count = 0;
    for (const auto& edge : edges) { count += edge.second; if (count > MaxVideoLayers) return "The compositor supports at most eight simultaneous video layers. Disable or rearrange tracks."; }
    return {};
}
qint64 sourceMediaUs(const Source& s, qint64 us) {
    if (s.nestedSource) return sourceMediaUs(*s.nestedSource, us + s.nestedOffsetUs);
    if (!s.timeClip) return s.inUs + std::max<qint64>(0, us - s.startUs);
    const auto& c = *s.timeClip;
    const auto frame = static_cast<long double>(std::clamp(us - s.startUs, 0LL, s.endUs - s.startUs)) * c.durationFrames / (s.endUs - s.startUs);
    return s.inUs + static_cast<qint64>(std::round(project::sourceFraction(c, frame) * s.sourceSpanUs));
}
qint64 sourceTimelineUs(const Source& s, qint64 us) {
    if (s.nestedSource) return sourceTimelineUs(*s.nestedSource, us) - s.nestedOffsetUs;
    if (!s.timeClip) return s.startUs + us - s.inUs;
    const auto& c = *s.timeClip;
    const auto fraction = static_cast<long double>(us - s.inUs) / s.sourceSpanUs;
    if (fraction < 0) return s.startUs - 1; // Seek preroll; it is discarded before the first visible frame.
    const auto frame = project::outputFrameAtFraction(c, fraction);
    return s.startUs + static_cast<qint64>(std::round(frame * (s.endUs - s.startUs) / c.durationFrames));
}
double sourceTempo(const Source& s, qint64 us) {
    if (s.nestedSource) return sourceTempo(*s.nestedSource, us);
    if (!s.timeClip) return 1;
    const auto& c = *s.timeClip;
    const auto frame = project::outputFrameAtFraction(c, static_cast<long double>(us - s.inUs) / s.sourceSpanUs);
    // Evaluate the continuous derivative using a short interval; integral defines the source map.
    const auto derivative = (project::speedIntegral(c, frame + 0.001L) - project::speedIntegral(c, frame)) / 0.001L;
    return static_cast<double>(derivative * s.sourceSpanUs * c.durationFrames / ((s.endUs - s.startUs) * project::speedIntegral(c, c.durationFrames)));
}
QStringList activeTitleClips(const RenderDescription& d, qint64 us) {
    QStringList result;
    if (d.sequence) for (const auto& t : d.sequence->tracks) if (t.enabled)
        for (const auto& c : t.clips) if (c.kind == "title" && us >= frameUs(c.startFrame, d.frameRate) && us < frameUs(c.startFrame + c.durationFrames, d.frameRate)) result.append(c.id);
    return result;
}
QImage renderTitles(const RenderDescription& d, const QImage& video, qint64 us, QSize outputSize, bool transparent) {
    const auto active = activeTitleClips(d, us);
    if (active.isEmpty() && outputSize.isEmpty()) return video;
    const auto size = outputSize.isEmpty() ? QSize(d.width, d.height).scaled(1280, 720, Qt::KeepAspectRatio) : outputSize;
    QImage image(size, QImage::Format_ARGB32_Premultiplied); image.fill(transparent ? Qt::transparent : Qt::black);
    QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing); painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!video.isNull()) { QRect area(QPoint{}, video.size().scaled(size, Qt::KeepAspectRatio)); area.moveCenter(image.rect().center()); painter.drawImage(area, video); }
    const double factor = static_cast<double>(size.width()) / d.width;
    if (d.sequence) for (const auto& track : d.sequence->tracks) for (const auto& clip : track.clips) if (active.contains(clip.id))
        for (const auto& title : d.titles) if (title.id == clip.titleId) {
            QFont font(title.fontFamily); font.setPixelSize(std::max(1, static_cast<int>(std::round(title.fontSize * factor)))); painter.setFont(font);
            int alignment = title.alignment == "left" ? Qt::AlignLeft : title.alignment == "right" ? Qt::AlignRight : Qt::AlignHCenter;
            const auto bounds = QFontMetricsF(font).boundingRect(QRectF(0, 0, size.width() * 0.9, size.height()), Qt::TextWordWrap | alignment, title.text);
            const auto width = std::clamp(bounds.width(), 1.0, size.width() * 0.9);
            QRectF box(0, 0, width, std::max<double>(font.pixelSize() * 1.5, bounds.height()));
            const double anchor = title.x * size.width();
            box.moveLeft(title.alignment == "left" ? anchor : title.alignment == "right" ? anchor - width : anchor - width / 2);
            box.moveTop(title.y * size.height() - box.height() / 2);
            alignment |= Qt::AlignVCenter | Qt::TextWordWrap;
            painter.fillRect(box.adjusted(-4 * factor, -4 * factor, 4 * factor, 4 * factor), QColor(title.background));
            if (title.shadow) { painter.setPen(Qt::black); painter.drawText(box.translated(2 * factor, 2 * factor), alignment, title.text); }
            painter.setPen(QColor(title.color)); painter.drawText(box, alignment, title.text);
        }
    return image;
}
QImage renderLayers(const RenderDescription& d, const QMap<QString, QImage>& videos, qint64 us, QSize size) {
    const auto canvas = QSize(d.width, d.height).scaled(size, Qt::KeepAspectRatio);
    QImage image(canvas, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::black);
    QPainter compositor(&image); compositor.setRenderHint(QPainter::SmoothPixmapTransform);
    const auto frame = sequenceFrameAt(d, us);
    const auto selectedCamera = d.sequence ? project::cameraAt(*d.sequence, frame) : QString{};
    if (d.sequence) { for (const auto& t : d.sequence->tracks) if (t.enabled) for (const auto& c : t.clips) {
        const bool camera = d.sequence->multicam && d.sequence->multicam->cameraTrackIds.contains(t.id);
        if (camera && !d.cameraOverview && t.id != selectedCamera) continue;
        if (frame < c.startFrame || frame - c.startFrame >= c.durationFrames || c.kind == "audio") continue;
        QImage layer(canvas, QImage::Format_ARGB32_Premultiplied); layer.fill(Qt::transparent);
        if (!c.sequenceId.isEmpty() && d.children.contains(c.id)) {
            const auto& child = *d.children[c.id]; QMap<QString, QImage> inputs;
            const auto prefix = c.id + "/";
            for (auto it = videos.cbegin(); it != videos.cend(); ++it) if (it.key().startsWith(prefix)) inputs[it.key().mid(prefix.size())] = it.value();
            const auto childUs = frameUs(frame - c.startFrame + c.sourceInTicks, d.frameRate);
            layer = renderLayers(child, inputs, childUs, canvas);
        } else if (c.kind == "video") {
            const auto video = videos.value(c.id);
            if (!video.isNull()) { QPainter p(&layer); p.setRenderHint(QPainter::SmoothPixmapTransform); QRect area(QPoint{}, video.size().scaled(canvas, Qt::KeepAspectRatio)); area.moveCenter(layer.rect().center()); p.drawImage(area, video); }
        } else {
            auto titleGraph = d; titleGraph.sequence->tracks = {t}; titleGraph.sequence->tracks[0].clips = {c};
            layer = renderTitles(titleGraph, {}, us, canvas, true);
        }
        const auto visual = applyEffects(std::move(layer), c, frame - c.startFrame);
        compositor.setCompositionMode(visual.mode);
        if (camera && d.cameraOverview) {
            const auto index = d.sequence->multicam->cameraTrackIds.indexOf(t.id);
            const QRect cell(static_cast<int>(index % 2) * canvas.width() / 2, static_cast<int>(index / 2) * canvas.height() / 2, canvas.width() / 2, canvas.height() / 2);
            compositor.drawImage(cell, visual.image); compositor.setPen(Qt::white); compositor.drawText(cell.adjusted(8, 8, -8, -8), Qt::AlignTop | Qt::AlignLeft, QString("%1 · %2").arg(index + 1).arg(t.name));
        } else compositor.drawImage(0, 0, visual.image);
    } } else if (const auto source = activeSource(d.video, us)) {
        const auto video = videos.value(source->clipId);
        if (!video.isNull()) { QRect area(QPoint{}, video.size().scaled(canvas, Qt::KeepAspectRatio)); area.moveCenter(image.rect().center()); compositor.drawImage(area, video); }
    }
    compositor.end();
    if (canvas == size) return image;
    QImage output(size, QImage::Format_ARGB32_Premultiplied); output.fill(Qt::black);
    QPainter painter(&output); painter.drawImage((size.width() - canvas.width()) / 2, (size.height() - canvas.height()) / 2, image);
    return output;
}
QImage renderFrame(const RenderDescription& d, const QImage& video, qint64 us, QSize size) {
    QMap<QString, QImage> layers;
    if (const auto source = activeSource(d.video, us)) layers[source->clipId] = video;
    return renderLayers(d, layers, us, size);
}
double audioGainAtSample(const Source& source, qint64 sample) {
    if (source.nestedSource) {
        if (sample < source.startSample || sample >= source.endSample) return 0;
        return audioGainAtSample(*source.nestedSource, sample + source.nestedOffsetSamples) * audioGainAtSample(*source.nestedEnvelope, sample);
    }
    const auto start = source.startSample >= 0 ? source.startSample : project::scaleTime(source.startUs, 48000, 1000000, project::Rounding::Nearest).value_or(0);
    const auto end = source.endSample >= 0 ? source.endSample : project::scaleTime(source.endUs, 48000, 1000000, project::Rounding::Nearest).value_or(0);
    if (sample < start || sample >= end) return 0;
    const auto timelineFrame = sequenceFrameAtSample(sample, source.sequenceRate);
    const auto clipFrame = timelineFrame - source.startFrame + source.clipAutomationOffsetFrames;
    double gain = source.gain * automationValue(source.clipAutomation.volume, clipFrame) * automationValue(source.trackAutomation.volume, timelineFrame);
    if (source.fadeInSamples > 0) gain *= std::min(1.0, static_cast<double>(sample - start) / source.fadeInSamples);
    if (source.fadeOutSamples > 0) gain *= std::min(1.0, static_cast<double>(end - sample) / source.fadeOutSamples);
    return gain;
}
double audioPanAtSample(const Source& source, qint64 sample) {
    if (source.nestedSource) return std::clamp(audioPanAtSample(*source.nestedSource, sample + source.nestedOffsetSamples) + audioPanAtSample(*source.nestedEnvelope, sample), -1.0, 1.0);
    const auto timelineFrame = sequenceFrameAtSample(sample, source.sequenceRate);
    const auto clipFrame = timelineFrame - source.startFrame + source.clipAutomationOffsetFrames;
    const auto pan = source.pan + automationValue(source.clipAutomation.pan, clipFrame, 0) + automationValue(source.trackAutomation.pan, timelineFrame, 0);
    return std::clamp(pan, -1.0, 1.0);
}
}

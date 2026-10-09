#pragma once
#include "project/Project.h"
#include <QImage>
#include <QMap>
#include <memory>

namespace editor::playback {
// App-owned boundary shared by preview and offline export. All ranges are half-open.
struct Source {
    QString clipId, path;
    int streamIndex = -1, rotation = 0;
    qint64 startUs = 0, endUs = 0, inUs = 0;
    double gain = 1;
    qint64 startSample = -1, endSample = -1, fadeInSamples = 0, fadeOutSamples = 0; // 48 kHz preview; frame-derived boundaries.
    std::optional<project::Clip> timeClip;
    qint64 sourceSpanUs = 0;
    QString trackId, busId;
    qint64 startFrame = 0, clipAutomationOffsetFrames = 0;
    project::Rational sequenceRate{30, 1};
    double pan = 0;
    project::AudioAutomation clipAutomation, trackAutomation;
    std::shared_ptr<const Source> nestedSource, nestedEnvelope;
    qint64 nestedOffsetUs = 0, nestedOffsetSamples = 0; // Child time = parent time + offset.
    bool camera = false;
    bool operator==(const Source&) const = default;
};
struct RenderDescription {
    // Preserve the complete ordered edit graph, including currently unevaluated titles/effects.
    // Export consumes this graph with its own quality/encoder settings, never a second edit model.
    std::optional<project::Sequence> sequence;
    QVector<project::Media> media;
    QVector<project::Title> titles;
    project::Rational frameRate{30, 1};
    int width = 1920, height = 1080;
    qint64 durationUs = 0;
    QVector<Source> video, audio;
    QStringList limitations;
    QMap<QString, std::shared_ptr<const RenderDescription>> children;
    QString graphError;
    bool cameraOverview = false; // Preview-only; never serialized or enabled by export compilation.
};
RenderDescription compileSequence(const project::Project& project);
RenderDescription compileSource(const project::Media& media);
std::optional<Source> activeSource(const QVector<Source>& sources, qint64 positionUs);
inline constexpr int MaxVideoLayers = 8;
qint64 sequenceFrameAt(const RenderDescription& description, qint64 positionUs);
QVector<Source> activeVideoSources(const RenderDescription& description, qint64 positionUs);
QString visualGraphError(const RenderDescription& description);
qint64 sourceMediaUs(const Source& source, qint64 timelineUs);
qint64 sourceTimelineUs(const Source& source, qint64 mediaUs);
double sourceTempo(const Source& source, qint64 mediaUs);
QStringList activeTitleClips(const RenderDescription& description, qint64 positionUs);
QImage renderTitles(const RenderDescription& description, const QImage& video, qint64 positionUs, QSize outputSize = {}, bool transparent = false);
QImage renderLayers(const RenderDescription& description, const QMap<QString, QImage>& videoLayers, qint64 positionUs, QSize outputSize);
// Shared letterbox and title composition. Export supplies its full output size; preview caps at 720p.
QImage renderFrame(const RenderDescription& description, const QImage& video, qint64 positionUs, QSize outputSize);
double audioGainAtSample(const Source& source, qint64 sample);
double audioPanAtSample(const Source& source, qint64 sample);
}

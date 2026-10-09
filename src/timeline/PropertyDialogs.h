#pragma once
#include "Timeline.h"
#include "ui/TimeDisplay.h"
#include <QWidget>

namespace editor::timeline {
struct TitleEdit { Title title; qint64 durationFrames = 1; };
struct AudioEdit { SetTrackAudio track; std::optional<SetClipAudio> clip; };
std::optional<TitleEdit> titleDialog(QWidget* parent, const Title& title, qint64 duration, TimeFormat time = {});
std::optional<AudioEdit> audioDialog(QWidget* parent, const QString& sequenceId, const Track& track,
    const Clip* clip, TimeFormat time = {}, const char* objectName = "audioProperties", const QVector<AudioBus>& buses = {});
std::optional<SetClipAudio> clipAudioDialog(QWidget* parent, const QString& sequenceId, const QString& trackId, const Clip& clip, TimeFormat time = {});
std::optional<SetTrackAudio> trackAudioDialog(QWidget* parent, const QString& sequenceId, const Track& track,
    const QVector<AudioBus>& buses = {}, TimeFormat time = {});
std::optional<QVector<AudioBus>> audioBusManagerDialog(QWidget* parent, const QVector<AudioBus>& buses);
std::optional<QVector<Effect>> effectsDialog(QWidget* parent, const Clip& clip, qint64 localFrame, TimeFormat time = {});
}

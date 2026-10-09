#pragma once
#include "DecodeWorker.h"
#include <atomic>

namespace editor::playback {
// A bounded sample scheduler: sources are opened only as their half-open interval is reached.
// Decode and mixing run off the UI thread; WASAPI consumes the bounded mixed queue.
class AudioMixer final : public QThread {
public:
    static constexpr size_t QueueLimit = 8, DecoderLimit = 64;
    AudioMixer(QVector<Source> sources, qint64 startUs, qint64 endUs, qint64 endSample = -1);
    ~AudioMixer() override;
    void cancel();
    bool takeAudio(AudioBlock& block);
    bool ready() const;
    bool done() const;
    QJsonObject stats() const;
    QJsonObject takeMeters(); // Peak window is reset; clipping count stays latched for this playback run.
protected:
    void run() override;
private:
    QVector<Source> sources_;
    qint64 startUs_, endUs_, endSample_;
    mutable QMutex mutex_;
    QWaitCondition space_;
    std::deque<AudioBlock> queue_;
    bool finished_ = false;
    int highWater_ = 0, decoderHighWater_ = 0;
    qint64 mixedSamples_ = 0;
    double masterPeak_ = 0;
    qint64 clippedSamples_ = 0;
    QMap<QString, double> trackPeaks_, busPeaks_;
    QString error_;
};
}

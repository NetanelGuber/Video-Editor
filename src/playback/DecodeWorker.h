#pragma once
#include "RenderDescription.h"
#include <QImage>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QJsonObject>
#include <deque>

namespace editor::playback {
enum class DecodeMode { Cpu, D3D11, UnavailableForTest };
struct VideoFrame { qint64 ptsUs = 0; QImage image; };
struct AudioBlock { qint64 ptsUs = 0; QVector<float> samples; }; // Interleaved 48 kHz stereo.
class DecodeWorker final : public QThread {
public:
    static constexpr size_t VideoLimit = 6, AudioLimit = 24;
    DecodeWorker(Source source, qint64 positionUs, bool audio, DecodeMode mode, QObject* parent = nullptr, QSize maximumSize = {1280, 720}, qint64 videoBudgetBytes = 64 * 1024 * 1024);
    ~DecodeWorker() override;
    void cancel();
    bool takeVideo(qint64 positionUs, VideoFrame& frame, int& dropped);
    bool takeNextVideo(VideoFrame& frame); // Offline rendering consumes in order, with one frame of lookahead.
    bool takeAudio(AudioBlock& block);
    bool ready() const;
    bool videoReadyThrough(qint64 positionUs, qint64 toleranceUs = 25000) const;
    bool done() const;
    QJsonObject stats() const;
protected:
    void run() override;
private:
    Source source_;
    qint64 positionUs_;
    bool audio_;
    DecodeMode mode_;
    QSize maximumSize_;
    qint64 videoBudgetBytes_, queuedVideoBytes_ = 0, videoBytesHighWater_ = 0;
    mutable QMutex mutex_;
    QWaitCondition space_;
    std::deque<VideoFrame> video_;
    std::deque<AudioBlock> audioQueue_;
    bool finished_ = false;
    QString error_, path_ = "CPU", fallback_;
    qint64 decoded_ = 0, firstMs_ = -1, convertUs_ = 0;
    qint64 latestVideoPts_ = -1;
    int highWater_ = 0;
    bool decode(DecodeMode mode, QString& failure);
    bool push(VideoFrame frame);
    bool push(AudioBlock block);
};
}

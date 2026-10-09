#pragma once
#include "AudioOutput.h"
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>
#include <vector>

namespace editor::playback {
class PlaybackController final : public QObject {
    Q_OBJECT
public:
    explicit PlaybackController(QObject* parent = nullptr);
    ~PlaybackController() override;
    void setDescription(RenderDescription description);
    void setDecodeMode(DecodeMode mode);
    void setPreviewProfile(QSize maximumSize, bool lowMemory = false);
    void setCameraOverview(bool enabled);
    bool updateCameraCuts(RenderDescription description);
    QSize previewSize() const;
    void seek(qint64 positionUs);
    void play();
    void pause();
    bool isPlaying() const { return playing_; }
    qint64 positionUs() const { return positionUs_; }
    const RenderDescription& description() const { return description_; }
    QJsonObject stats() const;
signals:
    void frameReady(const QImage& image);
    void positionChanged(qint64 positionUs);
    void stateChanged();
    void audioMetersChanged(const QJsonObject& meters);
private:
    void tick();
    void cancelPipelines();
    void startPipelines();
    void presentFrame();
    QMap<QString, QImage> rawFrames_;
    QMap<QString, qint64> rawFramePts_;
    QStringList titleClips_;
    RenderDescription description_;
    DecodeMode mode_ = DecodeMode::Cpu;
    QSize maximumSize_{1280, 720};
    qint64 videoBudgetBytes_ = 32 * 1024 * 1024;
    qint64 presentedPreviewFrame_ = -1;
    bool frameDirty_ = false;
    QTimer timer_;
    QElapsedTimer clock_;
    qint64 positionUs_ = 0, baseUs_ = 0;
    bool playing_ = false, pending_ = false, buffering_ = false, videoPending_ = false;
    bool synchronizedBuffering_ = false;
    qint64 synchronizedTargetUs_ = 0;
    int synchronizedWaits_ = 0;
    QVector<Source> videoSources_;
    std::vector<std::unique_ptr<DecodeWorker>> videos_;
    bool videosFinished() const;
    void startVideoLayers();
    std::unique_ptr<AudioMixer> audio_;
    std::unique_ptr<AudioOutput> output_;
    int presented_ = 0, dropped_ = 0, maxTickMs_ = 0, pipelineStarts_ = 0;
    QElapsedTimer heartbeat_;
    QElapsedTimer meterClock_;
    QJsonObject lastAudioOutput_;
    QJsonObject lastAudioMeters_;
};
}

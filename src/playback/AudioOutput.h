#pragma once
#include "AudioMixer.h"
#include <atomic>

namespace editor::playback {
// WASAPI and COM live entirely on this thread. Control/clock reads never wait for the device.
class AudioOutput final : public QThread {
public:
    AudioOutput(AudioMixer& decoder, qint64 startUs, qint64 endUs, double gain);
    ~AudioOutput() override;
    void cancel();
    void setPlaying(bool value) { playing_.store(value); }
    bool ready() const { return ready_.load(); }
    bool available() const { return available_.load(); }
    qint64 positionUs() const { return position_.load(); }
    QJsonObject stats() const;
protected:
    void run() override;
private:
    AudioMixer& decoder_;
    qint64 startUs_, endUs_;
    double gain_;
    std::atomic<bool> playing_{false}, ready_{false}, available_{false};
    std::atomic<qint64> position_{0};
    std::atomic<int> underruns_{0};
    mutable QMutex mutex_;
    QString error_;
};
}

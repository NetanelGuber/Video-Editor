#include "AudioMixer.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace editor::playback {
namespace {
qint64 sampleAt(qint64 us) { return project::scaleTime(us, 48000, 1000000, project::Rounding::Nearest).value_or(0); }
qint64 usAt(qint64 sample) { return project::scaleTime(sample, 1000000, 48000, project::Rounding::Nearest).value_or(0); }
QJsonObject peaksJson(const QMap<QString, double>& peaks) {
    QJsonObject result; for (auto it = peaks.cbegin(); it != peaks.cend(); ++it) result[it.key()] = it.value(); return result;
}
struct Input {
    Source source;
    std::unique_ptr<DecodeWorker> decoder;
    AudioBlock block;
    bool hasBlock = false;
    qint64 start = 0, end = 0;
};
}
AudioMixer::AudioMixer(QVector<Source> sources, qint64 startUs, qint64 endUs, qint64 endSample)
    : sources_(std::move(sources)), startUs_(startUs), endUs_(endUs), endSample_(endSample) {}
AudioMixer::~AudioMixer() { cancel(); wait(); }
void AudioMixer::cancel() { requestInterruption(); space_.wakeAll(); }
bool AudioMixer::takeAudio(AudioBlock& block) {
    QMutexLocker lock(&mutex_); if (queue_.empty()) return false;
    block = std::move(queue_.front()); queue_.pop_front(); space_.wakeAll(); return true;
}
bool AudioMixer::ready() const { QMutexLocker lock(&mutex_); return !queue_.empty() || finished_; }
bool AudioMixer::done() const { QMutexLocker lock(&mutex_); return finished_ && queue_.empty(); }
QJsonObject AudioMixer::stats() const {
    QMutexLocker lock(&mutex_);
    return {{"path", "48 kHz stereo sequence mixer"}, {"error", error_}, {"mixedSamples", mixedSamples_},
        {"queueHighWater", highWater_}, {"queueLimit", static_cast<int>(QueueLimit)},
        {"decoderHighWater", decoderHighWater_}, {"decoderLimit", static_cast<int>(DecoderLimit)},
        {"masterPeak", masterPeak_}, {"clippedSamples", clippedSamples_}, {"trackPeaks", peaksJson(trackPeaks_)}, {"busPeaks", peaksJson(busPeaks_)}};
}
QJsonObject AudioMixer::takeMeters() {
    QMutexLocker lock(&mutex_);
    const auto result = QJsonObject{{"masterPeak", masterPeak_}, {"clippedSamples", clippedSamples_},
        {"trackPeaks", peaksJson(trackPeaks_)}, {"busPeaks", peaksJson(busPeaks_)}, {"error", error_}};
    masterPeak_ = 0; trackPeaks_.clear(); busPeaks_.clear();
    return result;
}
void AudioMixer::run() {
    const auto stop = endSample_ >= 0 ? endSample_ : sampleAt(endUs_);
    auto cursor = sampleAt(startUs_);
    auto sources = sources_;
    for (auto& s : sources) {
        if (s.startSample < 0) s.startSample = sampleAt(s.startUs);
        if (s.endSample < 0) s.endSample = sampleAt(s.endUs);
    }
    std::sort(sources.begin(), sources.end(), [](const auto& a, const auto& b) { return a.startSample < b.startSample; });
    qsizetype next = 0;
    bool exceeded = false;
    std::vector<Input> inputs;
    auto fail = [&](const QString& reason) { QMutexLocker lock(&mutex_); if (error_.isEmpty()) error_ = reason; };
    while (cursor < stop && !isInterruptionRequested()) {
        { // Apply backpressure before opening more sources or producing a block.
            QMutexLocker lock(&mutex_);
            while (queue_.size() >= QueueLimit && !isInterruptionRequested()) space_.wait(&mutex_, 20);
        }
        if (isInterruptionRequested()) break;
        const auto length = std::min<qint64>(1024, stop - cursor), blockEnd = cursor + length;
        std::erase_if(inputs, [&](auto& i) { return i.end <= cursor; });
        while (next < sources.size() && sources[next].startSample < blockEnd) {
            const auto& s = sources[next++];
            if (s.endSample <= cursor || s.gain == 0) continue;
            if (inputs.size() >= DecoderLimit) { fail("Preview exceeds 64 simultaneous audio clips; mute or disable tracks to reduce the mix."); exceeded = true; break; }
            Input input; input.source = s; input.start = s.startSample; input.end = s.endSample;
            input.decoder = std::make_unique<DecodeWorker>(s, std::max(startUs_, s.startUs), true, DecodeMode::Cpu);
            input.decoder->start(); inputs.push_back(std::move(input));
            QMutexLocker lock(&mutex_); decoderHighWater_ = std::max(decoderHighWater_, static_cast<int>(inputs.size()));
        }
        if (exceeded) break;
        AudioBlock mixed{usAt(cursor), QVector<float>(static_cast<qsizetype>(length * 2), 0)};
        QMap<QString, double> blockTrackPeaks, blockBusPeaks; double blockMasterPeak = 0; qint64 blockClipped = 0;
        for (auto& input : inputs) {
            auto sample = std::max(cursor, input.start);
            const auto end = std::min(blockEnd, input.end);
            while (sample < end && !isInterruptionRequested()) {
                if (!input.hasBlock) {
                    if (!input.decoder->takeAudio(input.block)) {
                        if (input.decoder->done()) {
                            const auto error = input.decoder->stats()["error"].toString();
                            if (!error.isEmpty()) fail(input.source.clipId + ": " + error);
                            break; // Missing/short media is silence; its error remains visible.
                        }
                        msleep(1); continue;
                    }
                    input.hasBlock = true;
                }
                const auto first = sampleAt(input.block.ptsUs), last = first + input.block.samples.size() / 2;
                if (last <= sample) { input.hasBlock = false; continue; }
                if (first >= end) break;
                sample = std::max(sample, first);
                const auto until = std::min(end, last);
                for (; sample < until; ++sample) {
                    const auto gain = audioGainAtSample(input.source, sample);
                    const auto pan = audioPanAtSample(input.source, sample);
                    constexpr double HalfPi = 1.57079632679489661923;
                    const auto leftBalance = pan > 0 ? std::cos(pan * HalfPi) : 1.0;
                    const auto rightBalance = pan < 0 ? std::cos(-pan * HalfPi) : 1.0;
                    for (int channel = 0; channel < 2; ++channel) {
                        const auto balance = channel == 0 ? leftBalance : rightBalance;
                        const double value = input.block.samples[(sample - first) * 2 + channel] * gain * balance;
                        if (std::isfinite(value)) {
                            const auto magnitude = std::abs(value);
                            if (!input.source.trackId.isEmpty()) blockTrackPeaks[input.source.trackId] = std::max(blockTrackPeaks.value(input.source.trackId), magnitude);
                            if (!input.source.busId.isEmpty()) blockBusPeaks[input.source.busId] = std::max(blockBusPeaks.value(input.source.busId), magnitude);
                            mixed.samples[(sample - cursor) * 2 + channel] += static_cast<float>(value);
                        }
                    }
                }
                if (sample >= last) input.hasBlock = false;
            }
        }
        if (isInterruptionRequested()) break;
        for (auto& value : mixed.samples) {
            const auto magnitude = std::abs(static_cast<double>(value)); blockMasterPeak = std::max(blockMasterPeak, magnitude);
            if (magnitude > 1) ++blockClipped;
            value = std::clamp(value, -1.0f, 1.0f);
        }
        { QMutexLocker lock(&mutex_); queue_.push_back(std::move(mixed)); mixedSamples_ += length;
            highWater_ = std::max(highWater_, static_cast<int>(queue_.size()));
            masterPeak_ = std::max(masterPeak_, blockMasterPeak); clippedSamples_ += blockClipped;
            for (auto it = blockTrackPeaks.cbegin(); it != blockTrackPeaks.cend(); ++it) trackPeaks_[it.key()] = std::max(trackPeaks_.value(it.key()), it.value());
            for (auto it = blockBusPeaks.cbegin(); it != blockBusPeaks.cend(); ++it) busPeaks_[it.key()] = std::max(busPeaks_.value(it.key()), it.value()); }
        cursor = blockEnd;
    }
    // Cancel all decoders before joining any; cancellation also wakes their full queues.
    for (auto& input : inputs) input.decoder->cancel();
    inputs.clear();
    QMutexLocker lock(&mutex_); finished_ = true;
}
}

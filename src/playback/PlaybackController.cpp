#include "PlaybackController.h"
#include <QDebug>
#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace editor::playback {
namespace {
bool sameChildren(const RenderDescription& a, const RenderDescription& b) {
    if (a.children.keys() != b.children.keys()) return false;
    for (auto it = a.children.cbegin(); it != a.children.cend(); ++it) {
        const auto& other = *b.children[it.key()];
        if (it.value()->sequence != other.sequence || !sameChildren(*it.value(), other)) return false;
    }
    return true;
}
}
PlaybackController::PlaybackController(QObject* parent) : QObject(parent) {
    timer_.setInterval(5); timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &PlaybackController::tick); timer_.start(); heartbeat_.start(); clock_.start(); meterClock_.start();
}
PlaybackController::~PlaybackController() {
    cancelPipelines(); // Joining is limited to destruction, never the seek/play/edit path.
    output_.reset(); audio_.reset(); videos_.clear();
}
void PlaybackController::cancelPipelines() {
    if (output_) output_->cancel();
    if (audio_) audio_->cancel();
    for (const auto& video : videos_) video->cancel();
}
void PlaybackController::setDescription(RenderDescription description) {
    pause(); description_ = std::move(description); presented_ = dropped_ = maxTickMs_ = pipelineStarts_ = synchronizedWaits_ = 0;
    rawFrames_.clear(); rawFramePts_.clear(); titleClips_.clear(); lastAudioMeters_ = {}; emit audioMetersChanged({}); emit frameReady({}); seek(0);
}
void PlaybackController::setDecodeMode(DecodeMode mode) { mode_ = mode; seek(positionUs_); }
void PlaybackController::setCameraOverview(bool enabled) { description_.cameraOverview = enabled; presentFrame(); }
bool PlaybackController::updateCameraCuts(RenderDescription description) {
    if (!description.sequence || !description_.sequence || !description.sequence->multicam || !description_.sequence->multicam) return false;
    auto sequence = *description.sequence; sequence.multicam->cuts = description_.sequence->multicam->cuts;
    if (sequence != *description_.sequence || description.media != description_.media || description.titles != description_.titles || !sameChildren(description, description_)) return false;
    // Only the switching program changes. Preserve source identities and every warm nested pipeline.
    description_.sequence = std::move(description.sequence); presentFrame(); return true;
}
void PlaybackController::setPreviewProfile(QSize maximumSize, bool lowMemory) {
    maximumSize_ = maximumSize.expandedTo({160, 90}).boundedTo({1920, 1080});
    videoBudgetBytes_ = (lowMemory ? 4LL : 32LL) * 1024 * 1024;
    if (lowMemory) maximumSize_ = maximumSize_.scaled(640, 360, Qt::KeepAspectRatio);
    seek(positionUs_);
}
QSize PlaybackController::previewSize() const {
    return QSize(description_.width, description_.height).scaled(maximumSize_, Qt::KeepAspectRatio);
}
void PlaybackController::seek(qint64 positionUs) {
    positionUs_ = std::clamp(positionUs, 0LL, std::max<qint64>(0, description_.durationUs));
    pending_ = buffering_ = true; synchronizedBuffering_ = false; cancelPipelines();
    emit positionChanged(positionUs_); emit stateChanged();
}
void PlaybackController::play() {
    if (description_.durationUs <= 0) return;
    if (positionUs_ >= description_.durationUs) seek(0);
    playing_ = true; baseUs_ = positionUs_; clock_.restart();
    if (output_) output_->setPlaying(!pending_ && !buffering_);
    emit stateChanged();
}
void PlaybackController::pause() {
    playing_ = false; if (output_) output_->setPlaying(false); emit stateChanged();
}
void PlaybackController::startPipelines() {
    ++pipelineStarts_;
    output_.reset(); audio_.reset(); videos_.clear();
    lastAudioOutput_ = {};
    lastAudioMeters_ = {}; emit audioMetersChanged({}); meterClock_.restart();
    videoSources_ = activeVideoSources(description_, positionUs_);
    rawFrames_.clear(); rawFramePts_.clear(); startVideoLayers(); presentFrame();
    if (!description_.audio.isEmpty() && positionUs_ < description_.durationUs) {
        audio_ = std::make_unique<AudioMixer>(description_.audio, positionUs_, description_.durationUs); audio_->start();
        output_ = std::make_unique<AudioOutput>(*audio_, positionUs_, description_.durationUs, 1); output_->start();
    }
    pending_ = videoPending_ = false; buffering_ = true; baseUs_ = positionUs_; clock_.restart();
}
bool PlaybackController::videosFinished() const {
    return std::all_of(videos_.begin(), videos_.end(), [](const auto& v) { return v->isFinished(); });
}
void PlaybackController::startVideoLayers() {
    videos_.clear();
    if (videoSources_.size() > MaxVideoLayers) return;
    const auto budget = std::max<qint64>(1024 * 1024, videoBudgetBytes_ / std::max<qsizetype>(1, videoSources_.size()));
    auto decodeSize = maximumSize_;
    if (videoSources_.size() > 2 && std::any_of(videoSources_.begin(), videoSources_.end(), [](const auto& s) { return s.camera; }))
        decodeSize = decodeSize.scaled(QSize(640, 360), Qt::KeepAspectRatio);
    if (static_cast<qint64>(decodeSize.width()) * decodeSize.height() * 4 > budget) {
        const auto factor = std::sqrt(static_cast<double>(budget) / (static_cast<double>(decodeSize.width()) * decodeSize.height() * 4));
        decodeSize = {std::max(1, static_cast<int>(decodeSize.width() * factor)), std::max(1, static_cast<int>(decodeSize.height() * factor))};
    }
    for (const auto& source : videoSources_) {
        auto worker = std::make_unique<DecodeWorker>(source, positionUs_, false, mode_, nullptr, decodeSize, budget);
        worker->start(); videos_.push_back(std::move(worker));
    }
}
void PlaybackController::tick() {
    maxTickMs_ = std::max(maxTickMs_, static_cast<int>(heartbeat_.restart()));
    if (audio_ && meterClock_.elapsed() >= 100) {
        lastAudioMeters_ = audio_->takeMeters();
        if (output_) lastAudioMeters_["outputError"] = output_->stats()["error"];
        emit audioMetersChanged(lastAudioMeters_); meterClock_.restart();
    }
    if (pending_) {
        // Coalesce scrub requests into one latest target; the mixer bounds simultaneous decoders.
        if (!videosFinished() || (audio_ && !audio_->isFinished()) || (output_ && !output_->isFinished())) return;
        startPipelines();
    }
    if (buffering_) {
        if (synchronizedBuffering_) {
            // Drain towards the common target while the viewer and audio clock are paused.
            for (size_t i = 0; i < videos_.size(); ++i) {
                VideoFrame frame;
                if (videos_[i]->takeVideo(synchronizedTargetUs_, frame, dropped_)) { const auto id = videoSources_[static_cast<qsizetype>(i)].clipId; rawFrames_[id] = frame.image; rawFramePts_[id] = frame.ptsUs; frameDirty_ = true; }
            }
            if (std::any_of(videos_.begin(), videos_.end(), [this](const auto& v) { return !v->videoReadyThrough(synchronizedTargetUs_); })) return;
            positionUs_ = synchronizedTargetUs_; synchronizedBuffering_ = false;
        }
        if (std::any_of(videos_.begin(), videos_.end(), [](const auto& v) { return !v->ready(); }) || (output_ && !output_->ready())) return;
        buffering_ = false; baseUs_ = positionUs_; clock_.restart();
        if (output_) output_->setPlaying(playing_);
        emit stateChanged();
    }
    if (playing_) {
        const auto previousUs = positionUs_;
        if (output_ && output_->available()) positionUs_ = output_->positionUs();
        else {
            // Switch clocks without jumping if the audio endpoint disappears while playing.
            if (output_ && output_->isFinished()) {
                lastAudioOutput_ = output_->stats(); output_.reset(); if (audio_) audio_->cancel(); baseUs_ = positionUs_; clock_.restart();
            }
            positionUs_ = std::min(description_.durationUs, baseUs_ + clock_.nsecsElapsed() / 1000);
        }
        if (!videoPending_ && videos_.size() > 1 && std::any_of(videoSources_.begin(), videoSources_.end(), [](const auto& source) { return source.camera; }) &&
            std::any_of(videos_.begin(), videos_.end(), [this](const auto& v) { return !v->videoReadyThrough(positionUs_); })) {
            synchronizedTargetUs_ = positionUs_; positionUs_ = previousUs;
            synchronizedBuffering_ = buffering_ = true; ++synchronizedWaits_;
            if (output_) output_->setPlaying(false); emit stateChanged(); return;
        }
        if (activeVideoSources(description_, positionUs_) != videoSources_) {
            // Keep the audio master running continuously across video cuts and gaps.
            videoSources_ = activeVideoSources(description_, positionUs_);
            for (const auto& video : videos_) video->cancel();
            videoPending_ = true; rawFrames_.clear(); rawFramePts_.clear(); presentFrame();
        }
    }
    if (videoPending_ && videosFinished()) {
        videoPending_ = false; startVideoLayers();
    }
    if (!videoPending_) for (size_t i = 0; i < videos_.size(); ++i) {
        VideoFrame frame;
        if (videos_[i]->takeVideo(positionUs_, frame, dropped_)) { const auto id = videoSources_[static_cast<qsizetype>(i)].clipId; rawFrames_[id] = frame.image; rawFramePts_[id] = frame.ptsUs; frameDirty_ = true; }
    }
    // Animated titles/effects advance even when the source has no new decoded frame.
    if (playing_ && positionUs_ / 33334 != presentedPreviewFrame_) frameDirty_ = true;
    if (frameDirty_ && (!playing_ || positionUs_ / 33334 != presentedPreviewFrame_)) { ++presented_; presentFrame(); }
    if (activeTitleClips(description_, positionUs_) != titleClips_) presentFrame();
    if (playing_) {
        emit positionChanged(positionUs_);
        if (positionUs_ >= description_.durationUs) { pause(); qInfo() << "Playback complete" << stats(); }
    }
}
void PlaybackController::presentFrame() {
    frameDirty_ = false; presentedPreviewFrame_ = positionUs_ / 33334;
    titleClips_ = activeTitleClips(description_, positionUs_);
    emit frameReady(renderLayers(description_, rawFrames_, positionUs_, previewSize()));
}
QJsonObject PlaybackController::stats() const {
    QJsonObject result{{"positionUs", positionUs_}, {"playing", playing_}, {"buffering", pending_ || buffering_},
        {"presented", presented_}, {"dropped", dropped_}, {"maxUiTickGapMs", maxTickMs_}, {"pipelineStarts", pipelineStarts_},
        {"previewWidth", previewSize().width()}, {"previewHeight", previewSize().height()}, {"previewFpsLimit", 30}, {"videoBudgetBytes", videoBudgetBytes_}};
    result["synchronizedBufferWaits"] = synchronizedWaits_;
    if (!videos_.empty()) result["video"] = videos_.front()->stats();
    QJsonArray layers;
    for (const auto& video : videos_) layers.append(video->stats());
    result["videoLayers"] = layers;
    QJsonObject timestamps; for (auto it = rawFramePts_.cbegin(); it != rawFramePts_.cend(); ++it) timestamps[it.key()] = it.value(); result["videoPtsUs"] = timestamps;
    result["visualError"] = visualGraphError(description_);
    if (audio_) result["audioDecode"] = audio_->stats();
    if (output_) result["audioOutput"] = output_->stats();
    else if (!lastAudioOutput_.isEmpty()) result["audioOutput"] = lastAudioOutput_;
    return result;
}
}

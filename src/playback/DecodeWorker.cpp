#include "DecodeWorker.h"
#include "project/Effects.h"
#include <QElapsedTimer>
#include <QTransform>
#include <algorithm>
#include <memory>
#include <cmath>
#include <cstring>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
}
namespace editor::playback {
namespace {
QString avError(int code) { char b[AV_ERROR_MAX_STRING_SIZE]{}; av_strerror(code, b, sizeof(b)); return QString::fromUtf8(b); }
struct Resources {
    AVFormatContext* format = avformat_alloc_context();
    AVCodecContext* codec = nullptr;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* transferred = av_frame_alloc();
    AVFrame* preroll = av_frame_alloc();
    SwsContext* scaler = nullptr;
    SwrContext* resampler = nullptr;
    AVFilterGraph* tempo = nullptr;
    AVFilterContext *tempoInput = nullptr, *tempoOutput = nullptr;
    AVFrame* filtered = av_frame_alloc();
    ~Resources() { avfilter_graph_free(&tempo); av_frame_free(&filtered); swr_free(&resampler); sws_freeContext(scaler); av_frame_free(&frame); av_frame_free(&transferred);
        av_frame_free(&preroll); av_packet_free(&packet); avcodec_free_context(&codec); avformat_close_input(&format); }
};
struct Interrupt {
    DecodeWorker* worker;
    QElapsedTimer io;
    static int callback(void* p) { const auto* self = static_cast<Interrupt*>(p);
        return self->worker->isInterruptionRequested() || self->io.elapsed() > 10000; }
};
AVPixelFormat hardwareFormat(AVCodecContext*, const AVPixelFormat* formats) {
    for (; *formats != AV_PIX_FMT_NONE; ++formats) if (*formats == AV_PIX_FMT_D3D11) return *formats;
    return AV_PIX_FMT_NONE; // Retry the entire decoder in software, rather than silently mixing modes.
}
}
DecodeWorker::DecodeWorker(Source source, qint64 positionUs, bool audio, DecodeMode mode, QObject* parent, QSize maximumSize, qint64 videoBudgetBytes)
    : QThread(parent), source_(std::move(source)), positionUs_(positionUs), audio_(audio), mode_(mode), maximumSize_(maximumSize),
      videoBudgetBytes_(std::clamp<qint64>(videoBudgetBytes, 1, 128 * 1024 * 1024)) {}
DecodeWorker::~DecodeWorker() { cancel(); wait(); }
void DecodeWorker::cancel() { requestInterruption(); space_.wakeAll(); }
bool DecodeWorker::push(VideoFrame frame) {
    QMutexLocker lock(&mutex_);
    const auto bytes = frame.image.sizeInBytes();
    if (bytes > videoBudgetBytes_) { error_ = "Video frame exceeds queue memory budget; choose a lower preview quality."; return false; }
    while ((video_.size() >= VideoLimit || queuedVideoBytes_ + bytes > videoBudgetBytes_) && !isInterruptionRequested()) space_.wait(&mutex_, 50);
    if (isInterruptionRequested()) return false;
    queuedVideoBytes_ += bytes; videoBytesHighWater_ = std::max(videoBytesHighWater_, queuedVideoBytes_);
    latestVideoPts_ = frame.ptsUs; video_.push_back(std::move(frame)); ++decoded_; highWater_ = std::max(highWater_, static_cast<int>(video_.size())); return true;
}
bool DecodeWorker::push(AudioBlock block) {
    QMutexLocker lock(&mutex_);
    while (audioQueue_.size() >= AudioLimit && !isInterruptionRequested()) space_.wait(&mutex_, 50);
    if (isInterruptionRequested()) return false;
    audioQueue_.push_back(std::move(block)); ++decoded_; highWater_ = std::max(highWater_, static_cast<int>(audioQueue_.size())); return true;
}
bool DecodeWorker::takeVideo(qint64 positionUs, VideoFrame& frame, int& dropped) {
    QMutexLocker lock(&mutex_); bool taken = false;
    while (!video_.empty() && video_.front().ptsUs <= positionUs) {
        if (taken) ++dropped;
        queuedVideoBytes_ -= video_.front().image.sizeInBytes();
        frame = std::move(video_.front()); video_.pop_front(); taken = true;
    }
    if (taken) space_.wakeAll(); return taken;
}
bool DecodeWorker::takeAudio(AudioBlock& block) {
    QMutexLocker lock(&mutex_); if (audioQueue_.empty()) return false;
    block = std::move(audioQueue_.front()); audioQueue_.pop_front(); space_.wakeAll(); return true;
}
bool DecodeWorker::takeNextVideo(VideoFrame& frame) {
    QMutexLocker lock(&mutex_); if (video_.empty()) return false;
    queuedVideoBytes_ -= video_.front().image.sizeInBytes();
    frame = std::move(video_.front()); video_.pop_front(); space_.wakeAll(); return true;
}
bool DecodeWorker::ready() const { QMutexLocker lock(&mutex_); return audio_ ? !audioQueue_.empty() || finished_ : !video_.empty() || finished_; }
bool DecodeWorker::videoReadyThrough(qint64 positionUs, qint64 toleranceUs) const { QMutexLocker lock(&mutex_); return finished_ || latestVideoPts_ >= positionUs - toleranceUs; }
bool DecodeWorker::done() const { QMutexLocker lock(&mutex_); return finished_ && video_.empty() && audioQueue_.empty(); }
QJsonObject DecodeWorker::stats() const {
    QMutexLocker lock(&mutex_);
    return {{"decoded", decoded_}, {"firstFrameMs", firstMs_}, {"conversionUs", convertUs_}, {"queueHighWater", highWater_},
        {"queueLimit", static_cast<int>(audio_ ? AudioLimit : VideoLimit)}, {"queueBytes", queuedVideoBytes_},
        {"queueBytesHighWater", videoBytesHighWater_}, {"queueBudgetBytes", videoBudgetBytes_}, {"path", path_}, {"fallback", fallback_}, {"error", error_}};
}
void DecodeWorker::run() {
    if (source_.nestedSource) {
        // Collapse every nesting level before decoding. One relay bounds queues independently of depth.
        auto input = source_; qint64 offsetUs = 0, offsetSamples = 0;
        while (input.nestedSource) { offsetUs += input.nestedOffsetUs; offsetSamples += input.nestedOffsetSamples; input = *input.nestedSource; }
        DecodeWorker decoder(input, positionUs_ + offsetUs, audio_, mode_, nullptr, maximumSize_, videoBudgetBytes_);
        QElapsedTimer elapsed; elapsed.start(); decoder.start();
        auto signedSamples = [](qint64 us) { return static_cast<qint64>(std::llround(static_cast<long double>(us) * 48000 / 1000000)); };
        while (!isInterruptionRequested()) {
            bool consumed = false;
            if (audio_) {
                AudioBlock block;
                if (decoder.takeAudio(block)) {
                    consumed = true;
                    const auto first = signedSamples(block.ptsUs) - offsetSamples;
                    const auto begin = std::max({first, source_.startSample, signedSamples(positionUs_)});
                    const auto end = std::min(first + block.samples.size() / 2, source_.endSample);
                    if (first >= source_.endSample) break;
                    if (end > begin) {
                        block.samples = block.samples.mid((begin - first) * 2, (end - begin) * 2);
                        block.ptsUs = project::scaleTime(begin, 1000000, 48000, project::Rounding::Nearest).value_or(0);
                        if (!push(std::move(block))) break;
                    }
                }
            } else {
                VideoFrame frame;
                if (decoder.takeNextVideo(frame)) {
                    consumed = true; frame.ptsUs -= offsetUs;
                    if (frame.ptsUs >= source_.endUs) break;
                    frame.ptsUs = std::max(frame.ptsUs, source_.startUs);
                    if (!push(std::move(frame))) break;
                }
            }
            if (consumed) { QMutexLocker lock(&mutex_); if (firstMs_ < 0) firstMs_ = elapsed.elapsed(); }
            else if (decoder.done()) break;
            else msleep(1);
        }
        decoder.cancel(); decoder.wait(); const auto report = decoder.stats();
        QMutexLocker lock(&mutex_); path_ = report["path"].toString(); fallback_ = report["fallback"].toString();
        if (!isInterruptionRequested()) error_ = report["error"].toString(); finished_ = true; return;
    }
    QString error;
    if (!decode(mode_, error) && mode_ != DecodeMode::Cpu && !audio_ && !isInterruptionRequested()) {
        { QMutexLocker lock(&mutex_); fallback_ = error; path_ = "CPU fallback"; video_.clear(); queuedVideoBytes_ = 0; }
        decode(DecodeMode::Cpu, error);
    }
    QMutexLocker lock(&mutex_); if (isInterruptionRequested()) error_.clear(); else if (!error.isEmpty()) error_ = error; finished_ = true;
}
bool DecodeWorker::decode(DecodeMode mode, QString& failure) {
    failure.clear(); QElapsedTimer elapsed; elapsed.start(); Resources r; Interrupt interrupt{this, {}}; interrupt.io.start();
    auto fail = [&](int code, const QString& operation) { failure = operation + ": " + avError(code); return false; };
    if (!r.format || !r.packet || !r.frame || !r.transferred || !r.preroll) { failure = "Cannot allocate decoder"; return false; }
    r.format->interrupt_callback = {Interrupt::callback, &interrupt}; r.format->probesize = 8 * 1024 * 1024;
    r.format->max_analyze_duration = 3 * AV_TIME_BASE; r.format->max_streams = 64;
    r.format->max_index_size = 1024 * 1024;
    AVDictionary* options = nullptr; av_dict_set(&options, "protocol_whitelist", "file", 0);
    int code = avformat_open_input(&r.format, source_.path.toUtf8().constData(), nullptr, &options); av_dict_free(&options);
    if (code < 0) return fail(code, "Open media");
    interrupt.io.restart(); code = avformat_find_stream_info(r.format, nullptr);
    if (code < 0) return fail(code, "Read stream metadata");
    if (source_.streamIndex < 0 || static_cast<unsigned>(source_.streamIndex) >= r.format->nb_streams) { failure = "Selected stream no longer exists"; return false; }
    auto* stream = r.format->streams[source_.streamIndex];
    if (stream->codecpar->codec_type != (audio_ ? AVMEDIA_TYPE_AUDIO : AVMEDIA_TYPE_VIDEO)) { failure = "Selected stream has changed kind"; return false; }
    const auto* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) { failure = "Decoder unavailable"; return false; }
    r.codec = avcodec_alloc_context3(codec);
    if (!r.codec) { failure = "Cannot allocate codec"; return false; }
    code = avcodec_parameters_to_context(r.codec, stream->codecpar); if (code < 0) return fail(code, "Copy codec parameters");
    r.codec->thread_count = 2; r.codec->max_pixels = 4096LL * 4096;
    if (audio_ && (r.codec->sample_rate < 8000 || r.codec->sample_rate > 384000 || r.codec->ch_layout.nb_channels > 64)) { failure = "Audio exceeds preview limits"; return false; }
    if (!audio_ && mode != DecodeMode::Cpu) {
        if (mode == DecodeMode::UnavailableForTest) { failure = "Injected unavailable acceleration"; return false; }
        bool supported = false;
        for (int i = 0; const auto* config = avcodec_get_hw_config(codec, i); ++i)
            if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && config->device_type == AV_HWDEVICE_TYPE_D3D11VA) supported = true;
        if (!supported) { failure = "Codec has no D3D11VA configuration"; return false; }
        code = av_hwdevice_ctx_create(&r.codec->hw_device_ctx, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
        if (code < 0) return fail(code, "Create D3D11VA device");
        r.codec->get_format = hardwareFormat;
        QMutexLocker lock(&mutex_); path_ = "D3D11VA + CPU transfer/conversion";
    }
    code = avcodec_open2(r.codec, codec, nullptr); if (code < 0) return fail(code, "Open codec");
    const qint64 startTicks = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    const bool retimedAudio = audio_ && source_.timeClip.has_value();
    const auto* sourceSpeed = retimedAudio ? project::speedEffect(*source_.timeClip) : nullptr;
    const bool rampedAudio = sourceSpeed && !sourceSpeed->keyframes.value("rate").isEmpty();
    const qint64 targetUs = retimedAudio ? source_.inUs : sourceMediaUs(source_, positionUs_);
    // AAC and resampling need decoder/filter preroll. Seek earlier, then trim PCM at sample resolution.
    const auto seekUs = audio_ ? std::max<qint64>(0, targetUs - 100000) : targetUs;
    const auto targetTicks = av_rescale_q(seekUs, AV_TIME_BASE_Q, stream->time_base) + startTicks;
    interrupt.io.restart(); code = av_seek_frame(r.format, source_.streamIndex, targetTicks, AVSEEK_FLAG_BACKWARD);
    if (code < 0 && targetUs > 0) return fail(code, "Seek media");
    avcodec_flush_buffers(r.codec);
    qint64 lastPts = AV_NOPTS_VALUE;
    const auto rate = av_guess_frame_rate(r.format, stream, nullptr);
    const qint64 guessedDuration = !audio_ && rate.num > 0 ? av_rescale_q(1, av_inv_q(rate), AV_TIME_BASE_Q) : 33333;
    qint64 prerollPts = AV_NOPTS_VALUE; bool reachedTarget = false, end = false;
    const auto sampleAt = [](qint64 us) { return av_rescale_q(us, AV_TIME_BASE_Q, AVRational{1, 48000}); };
    const auto sourceStartSample = source_.startSample >= 0 ? source_.startSample : sampleAt(source_.startUs);
    const auto sourceEndSample = source_.endSample >= 0 ? source_.endSample : sampleAt(source_.endUs);
    qint64 tempoSamples = 0, tempoInputSamples = 0;
    if (retimedAudio) {
        // Fixed speed uses WSOLA; ramps use Rubber Band's continuous ratio updates.
        r.tempo = avfilter_graph_alloc(); if (!r.tempo) { failure = "Cannot allocate pitch-preserving tempo graph"; return false; }
        r.tempo->nb_threads = 1;
        code = avfilter_graph_create_filter(&r.tempoInput, avfilter_get_by_name("abuffer"), "input",
            "time_base=1/48000:sample_rate=48000:sample_fmt=flt:channel_layout=stereo", nullptr, r.tempo);
        if (code < 0) return fail(code, "Create tempo input");
        auto* previous = r.tempoInput;
        const auto initial = QByteArray::number(std::clamp(std::cbrt(sourceTempo(source_, source_.inUs)), 0.5, 2.0), 'g', 15);
        for (int i = 0; i < (rampedAudio ? 1 : 3); ++i) {
            AVFilterContext* stage = nullptr; const auto name = QByteArray("tempo") + QByteArray::number(i);
            const auto filterOptions = rampedAudio ? QByteArray("tempo=") + QByteArray::number(sourceTempo(source_, source_.inUs), 'g', 15) + ":pitch=1:transients=crisp:window=short:smoothing=on:pitchq=quality:channels=together" : initial;
            code = avfilter_graph_create_filter(&stage, avfilter_get_by_name(rampedAudio ? "rubberband" : "atempo"), name.constData(), filterOptions.constData(), nullptr, r.tempo);
            if (code < 0) return fail(code, "Create pitch-preserving tempo stage");
            code = avfilter_link(previous, 0, stage, 0); if (code < 0) return fail(code, "Link tempo stage"); previous = stage;
        }
        AVFilterContext* packed = nullptr;
        code = avfilter_graph_create_filter(&packed, avfilter_get_by_name("aformat"), "packed", "sample_fmts=flt:sample_rates=48000:channel_layouts=stereo", nullptr, r.tempo);
        if (code < 0) return fail(code, "Create packed tempo output");
        code = avfilter_link(previous, 0, packed, 0); if (code < 0) return fail(code, "Pack tempo output"); previous = packed;
        code = avfilter_graph_create_filter(&r.tempoOutput, avfilter_get_by_name("abuffersink"), "output", nullptr, nullptr, r.tempo);
        if (code < 0) return fail(code, "Create tempo output");
        code = avfilter_link(previous, 0, r.tempoOutput, 0); if (code < 0) return fail(code, "Link tempo output");
        code = avfilter_graph_config(r.tempo, nullptr); if (code < 0) return fail(code, "Configure tempo graph");
        QMutexLocker lock(&mutex_); path_ = rampedAudio ? "CPU + pitch-preserving Rubber Band ramp" : "CPU + pitch-preserving atempo";
    }
    auto drainTempo = [&]() {
        while (!isInterruptionRequested()) {
            av_frame_unref(r.filtered); const int status = av_buffersink_get_frame(r.tempoOutput, r.filtered);
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) return true;
            if (status < 0) return fail(status, "Read tempo output");
            const auto first = sourceStartSample + tempoSamples; const auto count = r.filtered->nb_samples; tempoSamples += count;
            const int skip = static_cast<int>(std::clamp<qint64>(sampleAt(positionUs_) - first, 0, count));
            const int until = static_cast<int>(std::clamp<qint64>(sourceEndSample - first, 0, count));
            const auto* samples = reinterpret_cast<const float*>(r.filtered->data[0]);
            for (int offset = skip; offset < until; offset += 4096) {
                const int length = std::min(4096, until - offset); QVector<float> pcm(length * 2);
                std::memcpy(pcm.data(), samples + offset * 2, static_cast<size_t>(length) * 2 * sizeof(float));
                if (!push(AudioBlock{av_rescale_q(first + offset, AVRational{1, 48000}, AV_TIME_BASE_Q), std::move(pcm)})) return false;
            }
        }
        return false;
    };
    auto feedTempo = [&](const float* pcm, int count, qint64 mediaSample) {
        for (int offset = 0; offset < count && !isInterruptionRequested(); offset += 512) {
            const int length = std::min(512, count - offset);
            const auto mediaUs = av_rescale_q(mediaSample + offset + length / 2, AVRational{1, 48000}, AV_TIME_BASE_Q);
            if (rampedAudio) {
                const auto rateText = QByteArray::number(sourceTempo(source_, mediaUs), 'g', 15);
                code = avfilter_graph_send_command(r.tempo, "tempo0", "tempo", rateText.constData(), nullptr, 0, 0);
                if (code < 0) return fail(code, "Update pitch-preserving speed ramp");
            }
            AVFrame* input = av_frame_alloc(); if (!input) { failure = "Cannot allocate tempo audio frame"; return false; }
            input->format = AV_SAMPLE_FMT_FLT; input->sample_rate = 48000; av_channel_layout_default(&input->ch_layout, 2);
            input->nb_samples = length; input->pts = tempoInputSamples; tempoInputSamples += length;
            code = av_frame_get_buffer(input, 0);
            if (code >= 0) { std::memcpy(input->data[0], pcm + offset * 2, static_cast<size_t>(length) * 2 * sizeof(float)); code = av_buffersrc_add_frame_flags(r.tempoInput, input, 0); }
            av_frame_free(&input); if (code < 0) return fail(code, "Feed tempo audio");
            if (!drainTempo()) return false;
        }
        return !isInterruptionRequested();
    };
    auto present = [&](AVFrame* frame, qint64 mapped) {
        if (frame->format == AV_PIX_FMT_D3D11) {
            av_frame_unref(r.transferred); const int transferred = av_hwframe_transfer_data(r.transferred, frame, 0);
            if (transferred < 0) return fail(transferred, "Transfer hardware frame"); frame = r.transferred;
        }
        QElapsedTimer conversion; conversion.start();
        const double factor = std::min({1.0, static_cast<double>(maximumSize_.width()) / frame->width, static_cast<double>(maximumSize_.height()) / frame->height});
        const int width = std::max(1, static_cast<int>(frame->width * factor)), height = std::max(1, static_cast<int>(frame->height * factor));
        QImage image(width, height, QImage::Format_RGB32);
        r.scaler = sws_getCachedContext(r.scaler, frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
            width, height, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!r.scaler || image.isNull()) { failure = "Cannot allocate preview conversion"; return false; }
        const int matrix = frame->colorspace == AVCOL_SPC_BT709 ? SWS_CS_ITU709 : SWS_CS_DEFAULT;
        sws_setColorspaceDetails(r.scaler, sws_getCoefficients(matrix), frame->color_range == AVCOL_RANGE_JPEG,
            sws_getCoefficients(matrix), 1, 0, 1 << 16, 1 << 16);
        uint8_t* destination[]{image.bits()}; int strides[]{static_cast<int>(image.bytesPerLine())};
        sws_scale(r.scaler, frame->data, frame->linesize, 0, frame->height, destination, strides);
        if (source_.rotation) image = image.transformed(QTransform().rotate(-source_.rotation));
        { QMutexLocker lock(&mutex_); convertUs_ += conversion.nsecsElapsed() / 1000; if (firstMs_ < 0) firstMs_ = elapsed.elapsed(); }
        return push(VideoFrame{mapped, std::move(image)});
    };
    auto consume = [&]() -> bool {
        while (!isInterruptionRequested()) {
            code = avcodec_receive_frame(r.codec, r.frame);
            if (code == AVERROR(EAGAIN) || code == AVERROR_EOF) return true;
            if (code < 0) return fail(code, "Decode frame");
            auto* frame = r.frame;
            const auto timestamp = frame->best_effort_timestamp;
            qint64 pts = timestamp == AV_NOPTS_VALUE ? (lastPts == AV_NOPTS_VALUE ? 0 : lastPts + guessedDuration)
                : av_rescale_q(timestamp - startTicks, stream->time_base, AV_TIME_BASE_Q);
            lastPts = pts;
            const qint64 mapped = sourceTimelineUs(source_, pts);
            if ((source_.sourceSpanUs > 0 && pts >= source_.inUs + source_.sourceSpanUs) || mapped >= source_.endUs) { end = true; av_frame_unref(r.frame); return true; }
            if (audio_) {
                if (frame->nb_samples > 65536 || frame->ch_layout.nb_channels > 64) { failure = "Audio frame exceeds preview limit"; return false; }
                if (!r.resampler) {
                    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
                    code = swr_alloc_set_opts2(&r.resampler, &stereo, AV_SAMPLE_FMT_FLT, 48000, &frame->ch_layout,
                        static_cast<AVSampleFormat>(frame->format), frame->sample_rate, 0, nullptr);
                    if (code >= 0) code = swr_init(r.resampler); if (code < 0) return fail(code, "Resample audio");
                }
                const auto delay = swr_get_delay(r.resampler, 48000);
                const auto needed = av_rescale_rnd(frame->nb_samples, 48000, frame->sample_rate, AV_ROUND_UP) + delay;
                if (needed > 400000) { failure = "Resampler exceeds preview limit"; return false; }
                QVector<float> samples(static_cast<qsizetype>(needed * 2));
                uint8_t* output[]{reinterpret_cast<uint8_t*>(samples.data())};
                const int count = swr_convert(r.resampler, output, static_cast<int>(needed), const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
                if (count < 0) return fail(count, "Convert audio");
                // Round absolute boundaries once, never subtract already-rounded microseconds.
                const auto sampleAt = [](qint64 us) { return av_rescale_q(us, AV_TIME_BASE_Q, AVRational{1, 48000}); };
                const auto sourceStart = source_.startSample >= 0 ? source_.startSample : sampleAt(source_.startUs);
                const auto sourceEnd = source_.endSample >= 0 ? source_.endSample : sampleAt(source_.endUs);
                const qint64 frameSample = timestamp == AV_NOPTS_VALUE ? sampleAt(pts) : av_rescale_q(timestamp - startTicks, stream->time_base, AVRational{1, 48000});
                if (retimedAudio) {
                    const auto first = frameSample - delay;
                    const int skip = static_cast<int>(std::clamp<qint64>(sampleAt(source_.inUs) - first, 0, count));
                    const int usable = static_cast<int>(std::clamp<qint64>(sampleAt(source_.inUs + source_.sourceSpanUs) - first, 0, count));
                    if (usable > skip && !feedTempo(samples.constData() + skip * 2, usable - skip, first + skip)) return false;
                    av_frame_unref(r.frame); continue;
                }
                const qint64 blockStart = sourceStart + frameSample - sampleAt(source_.inUs) - delay;
                const int skip = static_cast<int>(std::clamp<qint64>(sampleAt(positionUs_) - blockStart, 0, count));
                const int usable = static_cast<int>(std::clamp<qint64>(sourceEnd - blockStart, 0, count));
                for (int offset = skip; offset < usable; offset += 4096) {
                    const int length = std::min(4096, usable - offset);
                    if (!push(AudioBlock{av_rescale_q(blockStart + offset, AVRational{1, 48000}, AV_TIME_BASE_Q), samples.sliced(offset * 2, length * 2)})) return false;
                }
            } else {
                if (!reachedTarget && mapped < positionUs_) {
                    av_frame_unref(r.preroll); code = av_frame_ref(r.preroll, frame);
                    if (code < 0) return fail(code, "Retain seek preroll"); prerollPts = mapped;
                } else {
                    if (!reachedTarget && prerollPts != AV_NOPTS_VALUE && mapped > positionUs_ && !present(r.preroll, prerollPts)) return false;
                    prerollPts = AV_NOPTS_VALUE; av_frame_unref(r.preroll); reachedTarget = true;
                    if (!present(frame, mapped)) return false;
                }
            }
            { QMutexLocker lock(&mutex_); if (firstMs_ < 0 && decoded_ > 0) firstMs_ = elapsed.elapsed(); }
            av_frame_unref(r.frame);
        }
        return false;
    };
    while (!isInterruptionRequested() && !end) {
        interrupt.io.restart(); code = av_read_frame(r.format, r.packet);
        if (code < 0) {
            if (code != AVERROR_EOF) return fail(code, "Read packet");
            code = avcodec_send_packet(r.codec, nullptr); if (code < 0 && code != AVERROR_EOF) return fail(code, "Flush decoder");
            if (!consume()) return false;
            break;
        }
        if (r.packet->stream_index == source_.streamIndex) {
            if (r.packet->size > 64 * 1024 * 1024) { failure = "Compressed packet exceeds 64 MiB preview limit"; return false; }
            code = avcodec_send_packet(r.codec, r.packet);
            if (code == AVERROR(EAGAIN)) { if (!consume()) return false; code = avcodec_send_packet(r.codec, r.packet); }
            if (code < 0) return fail(code, "Send packet");
            if (!consume()) return false;
        }
        av_packet_unref(r.packet);
    }
    if (prerollPts != AV_NOPTS_VALUE && !isInterruptionRequested()) present(r.preroll, prerollPts);
    if (retimedAudio && !isInterruptionRequested()) {
        code = av_buffersrc_add_frame_flags(r.tempoInput, nullptr, 0); if (code < 0) return fail(code, "Flush tempo graph");
        if (!drainTempo()) return false;
    }
    // The small resampler tail is silence-padded by the sink at the clip boundary (<1 ms).
    return !isInterruptionRequested();
}
}

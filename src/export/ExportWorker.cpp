#include "ExportWorker.h"
#include "EncoderConfig.h"
#include "playback/AudioMixer.h"
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QResource>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <numeric>
#include <vector>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

static void initializeExportResources() { Q_INIT_RESOURCE(export_presets); }
namespace editor::exporting {
QVector<Preset> presets() {
    static const bool initialized = [] { initializeExportResources(); return true; }();
    Q_UNUSED(initialized);
    QFile file(":/export/presets.json"); if (!file.open(QIODevice::ReadOnly)) return {};
    QVector<Preset> result;
    for (const auto& entry : QJsonDocument::fromJson(file.readAll()).array()) {
        const auto o = entry.toObject(); Preset p;
        p.id = o["id"].toString(); p.label = o["label"].toString(); p.container = o["container"].toString();
        p.videoEncoder = o["videoEncoder"].toString(); p.audioEncoder = o["audioEncoder"].toString();
        for (const auto& s : o["speeds"].toArray()) p.speeds.append(s.toString());
        p.quality = o["quality"].toInt(); p.audioBitrate = o["audioBitrate"].toInt(); result.append(p);
    }
    return result;
}
namespace {
QString resolvedPath(const QString& path) {
    const QFileInfo info(path);
    const auto canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}
QString avError(int code) { char text[AV_ERROR_MAX_STRING_SIZE]{}; av_strerror(code, text, sizeof(text)); return QString::fromUtf8(text); }
void requireAv(int code, const char* operation) {
    if (code < 0) throw std::runtime_error((QString::fromUtf8(operation) + ": " + avError(code)).toStdString());
}
struct Encoder {
    AVCodecContext* codec = nullptr;
    AVFrame* frame = av_frame_alloc();
    AVStream* stream = nullptr;
    ~Encoder() { av_frame_free(&frame); avcodec_free_context(&codec); }
};
struct Muxer {
    AVFormatContext* format = nullptr;
    AVIOContext* io = nullptr;
    AVPacket* packet = av_packet_alloc();
    SwsContext* scaler = nullptr;
    SwrContext* resampler = nullptr;
    ~Muxer() {
        sws_freeContext(scaler); swr_free(&resampler); av_packet_free(&packet);
        avformat_free_context(format);
        if (io) { av_freep(&io->buffer); avio_context_free(&io); }
    }
};
int writeFile(void* opaque, const uint8_t* bytes, int length) {
    auto* file = static_cast<QSaveFile*>(opaque);
    return file->write(reinterpret_cast<const char*>(bytes), length) == length ? length : AVERROR(EIO);
}
int64_t seekFile(void* opaque, int64_t offset, int whence) {
    auto* file = static_cast<QSaveFile*>(opaque);
    if (whence == AVSEEK_SIZE) return file->size();
    whence &= ~AVSEEK_FORCE;
    const auto target = whence == SEEK_SET ? offset : whence == SEEK_CUR ? file->pos() + offset : file->size() + offset;
    return file->seek(target) ? target : AVERROR(EIO);
}
}
QString validateExport(const playback::RenderDescription& d, const project::ExportSettings& s) {
    if (!d.sequence || d.sequence->durationFrames <= 0) return "The active sequence is empty. Add clips before exporting.";
    if (const auto error = exportCombinationError(s); !error.isEmpty()) return error;
    if (const auto error = compiledCombinationError(s); !error.isEmpty()) return error;
    const auto extension = containerExtension(s.container);
    if (s.outputPath.trimmed().isEmpty()) return "Choose an output file using Browse or enter a destination path.";
    if (QFileInfo(s.outputPath).suffix().compare(extension, Qt::CaseInsensitive) != 0)
        return "Output file extension conflicts with " + s.container + ". Choose a file ending in ." + extension + ".";
    const auto output = resolvedPath(s.outputPath);
    for (const auto& m : d.media) if (resolvedPath(m.path).compare(output, Qt::CaseInsensitive) == 0) return "The output path is a source media file. Choose a different path.";
    for (const auto& source : d.video + (s.audioEnabled ? d.audio : QVector<playback::Source>{}))
        if (!QFileInfo(source.path).isFile()) return "Source media is missing: " + source.path;
    if (const auto error = playback::visualGraphError(d); !error.isEmpty()) return error;
    if (!project::framesToTicks(d.sequence->durationFrames, d.frameRate, {s.frameRate.denominator, s.frameRate.numerator}, project::Rounding::Ceil) ||
        !project::framesToSamples(d.sequence->durationFrames, d.frameRate, 48000, project::Rounding::Nearest)) return "Sequence duration exceeds export timing limits.";
    return {};
}
ExportWorker::ExportWorker(playback::RenderDescription d, project::ExportSettings s, QObject* parent)
    : QThread(parent), description_(std::move(d)), settings_(std::move(s)) {}
ExportWorker::~ExportWorker() { cancel(); wait(); }
void ExportWorker::cancel() { requestInterruption(); }
QJsonObject ExportWorker::result() const { QMutexLocker lock(&mutex_); return result_; }
void ExportWorker::run() {
    QElapsedTimer elapsed; elapsed.start(); qint64 frames = 0, samples = 0; audioMetrics_ = {};
    QString error;
    try { error = render(frames, samples); }
    catch (const std::exception& e) { error = QString::fromUtf8(e.what()); }
    QMutexLocker lock(&mutex_);
    result_ = {{"passed", error.isEmpty()}, {"cancelled", error == "Export cancelled."}, {"error", error},
        {"path", settings_.outputPath}, {"frames", frames}, {"samples", samples}, {"elapsedMs", elapsed.elapsed()}, {"audioMix", audioMetrics_},
        {"requestedEncoder", settings_.videoEncoder}, {"actualEncoder", encoder_.encoder}, {"pixelFormat", encoder_.pixelFormat},
        {"compatibilityProfile", settings_.compatibilityProfile}, {"capabilityWarning", encoder_.warning},
        {"configuration", QJsonDocument::fromJson(probeSettings(settings_, encoder_.encoder)).object()}};
}
QString ExportWorker::render(qint64& frames, qint64& samples) {
    const auto invalid = validateExport(description_, settings_); if (!invalid.isEmpty()) return invalid;
    QJsonArray probes;
    for (const auto& candidate : encoderCandidates(settings_)) {
        auto probe = probeEncoder(settings_, candidate, [this] { return isInterruptionRequested(); }); probes.append(probe);
        if (isInterruptionRequested()) return "Export cancelled.";
        if (probe["usable"].toBool()) break;
    }
    encoder_ = resolveEncoder(settings_, probes); if (!encoder_.error.isEmpty()) return encoder_.error;
    QSaveFile output(settings_.outputPath); output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) return "Cannot create export: " + output.errorString();
    // QSaveFile owns a same-directory temporary file. Commit only after all encoders and the muxer finish.
    Muxer mux; Encoder video, audio;
    requireAv(avformat_alloc_output_context2(&mux.format, nullptr, settings_.container.toUtf8().constData(), nullptr), "Create export muxer");
    auto* buffer = static_cast<unsigned char*>(av_malloc(65536));
    if (!buffer || !mux.packet || !video.frame || !audio.frame) { av_free(buffer); return "Cannot allocate export buffers."; }
    mux.io = avio_alloc_context(buffer, 65536, 1, &output, nullptr, writeFile, seekFile);
    if (!mux.io) { av_free(buffer); return "Cannot allocate export file writer."; }
    mux.format->pb = mux.io; mux.format->flags |= AVFMT_FLAG_CUSTOM_IO;
    auto openEncoder = [&](Encoder& encoder, const QString& name, bool isAudio) {
        const auto* codec = avcodec_find_encoder_by_name(name.toUtf8().constData());
        if (!codec) throw std::runtime_error(("Encoder unavailable: " + name).toStdString());
        encoder.codec = avcodec_alloc_context3(codec); encoder.stream = avformat_new_stream(mux.format, nullptr);
        if (!encoder.codec || !encoder.stream) throw std::runtime_error("Cannot allocate encoder.");
        auto* c = encoder.codec; c->thread_count = 2;
        if (mux.format->oformat->flags & AVFMT_GLOBALHEADER) c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (isAudio) configureAudioEncoder(c, settings_); else configureVideoEncoder(c, settings_, encoder_.encoder);
        checkAv(avcodec_open2(c, codec, nullptr), "Open encoder " + name + "; choose Software or refresh capabilities if the device changed");
        encoder.stream->time_base = c->time_base;
        if (!isAudio) encoder.stream->avg_frame_rate = c->framerate;
        requireAv(avcodec_parameters_from_context(encoder.stream->codecpar, c), "Copy output stream parameters");
        if (!isAudio && settings_.videoCodec == "hevc" && settings_.container == "mp4") encoder.stream->codecpar->codec_tag = MKTAG('h','v','c','1');
        auto* f = encoder.frame;
        if (isAudio) { f->format = c->sample_fmt; f->sample_rate = 48000;
            requireAv(av_channel_layout_copy(&f->ch_layout, &c->ch_layout), "Copy audio layout"); f->nb_samples = c->frame_size > 0 ? c->frame_size : 1024;
            requireAv(av_frame_get_buffer(f, 32), "Allocate output audio");
            checkAv(swr_alloc_set_opts2(&mux.resampler, &c->ch_layout, c->sample_fmt, 48000, &c->ch_layout, AV_SAMPLE_FMT_FLT, 48000, 0, nullptr), "Configure export audio conversion");
            checkAv(swr_init(mux.resampler), "Initialize export audio conversion");
        } else prepareVideoFrame(c, f);
    };
    openEncoder(video, encoder_.encoder, false);
    if (settings_.audioEnabled) openEncoder(audio, settings_.audioEncoder, true);
    requireAv(avformat_write_header(mux.format, nullptr), "Write MP4 header");
    auto encode = [&](Encoder& encoder, AVFrame* frame) {
        requireAv(sendEncoderFrame(encoder.codec, frame), "Encode frame");
        while (true) {
            const int code = avcodec_receive_packet(encoder.codec, mux.packet);
            if (code == AVERROR(EAGAIN) || code == AVERROR_EOF) break;
            requireAv(code, "Receive encoded packet");
            // Constant-rate video packets each cover one complete output frame, including delayed B frames.
            if (encoder.codec->codec_type == AVMEDIA_TYPE_VIDEO && mux.packet->duration <= 0) mux.packet->duration = 1;
            av_packet_rescale_ts(mux.packet, encoder.codec->time_base, encoder.stream->time_base);
            mux.packet->stream_index = encoder.stream->index;
            requireAv(av_interleaved_write_frame(mux.format, mux.packet), "Write MP4 packet");
            av_packet_unref(mux.packet);
        }
    };
    const auto totalFrames = *project::framesToTicks(description_.sequence->durationFrames, description_.frameRate,
        {settings_.frameRate.denominator, settings_.frameRate.numerator}, project::Rounding::Ceil);
    const auto totalSamples = settings_.audioEnabled ? *project::framesToSamples(description_.sequence->durationFrames,
        description_.frameRate, 48000, project::Rounding::Nearest) : 0;
    std::unique_ptr<playback::AudioMixer> mixer;
    if (settings_.audioEnabled) { mixer = std::make_unique<playback::AudioMixer>(description_.audio, 0, description_.durationUs, totalSamples); mixer->start(); }
    struct Layer {
        playback::Source source;
        std::unique_ptr<playback::DecodeWorker> decoder;
        playback::VideoFrame next, held;
        bool hasNext = false;
    };
    std::vector<Layer> layers;
    QVector<playback::Source> sources;
    playback::AudioBlock block; qsizetype blockOffset = 0;
    int lastPercent = -1;
    while (frames < totalFrames || samples < totalSamples) {
        if (isInterruptionRequested()) return "Export cancelled.";
        const auto us = project::framesToTicks(frames, settings_.frameRate, {1, 1000000}, project::Rounding::Nearest).value_or(0);
        const auto audioUs = project::scaleTime(samples, 1000000, 48000, project::Rounding::Nearest).value_or(0);
        if (frames < totalFrames && (samples >= totalSamples || us <= audioUs)) {
            const auto active = playback::activeVideoSources(description_, us);
            if (active != sources) {
                layers.clear(); sources = active;
                for (const auto& source : sources) {
                    Layer layer; layer.source = source;
                    layer.decoder = std::make_unique<playback::DecodeWorker>(source, us, false, playback::DecodeMode::Cpu, nullptr, QSize(4096, 4096));
                    layer.decoder->start(); layers.push_back(std::move(layer));
                }
            }
            QMap<QString, QImage> images;
            for (auto& layer : layers) {
                auto& decoder = layer.decoder; auto& next = layer.next; auto& held = layer.held; auto& hasNext = layer.hasNext;
                while (!isInterruptionRequested()) {
                    if (!hasNext) {
                        if (decoder->takeNextVideo(next)) hasNext = true;
                        else if (decoder->done()) {
                            const auto error = decoder->stats()["error"].toString();
                            if (!error.isEmpty()) return "Video " + layer.source.clipId + ": " + error;
                            break;
                        } else { msleep(1); continue; }
                    }
                    if (next.ptsUs > us) break;
                    held = std::move(next); hasNext = false;
                }
                if (held.image.isNull() && !isInterruptionRequested()) return "No video frame at clip start: " + layer.source.path;
                images[layer.source.clipId] = held.image;
            }
            if (isInterruptionRequested()) return "Export cancelled.";
            const auto image = playback::renderLayers(description_, images, us, {settings_.width, settings_.height}).convertToFormat(QImage::Format_RGB32);
            if (image.isNull()) return "Cannot allocate export image.";
            requireAv(av_frame_make_writable(video.frame), "Prepare video frame");
            if (video.frame->format == AV_PIX_FMT_PAL8) fillVideoFrame(video.codec, video.frame, image);
            else {
            mux.scaler = sws_getCachedContext(mux.scaler, image.width(), image.height(), AV_PIX_FMT_BGRA,
                settings_.width, settings_.height, static_cast<AVPixelFormat>(video.frame->format), SWS_BICUBIC, nullptr, nullptr, nullptr);
            if (!mux.scaler) return "Cannot allocate output color conversion.";
            requireAv(sws_setColorspaceDetails(mux.scaler, sws_getCoefficients(SWS_CS_ITU709), 1,
                sws_getCoefficients(SWS_CS_ITU709), video.codec->color_range == AVCOL_RANGE_JPEG, 0, 1 << 16, 1 << 16), "Set output color conversion");
            const uint8_t* bytes[]{image.constBits()}; int strides[]{static_cast<int>(image.bytesPerLine())};
            sws_scale(mux.scaler, bytes, strides, 0, image.height(), video.frame->data, video.frame->linesize);
            }
            video.frame->pts = frames; video.frame->duration = 1; encode(video, video.frame); ++frames;
        } else {
            requireAv(av_frame_make_writable(audio.frame), "Prepare audio frame");
            const auto capacity = audio.codec->frame_size > 0 ? audio.codec->frame_size : 1024;
            const auto count = static_cast<int>(std::min<qint64>(capacity, totalSamples - samples));
            std::vector<float> interleaved(static_cast<size_t>(count) * 2);
            for (int n = 0; n < count; ++n) {
                while (blockOffset >= block.samples.size() && !isInterruptionRequested()) {
                    if (mixer->takeAudio(block)) { blockOffset = 0; break; }
                    if (mixer->done()) return "Audio mixer ended before the sequence boundary: " + mixer->stats()["error"].toString();
                    msleep(1);
                }
                if (isInterruptionRequested()) return "Export cancelled.";
                for (int channel = 0; channel < 2; ++channel)
                    interleaved[static_cast<size_t>(n) * 2 + channel] = block.samples[blockOffset++];
            }
            const auto error = mixer->stats()["error"].toString(); if (!error.isEmpty()) return "Audio mix: " + error;
            const uint8_t* input[]{reinterpret_cast<const uint8_t*>(interleaved.data())};
            const auto converted = swr_convert(mux.resampler, audio.frame->data, count, input, count);
            checkAv(converted, "Convert mixed export audio");
            if (converted != count) return "Audio conversion did not preserve the sample count.";
            audio.frame->nb_samples = count; audio.frame->pts = samples; encode(audio, audio.frame); samples += count;
        }
        const int percent = static_cast<int>(project::scaleTime(frames, 99, totalFrames, project::Rounding::Floor).value_or(0));
        if (percent != lastPercent) { lastPercent = percent; emit progress(percent); }
    }
    // A late source error must fail the export even if its queued samples were already consumed.
    if (mixer) { while (!mixer->isFinished() && !isInterruptionRequested()) msleep(1);
        audioMetrics_ = mixer->stats();
        const auto error = audioMetrics_["error"].toString(); if (!error.isEmpty()) return "Audio mix: " + error; }
    if (isInterruptionRequested()) return "Export cancelled.";
    encode(video, nullptr); if (settings_.audioEnabled) encode(audio, nullptr);
    requireAv(av_write_trailer(mux.format), "Finalize MP4"); avio_flush(mux.io);
    requireAv(mux.io->error, "Flush MP4 file");
    if (isInterruptionRequested()) return "Export cancelled.";
    if (!output.commit()) return "Cannot publish completed export: " + output.errorString();
    emit progress(100); return {};
}
}

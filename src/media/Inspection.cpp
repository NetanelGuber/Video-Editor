#include "Inspection.h"
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>
#include <limits>
#include <QDir>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/display.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
}

namespace editor::media {
namespace {
QString avError(int code) { char buffer[AV_ERROR_MAX_STRING_SIZE]{}; av_strerror(code, buffer, sizeof(buffer)); return QString::fromUtf8(buffer); }
struct Context {
    std::function<bool()> cancelled;
    QElapsedTimer timer;
    int deadlineMs = 15000;
    Context(std::function<bool()> callback) : cancelled(std::move(callback)) { timer.start(); }
    bool stop() const { return (cancelled && cancelled()) || timer.elapsed() > deadlineMs; }
    static int interrupt(void* opaque) { return static_cast<Context*>(opaque)->stop() ? 1 : 0; }
};
struct Format {
    AVFormatContext* value = avformat_alloc_context();
    ~Format() { avformat_close_input(&value); }
};
struct Decoder {
    AVCodecContext* value = nullptr;
    ~Decoder() { avcodec_free_context(&value); }
    bool open(const AVCodecParameters* parameters) {
        const auto* codec = avcodec_find_decoder(parameters->codec_id);
        if (!codec) return false;
        value = avcodec_alloc_context3(codec);
        if (!value || avcodec_parameters_to_context(value, parameters) < 0) return false;
        value->thread_count = 2;
        value->max_pixels = 4096LL * 4096; // bounded decode allocation; larger media can still be inspected.
        return avcodec_open2(value, codec, nullptr) >= 0;
    }
};
struct Packet { AVPacket* value = av_packet_alloc(); ~Packet() { av_packet_free(&value); } };
struct Frame { AVFrame* value = av_frame_alloc(); ~Frame() { av_frame_free(&value); } };
project::Rational rational(AVRational r, bool zero = false) {
    if (r.num <= 0 || r.den <= 0) return zero ? project::Rational{0, 1} : project::Rational{1, 1};
    const int divisor = std::gcd(r.num, r.den); return {r.num / divisor, r.den / divisor};
}
bool open(Format& format, Context& context, const QString& path, QString& error) {
    if (!format.value) { error = "Cannot allocate media reader"; return false; }
    format.value->interrupt_callback = {Context::interrupt, &context};
    format.value->probesize = 8 * 1024 * 1024;
    format.value->max_analyze_duration = 5 * AV_TIME_BASE;
    format.value->max_streams = 64;
    AVDictionary* options = nullptr;
    // Imports are local files. Do not follow playlist URLs to remote resources.
    av_dict_set(&options, "protocol_whitelist", "file", 0);
    const auto bytes = path.toUtf8();
    int code = avformat_open_input(&format.value, bytes.constData(), nullptr, &options);
    av_dict_free(&options);
    if (code >= 0) code = avformat_find_stream_info(format.value, nullptr);
    if (code < 0) { error = avError(code); return false; }
    return true;
}
float audioSample(const AVFrame* frame, int channel, int sample) {
    const auto format = static_cast<AVSampleFormat>(frame->format);
    const auto packed = av_get_packed_sample_fmt(format);
    const bool planar = av_sample_fmt_is_planar(format) != 0;
    const auto* data = frame->extended_data[planar ? channel : 0];
    const auto index = planar ? sample : sample * frame->ch_layout.nb_channels + channel;
    switch (packed) {
    case AV_SAMPLE_FMT_U8: return (reinterpret_cast<const uint8_t*>(data)[index] - 128) / 128.0f;
    case AV_SAMPLE_FMT_S16: return reinterpret_cast<const int16_t*>(data)[index] / 32768.0f;
    case AV_SAMPLE_FMT_S32: return static_cast<float>(reinterpret_cast<const int32_t*>(data)[index] / 2147483648.0);
    case AV_SAMPLE_FMT_S64: return static_cast<float>(reinterpret_cast<const int64_t*>(data)[index] / 9223372036854775808.0);
    case AV_SAMPLE_FMT_FLT: return reinterpret_cast<const float*>(data)[index];
    case AV_SAMPLE_FMT_DBL: return static_cast<float>(reinterpret_cast<const double*>(data)[index]);
    default: return 0;
    }
}
void aids(const QString& path, Inspection& result, Context& context) {
    // One demux pass: first decodable video frame and first audio stream's opening 30 seconds.
    context.timer.restart(); context.deadlineMs = 20000;
    Format input;
    QString error;
    if (!open(input, context, path, error)) { result.notes += " Preview aids unavailable: " + error; return; }
    int video = -1, audio = -1, rotation = 0;
    for (const auto& s : result.media.streams) {
        if (s.kind == "video" && video < 0) { video = s.index; rotation = s.rotation; }
        if (s.kind == "audio" && audio < 0) audio = s.index;
    }
    Decoder videoDecoder, audioDecoder;
    bool videoDone = video < 0 || !videoDecoder.open(input.value->streams[video]->codecpar);
    bool audioDone = audio < 0 || !audioDecoder.open(input.value->streams[audio]->codecpar);
    if (video >= 0 && videoDone) result.notes += " Thumbnail decoder unavailable or frame exceeds decode limit.";
    if (audio >= 0 && audioDone) result.notes += " Waveform decoder unavailable.";
    constexpr int bins = 256;
    const double plannedSeconds = std::min(30.0, result.durationUs > 0 ? result.durationUs / 1000000.0 : 30.0);
    QVector<float> peaks(bins, 0.0f);
    qint64 samples = 0;
    int sampleRate = audio >= 0 ? input.value->streams[audio]->codecpar->sample_rate : 0;
    Packet packet; Frame frame;
    if (!packet.value || !frame.value) { result.notes += " Cannot allocate preview buffers."; return; }
    auto drain = [&](Decoder& decoder, bool isVideo) {
        while (!context.stop()) {
            const int code = avcodec_receive_frame(decoder.value, frame.value);
            if (code == AVERROR(EAGAIN) || code == AVERROR_EOF) break;
            if (code < 0) { result.notes += " Preview decode error: " + avError(code); if (isVideo) videoDone = true; else audioDone = true; break; }
            if (isVideo) {
                const auto* f = frame.value;
                const double scale = std::min(256.0 / f->width, 144.0 / f->height);
                const int width = std::max(1, static_cast<int>(f->width * scale));
                const int height = std::max(1, static_cast<int>(f->height * scale));
                QImage image(width, height, QImage::Format_RGB888);
                auto* scaler = sws_getContext(f->width, f->height, static_cast<AVPixelFormat>(f->format), width, height,
                    AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (scaler && !image.isNull()) {
                    uint8_t* destination[]{image.bits()}; int strides[]{static_cast<int>(image.bytesPerLine())};
                    sws_scale(scaler, f->data, f->linesize, 0, f->height, destination, strides);
                    // FFmpeg's display matrix rotation is counterclockwise; Qt's is clockwise.
                    result.thumbnail = image.transformed(QTransform().rotate(-rotation));
                }
                sws_freeContext(scaler); videoDone = true;
            } else {
                sampleRate = frame.value->sample_rate;
                if (sampleRate <= 0 || frame.value->ch_layout.nb_channels > 64) { audioDone = true; break; }
                const auto target = static_cast<qint64>(plannedSeconds * sampleRate);
                for (int n = 0; n < frame.value->nb_samples && samples < target; ++n, ++samples) {
                    if ((n & 1023) == 0 && context.stop()) break;
                    float peak = 0;
                    for (int ch = 0; ch < frame.value->ch_layout.nb_channels; ++ch) {
                        const float value = std::abs(audioSample(frame.value, ch, n));
                        if (std::isfinite(value)) peak = std::max(peak, std::min(1.0f, value));
                    }
                    const int bin = static_cast<int>(samples * bins / std::max<qint64>(1, target));
                    peaks[std::min(bins - 1, bin)] = std::max(peaks[std::min(bins - 1, bin)], peak);
                }
                audioDone = samples >= target;
            }
            av_frame_unref(frame.value);
            if (isVideo ? videoDone : audioDone) break;
        }
    };
    int packets = 0;
    while ((!videoDone || !audioDone) && !context.stop() && packets++ < 200000) {
        const int code = av_read_frame(input.value, packet.value);
        if (code < 0) {
            if (code == AVERROR_EOF) {
                if (!videoDone) { avcodec_send_packet(videoDecoder.value, nullptr); drain(videoDecoder, true); }
                if (!audioDone) { avcodec_send_packet(audioDecoder.value, nullptr); drain(audioDecoder, false); }
            } else result.notes += " Preview read error: " + avError(code);
            break;
        }
        const bool isVideo = packet.value->stream_index == video && !videoDone;
        const bool isAudio = packet.value->stream_index == audio && !audioDone;
        if (isVideo || isAudio) {
            auto& decoder = isVideo ? videoDecoder : audioDecoder;
            int sent = avcodec_send_packet(decoder.value, packet.value);
            if (sent == AVERROR(EAGAIN)) { drain(decoder, isVideo); sent = avcodec_send_packet(decoder.value, packet.value); }
            if (sent >= 0) drain(decoder, isVideo);
            else { result.notes += " Preview packet error: " + avError(sent); if (isVideo) videoDone = true; else audioDone = true; }
        }
        av_packet_unref(packet.value);
    }
    if (samples > 0 && sampleRate > 0) {
        result.waveform = peaks; result.waveformSeconds = static_cast<double>(samples) / sampleRate;
        if (!audioDone) result.notes += " Waveform analysis stopped at the resource limit; remaining bins are blank.";
    }
    if (video >= 0 && result.thumbnail.isNull()) result.notes += " No thumbnail decoded.";
}
}
QString pathIdentity(const QString& path) {
    const QFileInfo info(path); const auto canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical).toCaseFolded();
}
Inspection inspect(const QString& path, const std::function<bool()>& cancelled, const CacheOptions& cache) {
    Inspection result;
    const QFileInfo info(path);
    result.media.path = QDir::cleanPath(info.absoluteFilePath());
    result.media.name = info.fileName(); result.media.id = project::newId(); result.media.sizeBytes = info.size();
    QFile source(result.media.path);
    if (!info.isFile() || !source.open(QIODevice::ReadOnly)) {
        result.state = "Offline"; result.error = "File is missing or unreadable: " + result.media.path; return result;
    }
    source.close();
    Context context(cancelled); Format format;
    if (!open(format, context, result.media.path, result.error)) {
        result.state = "Unsupported"; result.cancelled = cancelled && cancelled(); return result;
    }
    result.media.container = QString::fromUtf8(format.value->iformat->name);
    result.durationUs = format.value->duration == AV_NOPTS_VALUE ? 0 : std::max<qint64>(0, format.value->duration);
    bool unsupported = false;
    for (unsigned i = 0; i < format.value->nb_streams; ++i) {
        const auto* stream = format.value->streams[i]; const auto* p = stream->codecpar;
        if (p->codec_type != AVMEDIA_TYPE_VIDEO && p->codec_type != AVMEDIA_TYPE_AUDIO) continue;
        if (p->codec_type == AVMEDIA_TYPE_VIDEO && (stream->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            result.notes += " Attached cover image skipped."; continue;
        }
        project::Stream s;
        s.index = static_cast<int>(i); s.kind = p->codec_type == AVMEDIA_TYPE_VIDEO ? "video" : "audio";
        s.codec = QString::fromUtf8(avcodec_get_name(p->codec_id)); s.timeBase = rational(stream->time_base);
        s.startTicks = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
        s.durationTicks = stream->duration == AV_NOPTS_VALUE ? 0 : std::max<qint64>(0, stream->duration);
        if (!s.durationTicks && result.durationUs > 0) {
            s.durationTicks = av_rescale_q(result.durationUs, AV_TIME_BASE_Q, stream->time_base);
            result.notes += QString(" Stream %1 duration uses container estimate.").arg(i);
        }
        if (s.kind == "video") {
            s.width = p->width; s.height = p->height;
            const auto rate = stream->avg_frame_rate.num > 0 ? stream->avg_frame_rate : stream->r_frame_rate;
            s.frameRate = rational(rate);
            if (rate.num <= 0 || rate.den <= 0) {
                result.state = "Unsupported";
                result.error = QString("Video stream %1 has no usable frame rate; cannot store accurate editing metadata.").arg(i);
                return result;
            }
            const auto* descriptor = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(p->format));
            s.bitDepth = descriptor ? descriptor->comp[0].depth : p->bits_per_raw_sample;
            const auto* matrix = av_packet_side_data_get(p->coded_side_data, p->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
            if (matrix && matrix->size >= 9 * sizeof(int32_t)) {
                const double rotation = av_display_rotation_get(reinterpret_cast<const int32_t*>(matrix->data));
                if (std::isfinite(rotation)) s.rotation = static_cast<int>(std::lround(rotation));
            }
        } else {
            s.frameRate = {0, 1}; s.sampleRate = p->sample_rate; s.channels = p->ch_layout.nb_channels;
            s.bitDepth = p->bits_per_raw_sample;
        }
        if (!avcodec_find_decoder(p->codec_id)) { unsupported = true; result.notes += QString(" No decoder for stream %1 (%2).").arg(i).arg(s.codec); }
        result.media.streams.append(s);
    }
    if (result.media.streams.isEmpty()) { result.state = "Unsupported"; result.error = "No video or audio streams found"; return result; }
    // Sample presentation timestamps, sorted to account for B-frame packet order. One tick of rounding is allowed.
    QHash<int, QVector<qint64>> timestamps;
    Packet packet;
    bool reachedEnd = false;
    if (packet.value) for (int n = 0; n < 4096 && !context.stop(); ++n) {
        const int code = av_read_frame(format.value, packet.value);
        if (code < 0) { reachedEnd = code == AVERROR_EOF; break; }
        const auto* stream = format.value->streams[packet.value->stream_index];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && packet.value->pts != AV_NOPTS_VALUE)
            timestamps[packet.value->stream_index].append(packet.value->pts);
        av_packet_unref(packet.value);
        bool enough = true;
        for (const auto& s : result.media.streams) if (s.kind == "video" && timestamps[s.index].size() < 512) enough = false;
        if (enough) break;
    }
    for (auto& s : result.media.streams) if (s.kind == "video") {
        auto values = timestamps[s.index]; std::sort(values.begin(), values.end());
        // A bounded demux sample can end before delayed B-frame PTS fill its tail.
        // Exclude the final 32 timestamps when sampling stops early, rather than
        // treating those incomplete presentation intervals as a VFR signal.
        if (!reachedEnd && values.size() > 64) values.resize(values.size() - 32);
        qint64 minimum = std::numeric_limits<qint64>::max(), maximum = 0;
        for (qsizetype n = 1; n < values.size(); ++n) {
            const qint64 delta = values[n] - values[n - 1];
            if (delta > 0) { minimum = std::min(minimum, delta); maximum = std::max(maximum, delta); }
        }
        s.variableFrameRate = values.size() >= 8 && maximum - minimum > 1;
        result.notes += QString(" Stream %1 timing: %2 (%3 sampled timestamps; whole-file timing unverified).")
            .arg(s.index).arg(values.size() < 8 ? "unknown" : s.variableFrameRate ? "variable intervals observed" : "constant intervals in sample").arg(values.size());
    }
    auto validation = project::newProject(); validation.media = {result.media};
    result.error = project::validate(validation);
    if (!result.error.isEmpty()) { result.state = "Unsupported"; return result; }
    if (cancelled && cancelled()) { result.cancelled = true; return result; }
    result.state = unsupported ? "Unsupported stream" : "Available";
    const auto key = cache.enabled ? aidCacheKey(result.media.path) : QString{};
    if (!readAids(key, result, cache)) {
        aids(result.media.path, result, context);
        result.cancelled = cancelled && cancelled();
        // Do not publish aids if the source changed during analysis.
        if (key == aidCacheKey(result.media.path)) writeAids(key, result, cache, cancelled);
    }
    result.cancelled = cancelled && cancelled();
    return result;
}
QString relinkError(const project::Media& original, const project::Media& replacement) {
    // A path replacement must preserve the meaning of source ticks and selected streams.
    for (const auto& old : original.streams) {
        const project::Stream* found = nullptr;
        for (const auto& s : replacement.streams) if (s.index == old.index) found = &s;
        if (!found || found->kind != old.kind || found->codec != old.codec || found->timeBase != old.timeBase ||
            found->frameRate != old.frameRate || found->width != old.width || found->height != old.height ||
            found->rotation != old.rotation || found->bitDepth != old.bitDepth || found->sampleRate != old.sampleRate || found->channels != old.channels ||
            found->startTicks != old.startTicks)
            return QString("Replacement stream %1 does not match the original format/timing. Choose the moved original file.").arg(old.index);
    }
    return {};
}
ImportWorker::ImportWorker(QVector<ImportJob> jobs, QSet<QString> knownPaths, QObject* parent)
    : QThread(parent), jobs_(std::move(jobs)), knownPaths_(std::move(knownPaths)) {
    qRegisterMetaType<Inspection>();
}
void ImportWorker::run() {
    int count = 0, duplicates = 0; QString note;
    const QSet<QString> extensions{"mp4", "mkv", "mov", "avi", "webm", "m4v", "mxf", "mts", "m2ts", "ts", "mpg", "mpeg", "wmv",
        "wav", "mp3", "flac", "aac", "m4a", "ogg", "opus", "aiff", "aif", "wma"};
    auto process = [&](const ImportJob& job) {
        if (isInterruptionRequested() || count >= 1000) return;
        const auto key = pathIdentity(job.path);
        if (job.mediaId.isEmpty() && knownPaths_.contains(key)) { ++duplicates; return; }
        knownPaths_.insert(key);
        auto result = inspect(job.path, [this] { return isInterruptionRequested(); });
        if (result.cancelled || isInterruptionRequested()) return;
        result.mediaId = job.mediaId;
        emit inspected(std::move(result)); ++count;
        // At most one result/image is queued; GUI acknowledgement provides backpressure.
        while (!delivered_.tryAcquire(1, 50)) if (isInterruptionRequested()) return;
    };
    QHash<QString, QStringList> matches;
    bool scanned = false, scanLimited = false;
    for (const auto& job : jobs_) {
        if (isInterruptionRequested() || count >= 1000) break;
        if (!job.findFileName.isEmpty()) {
            if (!scanned) {
                QDirIterator files(job.path, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
                int visited = 0;
                while (files.hasNext() && !isInterruptionRequested() && visited < 10000) {
                    const auto path = files.next(); ++visited;
                    matches[QFileInfo(path).fileName().toCaseFolded()].append(path);
                }
                scanLimited = files.hasNext(); scanned = true;
            }
            if (isInterruptionRequested()) break;
            const auto candidates = matches.value(job.findFileName.toCaseFolded());
            if (!scanLimited && candidates.size() == 1) process({candidates[0], job.mediaId, {}});
            else {
                Inspection result; result.mediaId = job.mediaId; result.media.path = job.path;
                result.error = scanLimited ? "Folder search exceeds 10000 files; select a smaller folder."
                    : candidates.isEmpty() ? "Original filename was not found: " + job.findFileName
                    : "Ambiguous filename; use Relink selected media to choose the original: " + job.findFileName;
                emit inspected(std::move(result)); ++count;
                while (!delivered_.tryAcquire(1, 50)) if (isInterruptionRequested()) return;
            }
        } else if (QFileInfo(job.path).isDir()) {
            QDirIterator files(job.path, QDir::Files | QDir::NoSymLinks | QDir::Readable, QDirIterator::Subdirectories);
            while (files.hasNext() && !isInterruptionRequested() && count < 1000) {
                const auto path = files.next();
                if (extensions.contains(QFileInfo(path).suffix().toLower())) process({path, {}});
            }
        } else process(job);
    }
    if (count >= 1000) note = "Batch limit of 1000 files reached; import remaining files in another batch.";
    emit batchComplete(count, duplicates, isInterruptionRequested(), note);
}
}

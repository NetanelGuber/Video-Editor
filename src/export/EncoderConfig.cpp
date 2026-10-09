#include "EncoderConfig.h"
#include "Capabilities.h"
#include <QImage>
#include <stdexcept>
#include <algorithm>
#include <cmath>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}
namespace editor::exporting {
void checkAv(int result, const QString& operation) {
    if (result >= 0) return;
    char text[AV_ERROR_MAX_STRING_SIZE]{}; av_strerror(result, text, sizeof(text));
    throw std::runtime_error((operation + ": " + QString::fromUtf8(text)).toStdString());
}
void configureAudioEncoder(AVCodecContext* c, const project::ExportSettings& settings) {
    c->sample_fmt = AV_SAMPLE_FMT_FLTP;
    const void* formats = nullptr; int count = 0;
    checkAv(avcodec_get_supported_config(c, c->codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &formats, &count), "Query audio formats");
    if (formats && count) {
        const auto* list = static_cast<const AVSampleFormat*>(formats); c->sample_fmt = list[0];
        for (const auto preferred : {AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16P})
            if (std::find(list, list + count, preferred) != list + count) { c->sample_fmt = preferred; break; }
    }
    c->sample_rate = 48000; av_channel_layout_default(&c->ch_layout, 2);
    c->time_base = {1, 48000}; c->bit_rate = settings.audioBitrate; c->thread_count = 2;
    if (c->codec_id == AV_CODEC_ID_AAC) c->profile = AV_PROFILE_AAC_LOW;
    if (c->codec_id == AV_CODEC_ID_FLAC || c->codec_id == AV_CODEC_ID_PCM_S16LE) c->bit_rate = 0;
}
void configureVideoEncoder(AVCodecContext* c, const project::ExportSettings& s, const QString& encoder) {
    if (const auto error = nativeSettingsError(s, encoder); !error.isEmpty()) throw std::runtime_error(error.toStdString());
    c->thread_count = 2; c->flags |= AV_CODEC_FLAG_FRAME_DURATION;
    c->width = s.width; c->height = s.height; c->pix_fmt = av_get_pix_fmt(encoderPixelFormat(s, encoder).toUtf8().constData()); c->sw_pix_fmt = c->pix_fmt;
    c->time_base = {static_cast<int>(s.frameRate.denominator), static_cast<int>(s.frameRate.numerator)};
    c->framerate = av_inv_q(c->time_base); c->sample_aspect_ratio = {1, 1}; c->gop_size = 60; c->max_b_frames = 0;
    const auto* pixel = av_pix_fmt_desc_get(c->pix_fmt); if (!pixel) throw std::runtime_error("Unknown encoder pixel format.");
    c->color_range = (pixel->flags & AV_PIX_FMT_FLAG_RGB) || QString::fromUtf8(pixel->name).startsWith("yuvj") ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    c->colorspace = pixel->flags & AV_PIX_FMT_FLAG_RGB ? AVCOL_SPC_RGB : AVCOL_SPC_BT709;
    c->color_primaries = AVCOL_PRI_BT709; c->color_trc = AVCOL_TRC_BT709;
    auto option = [&](const QString& key, const QString& value) {
        checkAv(av_opt_set(c->priv_data, key.toUtf8().constData(), value.toUtf8().constData(), 0), encoder + " option " + key + "=" + value);
    };
    const auto controls = encoderControls(encoder);
    const auto speedKey = controls["speedKey"].toString();
    if (s.preset != "default" && !speedKey.isEmpty()) {
        if (speedKey.startsWith("context:")) checkAv(av_opt_set(c, speedKey.section(':', 1).toUtf8().constData(), s.preset.toUtf8().constData(), 0), "Native compression speed");
        else option(speedKey, s.preset);
    }
    if (s.qualityMode == "bitrate") c->bit_rate = static_cast<int64_t>(std::llround(s.quality * 1000));
    else if (s.qualityMode == "native") {
        const auto key = controls["qualityKey"].toString();
        if (key == "qscale" || key == "qsv-cqp") { c->flags |= AV_CODEC_FLAG_QSCALE; c->global_quality = static_cast<int>(std::lround(s.quality * FF_QP2LAMBDA)); }
        else if (!key.isEmpty()) {
            option(key, QString::number(s.quality, 'g', 17));
            if (encoder.endsWith("_amf")) { option("rc", "cqp"); option("qp_i", QString::number(s.quality, 'g', 17));
                if (av_opt_find(c->priv_data, "qp_b", nullptr, 0, 0)) option("qp_b", QString::number(s.quality, 'g', 17)); }
            if (encoder.endsWith("_nvenc")) option("rc", "constqp");
            if (key == "crf") c->bit_rate = 0;
        }
    }
    if (encoder == "libx265" && !s.encoderOptions.contains("private:x265-params")) option("x265-params", "pools=2:frame-threads=2:log-level=error");
    if (s.compatibilityProfile != "custom") {
        if (c->codec_id == AV_CODEC_ID_H264 || c->codec_id == AV_CODEC_ID_HEVC) option("profile", c->codec_id == AV_CODEC_ID_HEVC ? "main" : "high");
        if (s.compatibilityProfile == "broad-mp4") { option("level", "4.1"); c->rc_max_rate = 20000000; c->rc_buffer_size = 20000000; }
    }
    applyEncoderOptions(c, s);
    if (pixel->flags & AV_PIX_FMT_FLAG_HWACCEL) {
        const AVCodecHWConfig* chosen = nullptr;
        for (int n = 0; const auto* config = avcodec_get_hw_config(c->codec, n); ++n)
            if (config->pix_fmt == c->pix_fmt && config->device_type != AV_HWDEVICE_TYPE_NONE) { chosen = config; break; }
        if (!chosen) throw std::runtime_error("Encoder requires a hardware surface without a runtime device configuration. Select a host pixel format or another encoder.");
        checkAv(av_hwdevice_ctx_create(&c->hw_device_ctx, chosen->device_type, nullptr, nullptr, 0), "Create encoder hardware device");
        auto* constraints = av_hwdevice_get_hwframe_constraints(c->hw_device_ctx, nullptr);
        if (!constraints || !constraints->valid_sw_formats || constraints->valid_sw_formats[0] == AV_PIX_FMT_NONE) {
            av_hwframe_constraints_free(&constraints); throw std::runtime_error("Hardware device has no upload-compatible host formats.");
        }
        c->sw_pix_fmt = constraints->valid_sw_formats[0];
        for (const auto preferred : {AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P, AV_PIX_FMT_P010LE}) {
            bool found = false; for (const auto* format = constraints->valid_sw_formats; *format != AV_PIX_FMT_NONE; ++format) found |= *format == preferred;
            if (found && sws_isSupportedOutput(preferred)) { c->sw_pix_fmt = preferred; break; }
        }
        av_hwframe_constraints_free(&constraints); c->hw_frames_ctx = av_hwframe_ctx_alloc(c->hw_device_ctx);
        if (!c->hw_frames_ctx) throw std::runtime_error("Cannot allocate hardware upload context.");
        auto* frames = reinterpret_cast<AVHWFramesContext*>(c->hw_frames_ctx->data);
        frames->format = c->pix_fmt; frames->sw_format = c->sw_pix_fmt; frames->width = s.width; frames->height = s.height; frames->initial_pool_size = 4;
        checkAv(av_hwframe_ctx_init(c->hw_frames_ctx), "Initialize encoder hardware frames");
    }
}
void prepareVideoFrame(AVCodecContext* c, AVFrame* frame) {
    frame->format = c->hw_frames_ctx ? c->sw_pix_fmt : c->pix_fmt; frame->width = c->width; frame->height = c->height;
    frame->color_range = c->color_range; frame->colorspace = c->colorspace; frame->color_primaries = c->color_primaries;
    frame->color_trc = c->color_trc; frame->sample_aspect_ratio = c->sample_aspect_ratio; frame->quality = c->global_quality;
    checkAv(av_frame_get_buffer(frame, 32), "Allocate video frame");
}
void fillVideoFrame(AVCodecContext* c, AVFrame* frame, const QImage& input) {
    Q_UNUSED(c); const auto image = input.convertToFormat(QImage::Format_RGB32);
    checkAv(av_frame_make_writable(frame), "Prepare encoder pixels");
    if (frame->format == AV_PIX_FMT_PAL8) {
        const auto scaled = image.size() == QSize(frame->width, frame->height) ? image : image.scaled(frame->width, frame->height);
        for (int n = 0; n < 256; ++n) reinterpret_cast<uint32_t*>(frame->data[1])[n] = qRgb(((n >> 5) & 7) * 255 / 7, ((n >> 2) & 7) * 255 / 7, (n & 3) * 255 / 3);
        for (int y = 0; y < frame->height; ++y) for (int x = 0; x < frame->width; ++x) {
            const auto p = scaled.pixel(x, y); frame->data[0][y * frame->linesize[0] + x] = static_cast<uint8_t>((qRed(p) & 0xe0) | ((qGreen(p) >> 3) & 0x1c) | (qBlue(p) >> 6));
        }
        return;
    }
    auto* scaler = sws_getContext(image.width(), image.height(), AV_PIX_FMT_BGRA, frame->width, frame->height, static_cast<AVPixelFormat>(frame->format), SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!scaler) throw std::runtime_error("Pixel format has no available CPU conversion path. Choose another advertised format.");
    sws_setColorspaceDetails(scaler, sws_getCoefficients(SWS_CS_ITU709), 1, sws_getCoefficients(SWS_CS_ITU709), frame->color_range == AVCOL_RANGE_JPEG, 0, 1 << 16, 1 << 16);
    const uint8_t* bytes[]{image.constBits()}; const int strides[]{static_cast<int>(image.bytesPerLine())};
    const auto result = sws_scale(scaler, bytes, strides, 0, image.height(), frame->data, frame->linesize); sws_freeContext(scaler);
    checkAv(result, "Convert encoder pixels");
}
int sendEncoderFrame(AVCodecContext* c, AVFrame* frame) {
    if (!frame || c->codec_type != AVMEDIA_TYPE_VIDEO || !c->hw_frames_ctx) return avcodec_send_frame(c, frame);
    auto* uploaded = av_frame_alloc(); if (!uploaded) return AVERROR(ENOMEM);
    int code = av_hwframe_get_buffer(c->hw_frames_ctx, uploaded, 0);
    if (code >= 0) code = av_hwframe_transfer_data(uploaded, frame, 0);
    if (code >= 0) code = av_frame_copy_props(uploaded, frame);
    if (code >= 0) code = avcodec_send_frame(c, uploaded);
    av_frame_free(&uploaded); return code;
}
}

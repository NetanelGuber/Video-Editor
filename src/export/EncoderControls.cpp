#include "Capabilities.h"
#include "EncoderConfig.h"
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <climits>
#include <stdexcept>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}
namespace editor::exporting {
namespace {
QJsonArray options(void* object, const QString& scope) {
    QJsonArray result; if (!object) return result;
    const AVOption* option = nullptr;
    while ((option = av_opt_next(object, option))) {
        if (option->type == AV_OPT_TYPE_CONST || (option->flags & AV_OPT_FLAG_READONLY) ||
            !(option->flags & AV_OPT_FLAG_ENCODING_PARAM) || !(option->flags & AV_OPT_FLAG_VIDEO_PARAM)) continue;
        const QString name = QString::fromUtf8(option->name);
        if (scope == "context" && !QStringList{"b", "bt", "g", "bf", "qmin", "qmax", "profile", "level", "strict", "threads", "thread_type", "flags", "flags2", "global_quality", "compression_level", "qcomp", "qblur", "maxrate", "minrate", "bufsize", "rc_init_occupancy", "slices", "coder", "mbd", "trellis"}.contains(name)) continue;
        QJsonArray values;
        if (option->unit) {
            const AVOption* constant = nullptr;
            while ((constant = av_opt_next(object, constant))) if (constant->type == AV_OPT_TYPE_CONST && constant->unit &&
                QString::fromUtf8(constant->unit) == QString::fromUtf8(option->unit))
                values.append(QJsonObject{{"name", constant->name}, {"value", static_cast<double>(constant->default_val.i64)}, {"help", constant->help ? constant->help : ""}});
        }
        uint8_t* value = nullptr; QString defaultValue;
        if (av_opt_get(object, option->name, 0, &value) >= 0 && value) defaultValue = QString::fromUtf8(reinterpret_cast<const char*>(value));
        av_free(value);
        result.append(QJsonObject{{"key", scope + ":" + name}, {"name", name}, {"scope", scope}, {"help", option->help ? option->help : ""},
            {"type", static_cast<int>(option->type)}, {"minimum", option->min}, {"maximum", std::min(option->max, 1.0e15)}, {"default", defaultValue}, {"values", values}});
    }
    return result;
}
QJsonObject find(const QJsonArray& options, const QString& name) { for (const auto& v : options) if (v.toObject()["name"] == name && v.toObject()["scope"] == "private") return v.toObject(); return {}; }
QStringList choices(const QJsonObject& option) {
    QStringList result;
    for (const auto& v : option["values"].toArray()) result.append(v.toObject()["name"].toString());
    if (result.isEmpty() && (option["type"].toInt() == AV_OPT_TYPE_INT || option["type"].toInt() == AV_OPT_TYPE_INT64) &&
        option["maximum"].toDouble() - option["minimum"].toDouble() <= 256)
        for (int n = static_cast<int>(option["minimum"].toDouble()); n <= static_cast<int>(option["maximum"].toDouble()); ++n) result.append(QString::number(n));
    return result;
}
}
QStringList videoEncoderNames() {
    QStringList names; void* iterator = nullptr;
    while (const auto* c = av_codec_iterate(&iterator)) if (av_codec_is_encoder(c) && c->type == AVMEDIA_TYPE_VIDEO) names.append(c->name);
    names.sort(); return names;
}
QJsonObject encoderControls(const QString& encoder) {
    const auto* codec = avcodec_find_encoder_by_name(encoder.toUtf8().constData());
    if (!codec || codec->type != AVMEDIA_TYPE_VIDEO) return {};
    auto* context = avcodec_alloc_context3(codec); if (!context) return {};
    auto specs = options(context->priv_data, "private"); const auto globals = options(context, "context"); for (const auto& v : globals) specs.append(v);
    QJsonArray pixels, supportedPixels; const void* configs = nullptr; int count = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &configs, &count) >= 0 && configs) {
        for (int n = 0; n < count; ++n) {
            const auto format = static_cast<const AVPixelFormat*>(configs)[n];
            const auto* formatName = av_get_pix_fmt_name(format);
            if (!formatName) continue;
            supportedPixels.append(formatName);
            // The editor feeds encoder frames from a CPU raster. Hardware surfaces
            // remain an automatic encoder detail; the manual list contains formats
            // that the configured RGB-to-pixel conversion path can actually create.
            if (format == AV_PIX_FMT_PAL8 || sws_isSupportedOutput(format)) pixels.append(formatName);
        }
    }
    if (!configs) { pixels.append("yuv420p"); supportedPixels.append("yuv420p"); }
    QString speedKey, qualityKey, qualityLabel = "Encoder default";
    double low = 0, high = 0, qualityDefault = 20; int qualityDecimals = 0; QString speedDefault = "default"; QStringList speeds;
    if (encoder == "libx264" || encoder == "libx264rgb" || encoder == "libx265") {
        speedKey = "preset"; speeds = {"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow", "placebo"}; speedDefault = "medium";
        qualityKey = "crf"; qualityLabel = "Constant quality (CRF; lower is better)"; high = 51;
    } else {
        for (const auto& name : encoder.endsWith("_amf") ? QStringList{"quality", "preset"} : encoder.startsWith("libwebp") ? QStringList{} : QStringList{"preset", "cpu-used", "speed", "effort", "compression_level", "deadline"}) {
            const auto spec = find(specs, name); const auto values = choices(spec);
            if (!values.isEmpty()) { speedKey = name; speeds = values; const auto def = spec["default"].toString(); speedDefault = def;
                for (const auto& v : spec["values"].toArray()) if (QString::number(v.toObject()["value"].toInteger()) == def) { speedDefault = v.toObject()["name"].toString(); break; }
                if (!speeds.contains(speedDefault)) speedDefault = speeds.contains("medium") ? "medium" : speeds.contains("balanced") ? "balanced" : speeds.front();
                if (encoder.endsWith("_amf") && speeds.contains("balanced")) speedDefault = "balanced";
                if (encoder.endsWith("_nvenc") && speeds.contains("p4")) speedDefault = "p4";
                break;
            }
        }
        if (encoder.endsWith("_amf")) qualityKey = "qp_p";
        else if (encoder.endsWith("_nvenc")) qualityKey = "qp";
        else if (encoder.endsWith("_qsv")) { qualityKey = "qsv-cqp"; high = codec->id == AV_CODEC_ID_AV1 || codec->id == AV_CODEC_ID_VP9 ? 255 : 51; qualityLabel = "Constant quantizer (QP; lower is better)"; }
        else if (!find(specs, "crf").isEmpty()) qualityKey = "crf";
        else if (!find(specs, "qp").isEmpty()) qualityKey = "qp";
        if (qualityKey != "qsv-cqp" && !qualityKey.isEmpty()) {
            const auto spec = find(specs, qualityKey); low = std::max(0, static_cast<int>(spec["minimum"].toDouble()));
            high = static_cast<int>(std::min(1000000.0, spec["maximum"].toDouble()));
            if (qualityKey == "crf" && (encoder.contains("aom") || encoder.contains("vpx") || encoder.contains("svt"))) high = 63;
            qualityLabel = qualityKey == "crf" ? "Constant quality (CRF; lower is better)" : "Constant quantizer (QP; lower is better)";
            qualityDecimals = spec["type"].toInt() == AV_OPT_TYPE_FLOAT || spec["type"].toInt() == AV_OPT_TYPE_DOUBLE ? 2 : 0;
        }
        if (qualityKey.isEmpty() && QStringList{"mpeg1video","mpeg2video","mpeg4","mjpeg","msmpeg4v2","msmpeg4","wmv1","wmv2","flv1","theora","prores"}.contains(avcodec_get_name(codec->id))) {
            qualityKey = "qscale"; low = 1; high = codec->id == AV_CODEC_ID_THEORA ? 10 : 31; qualityDefault = 3; qualityLabel = "Quantizer scale (lower is better)";
            if (codec->id == AV_CODEC_ID_THEORA) { low = 0; qualityDefault = 7; qualityLabel = "Theora quality (higher is better)"; }
            qualityDecimals = 2;
        }
    }
    if (encoder == "libx264" || encoder == "libx264rgb" || encoder == "libx265") qualityDecimals = 2;
    if (encoder.startsWith("libwebp")) {
        speedKey = "context:compression_level"; speeds = {"0", "1", "2", "3", "4", "5", "6"}; speedDefault = "4";
        qualityKey = "quality"; low = 0; high = 100; qualityDefault = 75; qualityDecimals = 2; qualityLabel = "WebP quality (higher is better)";
    }
    if (encoder.startsWith("libjxl")) {
        const auto spec = find(specs, "distance"); qualityKey = "distance"; low = 0; high = spec["maximum"].toDouble(); qualityDefault = 1; qualityDecimals = 2; qualityLabel = "JPEG XL distance (lower is better; 0 is lossless)";
    }
    const auto* descriptor = avcodec_descriptor_get(codec->id);
    const bool bitrate = descriptor && (descriptor->props & AV_CODEC_PROP_LOSSY) &&
        (!(descriptor->props & AV_CODEC_PROP_INTRA_ONLY) || codec->id == AV_CODEC_ID_JPEGXS || codec->id == AV_CODEC_ID_DIRAC);
    QJsonArray speedValues; for (const auto& speed : speeds) speedValues.append(speed);
    avcodec_free_context(&context);
    return {{"name", encoder}, {"codec", avcodec_get_name(codec->id)}, {"options", specs}, {"pixels", pixels}, {"supportedPixels", supportedPixels}, {"speedKey", speedKey},
        {"speeds", speedValues}, {"speedDefault", speedDefault}, {"qualityKey", qualityKey}, {"qualityLabel", qualityLabel},
        {"qualityMinimum", low}, {"qualityMaximum", high}, {"qualityDecimals", qualityDecimals}, {"bitrateSupported", bitrate}, {"qualityDefault", std::clamp(qualityDefault, low, std::max(low, high))}};
}
QString containerExtension(const QString& name) {
    const auto* mux = av_guess_format(name.toUtf8().constData(), nullptr, nullptr); if (!mux) return {};
    return mux->extensions ? QString::fromUtf8(mux->extensions).section(',', 0, 0) : name.section(',', 0, 0);
}
bool containerAcceptsVideo(const QString& container, const QString& encoder) {
    const auto* mux = av_guess_format(container.toUtf8().constData(), nullptr, nullptr);
    const auto* codec = avcodec_find_encoder_by_name(encoder.toUtf8().constData());
    if (!mux || !codec || codec->type != AVMEDIA_TYPE_VIDEO || mux->flags & AVFMT_NOFILE) return false;
    const auto* desc = avcodec_descriptor_get(codec->id);
    const bool image = desc && ((desc->props & AV_CODEC_PROP_INTRA_ONLY) ||
        (desc->mime_types && QString::fromUtf8(desc->mime_types[0]).startsWith("image/")));
    // image2pipe intentionally writes encoded image packets without codec tags or a sequence of external files.
    return avformat_query_codec(mux, codec->id, FF_COMPLIANCE_NORMAL) > 0 || mux->video_codec == codec->id || (container == "image2pipe" && image);
}
QJsonArray compatibleContainers(const QString& encoder) {
    QJsonArray result; const auto* codec = avcodec_find_encoder_by_name(encoder.toUtf8().constData()); if (!codec) return result;
    void* iterator = nullptr;
    while (const auto* m = av_muxer_iterate(&iterator)) {
        const auto name = QString::fromUtf8(m->name).section(',', 0, 0);
        if (!containerAcceptsVideo(name, encoder)) continue;
        result.append(QJsonObject{{"name", name}, {"extension", containerExtension(name)}, {"description", m->long_name ? m->long_name : m->name}});
    }
    return result;
}
project::ExportSettings adaptToEncoder(project::ExportSettings s, const QString& name) {
    const auto controls = encoderControls(name); if (controls.isEmpty()) return s;
    s.compatibilityProfile = "custom"; s.videoEncoder = name; s.videoCodec = controls["codec"].toString();
    s.preset = controls["speedDefault"].toString(); s.quality = controls["qualityDefault"].toDouble(); s.qualityMode = "native"; s.encoderOptions.clear(); s.pixelFormat = "auto";
    const auto containers = compatibleContainers(name); QString chosen;
    for (const auto& preferred : QStringList{"matroska", "mp4", "mov", "avi", "nut"}) {
        const auto* candidate = av_guess_format(preferred.toUtf8().constData(), nullptr, nullptr);
        for (const auto& v : containers) if (v.toObject()["name"] == preferred && candidate && containerAcceptsVideo(preferred, name)) { chosen = preferred; break; }
        if (!chosen.isEmpty()) break;
    }
    if (chosen.isEmpty()) for (const auto& v : containers) if (v.toObject()["name"] == "nut") { chosen = "nut"; break; }
    if (chosen.isEmpty()) for (const auto& v : containers) if (v.toObject()["name"] == "image2pipe") { chosen = "image2pipe"; break; }
    s.container = !chosen.isEmpty() ? chosen : !containers.isEmpty() ? containers[0].toObject()["name"].toString() : "unavailable";
    // These encoders emit configuration in-band; transport/raw containers need no header extradata.
    if (name.endsWith("_d3d12va") || name.endsWith("_mf")) {
        const auto codec = controls["codec"].toString();
        if (codec == "h264" || codec == "hevc") s.container = "mpegts";
        if (codec == "av1") s.container = "ivf";
    }
    const auto* mux = av_guess_format(s.container.toUtf8().constData(), nullptr, nullptr);
    s.audioEncoder = "aac"; s.audioCodec = "aac";
    bool supported = false;
    for (const auto& audio : QStringList{"aac", "libopus", "flac", "libmp3lame", "ac3", "pcm_s16le"}) {
        const auto* c = avcodec_find_encoder_by_name(audio.toUtf8().constData());
        if (mux && c && (avformat_query_codec(mux, c->id, FF_COMPLIANCE_NORMAL) > 0 || mux->audio_codec == c->id)) { s.audioEncoder = audio; s.audioCodec = avcodec_get_name(c->id); supported = true; break; }
    }
    if (!supported) s.audioEnabled = false;
    return s;
}
void applyEncoderOptions(AVCodecContext* c, const project::ExportSettings& s) {
    for (auto it = s.encoderOptions.begin(); it != s.encoderOptions.end(); ++it) {
        if (it.value().isEmpty()) continue;
        const auto name = it.key().section(':', 1); void* object = it.key().startsWith("private:") ? c->priv_data : static_cast<void*>(c);
        if (!object) throw std::runtime_error(("Option unavailable: " + it.key()).toStdString());
        checkAv(av_opt_set(object, name.toUtf8().constData(), it.value().toUtf8().constData(), 0), "Encoder option " + it.key() + "=" + it.value());
    }
}
QString nativeSettingsError(const project::ExportSettings& s, const QString& name) {
    const auto controls = encoderControls(name); if (controls.isEmpty()) return "Video encoder '" + name + "' is not present in the loaded FFmpeg build.";
    const auto speedKey = controls["speedKey"].toString(); const auto qualityKey = controls["qualityKey"].toString();
    QStringList speeds; for (const auto& v : controls["speeds"].toArray()) speeds.append(v.toString());
    if (s.preset != "default" && !speeds.contains(s.preset)) return "Encoder " + name + " does not support speed '" + s.preset + "'. Choose its native " + (speeds.isEmpty() ? "Encoder default" : speedKey + ": " + speeds.join(", ")) + ".";
    if (s.qualityMode != "native" && s.qualityMode != "default" && s.qualityMode != "bitrate") return "Unknown quality/rate-control mode. Choose Native quality, Target bitrate or Encoder default.";
    if (!std::isfinite(s.quality) || s.quality < 0 || s.quality > 1000000) return "Quality must be a finite value between 0 and 1000000.";
    if (s.qualityMode == "native" && !qualityKey.isEmpty() && (s.quality < controls["qualityMinimum"].toDouble() || s.quality > controls["qualityMaximum"].toDouble()))
        return QString("%1 quality %2 is outside its %3 range %4–%5. Choose a value in this encoder's scale.").arg(name).arg(s.quality).arg(qualityKey).arg(controls["qualityMinimum"].toInt()).arg(controls["qualityMaximum"].toInt());
    if (s.qualityMode == "bitrate" && s.quality < 1) return "Target bitrate must be at least 1 kbit/s.";
    if (s.qualityMode == "bitrate" && !controls["bitrateSupported"].toBool()) return "Encoder " + name + " has no primary target bitrate control (lossless/image/fixed-rate format). Select Native quality or Encoder default and use any codec-specific rate options.";
    if (s.qualityMode == "native" && !qualityKey.isEmpty() && controls["qualityDecimals"].toInt() == 0 && std::floor(s.quality) != s.quality) return "Encoder " + name + " requires a whole-number quantizer. Choose an integer on its native scale.";
    const auto specs = controls["options"].toArray();
    QStringList owned;
    if (s.preset != "default" && !speedKey.isEmpty()) owned.append(speedKey.contains(':') ? speedKey : "private:" + speedKey);
    if (s.preset != "default" && name.endsWith("_amf")) owned.append("private:preset");
    if (s.compatibilityProfile != "custom") owned += {"private:profile", "context:profile"};
    if (s.compatibilityProfile == "broad-mp4") owned += {"private:level", "context:level"};
    if (s.qualityMode == "native" && !qualityKey.isEmpty()) {
        owned.append("private:" + qualityKey);
        if (name.endsWith("_amf")) owned += {"private:rc", "private:qp_i", "private:qp_p", "private:qp_b"};
        if (name.endsWith("_nvenc")) owned.append("private:rc");
        if (qualityKey == "qscale" || qualityKey == "qsv-cqp") owned += {"context:flags", "context:global_quality"};
        if (qualityKey == "crf") owned.append("context:b");
    }
    if (s.qualityMode == "bitrate") owned.append("context:b");
    if (s.qualityMode == "bitrate") owned += {"private:crf", "private:qp", "private:qp_i", "private:qp_p", "private:qp_b", "context:global_quality"};
    for (auto it = s.encoderOptions.begin(); it != s.encoderOptions.end(); ++it) {
        if (it.value().isEmpty()) continue;
        if (s.compatibilityProfile != "custom") return "Native overrides conflict with the fixed playback target. Choose Custom encoder / container or clear the overrides.";
        if (owned.contains(it.key())) return "Option " + it.key() + " conflicts with the Speed or Quality control. Clear the override or select Encoder default for that control.";
        bool found = false; for (const auto& v : specs) found |= v.toObject()["key"] == it.key();
        if (!found) return "Option " + it.key() + " is unavailable for " + name + ". Clear it or select a compatible encoder.";
    }
    const auto* codec = avcodec_find_encoder_by_name(name.toUtf8().constData()); auto* c = avcodec_alloc_context3(codec); if (!c) return "Cannot allocate option validator.";
    QString error; try { applyEncoderOptions(c, s); } catch (const std::exception& e) { error = QString::fromUtf8(e.what()); }
    avcodec_free_context(&c); return error;
}
}

#include "Capabilities.h"
#include "EncoderConfig.h"
#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QProcess>
#include <QImage>
#include <QSet>
#include <algorithm>
#include <numeric>
#ifdef Q_OS_WIN
#include <dxgi1_2.h>
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace editor::exporting {
namespace {
constexpr int CapabilityCacheFormatVersion = 1;
constexpr qint64 MaxCapabilityCacheBytes = 32 * 1024 * 1024;
QString cachePath(const QString& requested) {
    return requested.isEmpty() ? capabilityCacheFilePath() : requested;
}
QJsonArray gpuDriverFingerprint() {
    QJsonArray adapters;
#ifdef Q_OS_WIN
    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) && factory) {
        for (UINT index = 0;; ++index) {
            IDXGIAdapter1* adapter = nullptr;
            if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
            if (!adapter) continue;
            DXGI_ADAPTER_DESC1 description{}; LARGE_INTEGER driverVersion{};
            const HRESULT described = adapter->GetDesc1(&description);
            const HRESULT driver = adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion);
            if (SUCCEEDED(described)) adapters.append(QJsonObject{
                {"description", QString::fromWCharArray(description.Description)},
                {"vendorId", static_cast<int>(description.VendorId)}, {"deviceId", static_cast<int>(description.DeviceId)},
                {"subsystemId", static_cast<int>(description.SubSysId)}, {"revision", static_cast<int>(description.Revision)},
                {"driverVersion", SUCCEEDED(driver) ? QString::number(static_cast<qulonglong>(driverVersion.QuadPart), 16) : QStringLiteral("unavailable")}});
            adapter->Release();
        }
        factory->Release();
    }
#endif
    if (adapters.isEmpty()) adapters.append(QJsonObject{{"adapters", "not available"}});
    return adapters;
}
bool gpuDriverVersionsKnown(const QJsonArray& adapters) {
#ifdef Q_OS_WIN
    if (adapters.isEmpty()) return false;
    for (const auto& value : adapters) {
        const auto adapter = value.toObject();
        if (!adapter.contains("driverVersion") || adapter["driverVersion"].toString() == "unavailable") return false;
    }
    return true;
#else
    Q_UNUSED(adapters);
    return false;
#endif
}
QJsonObject capabilityFingerprint() {
    const auto drivers = gpuDriverFingerprint();
    return {{"ffmpegVersion", av_version_info()}, {"ffmpegConfiguration", avcodec_configuration()},
        {"ffmpegLicense", avcodec_license()}, {"avcodecVersion", static_cast<int>(avcodec_version())},
        {"avformatVersion", static_cast<int>(avformat_version())}, {"avutilVersion", static_cast<int>(avutil_version())},
        {"swscaleVersion", static_cast<int>(swscale_version())}, {"swresampleVersion", static_cast<int>(swresample_version())},
        {"gpuDrivers", drivers}, {"gpuDriverVersionsKnown", gpuDriverVersionsKnown(drivers)}};
}
QJsonObject emptyCapabilityCache() {
    return {{"formatVersion", CapabilityCacheFormatVersion}, {"fingerprint", capabilityFingerprint()},
        {"catalogs", QJsonObject{}}, {"compatibleSettings", QJsonObject{}}};
}
QJsonObject readCapabilityCache(const QString& requestedPath) {
    const auto path = cachePath(requestedPath); QFile file(path);
    if (!file.exists()) return {};
    if (QFileInfo(file).size() > MaxCapabilityCacheBytes) { QFile::remove(path); return {}; }
    if (!file.open(QIODevice::ReadOnly)) return {};
    QJsonParseError error; const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) { file.close(); QFile::remove(path); return {}; }
    const auto root = document.object();
    const auto fingerprint = capabilityFingerprint();
    if (!fingerprint["gpuDriverVersionsKnown"].toBool() || root["formatVersion"].toInt(-1) != CapabilityCacheFormatVersion || root["fingerprint"].toObject() != fingerprint ||
        !root["catalogs"].isObject() || !root["compatibleSettings"].isObject()) { file.close(); QFile::remove(path); return {}; }
    return root;
}
bool writeCapabilityCache(QJsonObject root, const QString& requestedPath) {
    const auto path = cachePath(requestedPath); const auto parent = QFileInfo(path).absolutePath();
    if (path.isEmpty()) return false;
    if (!QDir().mkpath(parent)) return false;
    const auto fingerprint = capabilityFingerprint(); if (!fingerprint["gpuDriverVersionsKnown"].toBool()) return false;
    root["formatVersion"] = CapabilityCacheFormatVersion; root["fingerprint"] = fingerprint;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxCapabilityCacheBytes || file.write(bytes) != bytes.size()) { file.cancelWriting(); return false; }
    return file.commit();
}
QByteArray compatibleProbeKey(const QJsonObject& configuration) {
    return QCryptographicHash::hash(QJsonDocument(configuration).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
}
void mergeCompatibleProbes(QJsonObject& root, const QJsonArray& probes) {
    auto settings = root["compatibleSettings"].toObject();
    for (const auto& item : probes) {
        const auto probe = item.toObject(); const auto configuration = probe["configuration"].toObject();
        if (configuration.isEmpty() || probe["encoder"].toString().isEmpty()) continue;
        settings[QString::fromLatin1(compatibleProbeKey(configuration))] = probe;
    }
    while (settings.size() > 512) settings.erase(settings.begin());
    root["compatibleSettings"] = settings;
}
bool isCompleteCatalog(const QJsonArray& probes) {
    QSet<QString> found;
    for (const auto& item : probes) {
        const auto probe = item.toObject();
        if (!probe.contains("usable") || probe["encoder"].toString().isEmpty()) return false;
        found.insert(probe["encoder"].toString());
    }
    for (const auto& name : videoEncoderNames()) if (!found.contains(name)) return false;
    return true;
}
bool hardwareDependentEncoder(const QString& name, const QJsonObject& build) {
    for (const auto& value : build["encoders"].toArray()) {
        const auto encoder = value.toObject();
        if (encoder["name"].toString() == name)
            return encoder["hardware"].toBool() || !encoder["hardwareConfigs"].toArray().isEmpty();
    }
    return name.endsWith("_amf") || name.endsWith("_nvenc") || name.endsWith("_qsv") || name.endsWith("_d3d12va") || name.endsWith("_mf");
}
}
QVector<CompatibilityProfile> compatibilityProfiles() {
    return {
        {"compatible-mp4", "Desktop MP4 — H.264 / AAC", "mp4", "h264", "mp4", "8-bit SDR. High frame rates and sizes require a capable desktop player."},
        {"desktop-mkv", "Desktop Matroska — H.264 / AAC", "matroska", "h264", "mkv", "Requires a player with Matroska and H.264 support, such as VLC."},
        {"broad-mp4", "Broad MP4 — up to 1080p / 30 fps", "mp4", "h264", "mp4", "H.264 High Level 4.1, 8-bit SDR, AAC-LC. Physical TVs/phones have not been tested.", 1920, 1080, 30, true},
        {"hevc-mp4", "HEVC MP4 — newer devices / desktop", "mp4", "hevc", "mp4", "HEVC Main (hvc1), 8-bit SDR. Requires an HEVC-capable player; up to 4K / 60 fps.", 4096, 2160, 60}
    };
}
std::optional<CompatibilityProfile> compatibilityProfile(const QString& id) {
    for (const auto& p : compatibilityProfiles()) if (p.id == id) return p;
    return {};
}
QStringList encoderCandidates(const project::ExportSettings& s) {
    if (s.videoEncoder != "auto" && s.videoEncoder != "software") return {s.videoEncoder};
    const auto p = compatibilityProfile(s.compatibilityProfile); if (!p) return {};
    const auto software = p->codec == "hevc" ? QString("libx265") : QString("libx264");
    if (s.videoEncoder == "software") return {software};
    if (s.videoEncoder != "auto") return {s.videoEncoder};
    if (p->softwareOnly) return {software};
    return {p->codec + "_amf", p->codec + "_nvenc", p->codec + "_qsv", software};
}
QString encoderPixelFormat(const project::ExportSettings& s, const QString& encoder) {
    if (s.pixelFormat != "auto") return s.pixelFormat;
    const auto controls = encoderControls(encoder);
    QStringList formats; for (const auto& v : controls["supportedPixels"].toArray()) formats.append(v.toString());
    if (formats.isEmpty()) for (const auto& v : controls["pixels"].toArray()) formats.append(v.toString());
    const auto* codec = avcodec_find_encoder_by_name(encoder.toUtf8().constData());
    if (codec && (codec->id == AV_CODEC_ID_MJPEG || codec->id == AV_CODEC_ID_LJPEG))
        for (const auto& p : QStringList{"yuvj420p", "yuvj422p", "yuvj444p"}) if (formats.contains(p)) return p;
    const auto preferred = encoder.endsWith("_amf") || encoder.endsWith("_qsv") || encoder.endsWith("_nvenc") ?
        QStringList{"nv12", "yuv420p", "p010le", "bgra", "rgb24"} : QStringList{"yuv420p", "nv12", "yuv422p10le", "yuv444p10le", "rgb24", "bgra"};
    for (const auto& p : preferred) if (formats.contains(p)) return p;
    for (const auto& p : formats) if (p == "pal8" || sws_isSupportedOutput(av_get_pix_fmt(p.toUtf8().constData()))) return p;
    return formats.isEmpty() ? "yuv420p" : formats.first(); // Hardware-only formats are uploaded in the helper/worker.
}
QString exportCombinationError(const project::ExportSettings& s) {
    if (s.audioEnabled && s.audioEncoder != "flac" && s.audioEncoder != "pcm_s16le") {
        if (s.audioBitrate < 32000 || s.audioBitrate > 512000) return QString("Audio bitrate %1 kbit/s conflicts with the supported 32-512 kbit/s range. Choose an available audio bitrate.").arg(s.audioBitrate / 1000.0);
        const QList<int> mp3{32000, 40000, 48000, 56000, 64000, 80000, 96000, 112000, 128000, 160000, 192000, 224000, 256000, 320000};
        auto ac3 = mp3; ac3.append({384000, 448000, 512000});
        if ((s.audioEncoder == "libmp3lame" && !mp3.contains(s.audioBitrate)) || (s.audioEncoder == "ac3" && !ac3.contains(s.audioBitrate)))
            return QString("Audio bitrate %1 kbit/s conflicts with %2 at 48 kHz stereo. Choose one of its listed bitrates.").arg(s.audioBitrate / 1000.0).arg(s.audioEncoder);
    }
    const auto p = compatibilityProfile(s.compatibilityProfile);
    if (s.compatibilityProfile == "custom") {
        const auto* codec = avcodec_find_encoder_by_name(s.videoEncoder.toUtf8().constData());
        if (!codec || codec->type != AVMEDIA_TYPE_VIDEO) return "Video encoder '" + s.videoEncoder + "' is unavailable. Select a bundled video encoder.";
        if (s.videoCodec != avcodec_get_name(codec->id)) return "Encoder " + s.videoEncoder + " conflicts with saved codec " + s.videoCodec + ". Reselect the encoder.";
        const auto* mux = av_guess_format(s.container.toUtf8().constData(), nullptr, nullptr);
        if (!mux || mux->flags & AVFMT_NOFILE) return "Container " + s.container + " is unavailable for single-file export. Select a compatible container.";
        if (s.width < 1 || s.height < 1 || s.width > 4096 || s.height > 4096) return "Custom export dimensions must be between 1 and 4096 pixels. The selected encoder may impose further limits.";
        if (s.frameRate.numerator <= 0 || s.frameRate.denominator <= 0 || s.frameRate.numerator > 1000000 || s.frameRate.denominator > 1000000 ||
            s.frameRate.numerator > 240 * s.frameRate.denominator || std::gcd(s.frameRate.numerator, s.frameRate.denominator) != 1) return "Custom frame rate must be a positive reduced fraction up to 240 fps.";
        if (s.audioEnabled && (s.sampleRate != 48000 || s.channels != 2)) return "Audio requires 48 kHz stereo. Turn Include audio off and on to reset it.";
        return nativeSettingsError(s, s.videoEncoder);
    }
    if (!p) return "Unknown playback target '" + s.compatibilityProfile + "'. Select Desktop MP4 or another supported target.";
    if (s.container != p->container || s.videoCodec != p->codec || s.audioCodec != "aac")
        return QString("Playback target %1 conflicts with saved %2 / %3 / %4. Reselect the target to use %5 / %6 / AAC.")
            .arg(p->label, s.container, s.videoCodec, s.audioCodec, p->container, p->codec);
    if (s.width < 2 || s.height < 2 || s.width % 2 || s.height % 2 ||
        std::max(s.width, s.height) > p->maxLongEdge || std::min(s.width, s.height) > p->maxShortEdge)
        return QString("Output dimensions %1×%2 conflict with %3. Use even dimensions with long edge ≤%4 and short edge ≤%5, or select Desktop MP4/Matroska.")
            .arg(s.width).arg(s.height).arg(p->label).arg(p->maxLongEdge).arg(p->maxShortEdge);
    if (s.frameRate.numerator <= 0 || s.frameRate.numerator > 1000000 || s.frameRate.denominator <= 0 || s.frameRate.denominator > 1000000 ||
        s.frameRate.numerator > p->maxFps * s.frameRate.denominator || std::gcd(s.frameRate.numerator, s.frameRate.denominator) != 1)
        return QString("Frame rate %1/%2 conflicts with %3. Enter a positive reduced fraction at most %4 fps, or select Desktop MP4/Matroska.")
            .arg(s.frameRate.numerator).arg(s.frameRate.denominator).arg(p->label).arg(p->maxFps);
    if (s.qualityMode == "native" && s.quality == 0) return "Quality 0 conflicts with the fixed playback profile. Select Custom encoder / container for lossless modes.";
    if (s.audioEnabled && (s.sampleRate != 48000 || s.channels != 2)) return "Saved audio rate/channel count conflicts with 48 kHz stereo AAC. Turn Include audio off and on to reset to 48 kHz stereo, or leave audio disabled.";
    if (s.videoEncoder != "auto" && s.videoEncoder != "software") {
        const auto* codec = avcodec_find_encoder_by_name(s.videoEncoder.toUtf8().constData());
        if (!codec || codec->type != AVMEDIA_TYPE_VIDEO) return "Encoder '" + s.videoEncoder + "' is unavailable. Select a bundled video encoder.";
        if (QString::fromUtf8(avcodec_get_name(codec->id)) != p->codec) return "Encoder " + s.videoEncoder + " conflicts with " + p->codec + ". Select Custom encoder / container.";
        if (p->softwareOnly && !s.videoEncoder.startsWith("libx")) return "Hardware encoder " + s.videoEncoder + " conflicts with Broad MP4's Level 4.1 guarantee. Choose Software or Automatic, or select a desktop target.";
    }
    if (s.pixelFormat != "auto" && s.pixelFormat != "yuv420p" && s.pixelFormat != "nv12") return "Pixel format '" + s.pixelFormat + "' conflicts with 8-bit SDR targets. Use automatic, yuv420p or nv12.";
    if (s.audioEncoder != "aac") return "Fixed playback targets require the aac encoder. Reselect the target or choose Custom encoder / container.";
    if (s.videoEncoder != "auto") return nativeSettingsError(s, encoderCandidates(s).value(0));
    return {};
}
namespace {
QString compiledEncoderError(const project::ExportSettings& s, const QString& name) {
    if (auto e = exportCombinationError(s); !e.isEmpty()) return e;
    const auto* c = avcodec_find_encoder_by_name(name.toUtf8().constData());
    if (!c) return name + " is not compiled into the loaded FFmpeg build.";
    const auto* mux = av_guess_format(s.container.toUtf8().constData(), nullptr, nullptr);
    if (!mux) return "Container " + s.container + " is not compiled into the loaded FFmpeg build.";
    if (!containerAcceptsVideo(s.container, name)) return "Container " + s.container + " rejects encoder " + name + ". Select a compatible container.";
    if (auto error = nativeSettingsError(s, name); !error.isEmpty()) return error;
    const auto pixel = av_get_pix_fmt(encoderPixelFormat(s, name).toUtf8().constData());
    const void* values = nullptr; int count = 0;
    checkAv(avcodec_get_supported_config(nullptr, c, AV_CODEC_CONFIG_PIX_FORMAT, 0, &values, &count), "Query pixel formats");
    const auto* formats = static_cast<const AVPixelFormat*>(values);
    if (pixel == AV_PIX_FMT_NONE || (formats && std::find(formats, formats + count, pixel) == formats + count))
        return "Encoder " + name + " conflicts with pixel format " + encoderPixelFormat(s, name) + ". Select automatic pixel format or Software.";
    if (s.audioEnabled) {
        const auto* aac = avcodec_find_encoder_by_name(s.audioEncoder.toUtf8().constData());
        if (!aac || aac->type != AVMEDIA_TYPE_AUDIO ||
            (avformat_query_codec(mux, aac->id, FF_COMPLIANCE_NORMAL) <= 0 && mux->audio_codec != aac->id))
            return "Audio encoder " + s.audioEncoder + " is unavailable for " + s.container + ". Disable audio or choose another audio encoder.";
        if (s.audioCodec != avcodec_get_name(aac->id)) return "Saved audio codec conflicts with the selected encoder. Reselect the audio encoder.";
        checkAv(avcodec_get_supported_config(nullptr, aac, AV_CODEC_CONFIG_SAMPLE_RATE, 0, &values, &count), "Query AAC sample rates");
        const auto* rates = static_cast<const int*>(values);
        if (rates && std::find(rates, rates + count, 48000) == rates + count) return "AAC encoder rejects 48 kHz audio. Disable audio or use a compatible FFmpeg build.";
        checkAv(avcodec_get_supported_config(nullptr, aac, AV_CODEC_CONFIG_CHANNEL_LAYOUT, 0, &values, &count), "Query AAC channel layouts");
        const auto* layouts = static_cast<const AVChannelLayout*>(values); bool stereo = !layouts;
        AVChannelLayout expected{}; av_channel_layout_default(&expected, 2);
        for (int n = 0; layouts && n < count; ++n) stereo |= av_channel_layout_compare(&layouts[n], &expected) == 0;
        av_channel_layout_uninit(&expected);
        if (!stereo) return "AAC encoder rejects stereo audio. Disable audio or use a compatible FFmpeg build.";
    }
    return {};
}
QJsonObject errorResult(const QString& reason) { return {{"usable", false}, {"reason", reason}}; }
}
QString compiledCombinationError(const project::ExportSettings& s) {
    QStringList errors;
    try {
        if (const auto error = exportCombinationError(s); !error.isEmpty()) return error;
        for (const auto& name : encoderCandidates(s)) {
            const auto error = compiledEncoderError(s, name); if (error.isEmpty()) return {};
            errors.append(error);
        }
    } catch (const std::exception& e) { errors.append(QString::fromUtf8(e.what())); }
    return errors.join(" ");
}
QJsonObject compiledCapabilities() {
    QJsonArray encoders, muxers, pixels, devices;
    void* iterator = nullptr;
    while (const auto* c = av_codec_iterate(&iterator)) {
        if (!av_codec_is_encoder(c)) continue;
        QJsonArray formats, hardwareConfigs; const void* values = nullptr; int count = 0;
        if (c->type == AVMEDIA_TYPE_VIDEO && avcodec_get_supported_config(nullptr, c, AV_CODEC_CONFIG_PIX_FORMAT, 0, &values, &count) >= 0 && values)
            for (int n = 0; n < count; ++n) { const auto* name = av_get_pix_fmt_name(static_cast<const AVPixelFormat*>(values)[n]); if (name) formats.append(name); }
        for (int n = 0; const auto* config = avcodec_get_hw_config(c, n); ++n)
            hardwareConfigs.append(QJsonObject{{"device", av_hwdevice_get_type_name(config->device_type)}, {"methods", config->methods}});
        encoders.append(QJsonObject{{"name", c->name}, {"codec", avcodec_get_name(c->id)}, {"type", c->type == AVMEDIA_TYPE_VIDEO ? "video" : c->type == AVMEDIA_TYPE_AUDIO ? "audio" : "other"},
            {"hardware", bool(c->capabilities & (AV_CODEC_CAP_HARDWARE | AV_CODEC_CAP_HYBRID))}, {"pixelFormats", formats}, {"hardwareConfigs", hardwareConfigs}});
    }
    iterator = nullptr;
    while (const auto* m = av_muxer_iterate(&iterator)) muxers.append(QJsonObject{{"name", m->name}, {"extensions", m->extensions ? m->extensions : ""},
        {"h264", avformat_query_codec(m, AV_CODEC_ID_H264, FF_COMPLIANCE_NORMAL) > 0},
        {"hevc", avformat_query_codec(m, AV_CODEC_ID_HEVC, FF_COMPLIANCE_NORMAL) > 0}, {"aac", avformat_query_codec(m, AV_CODEC_ID_AAC, FF_COMPLIANCE_NORMAL) > 0}});
    const AVPixFmtDescriptor* pixel = nullptr;
    while ((pixel = av_pix_fmt_desc_next(pixel))) pixels.append(QJsonObject{{"name", pixel->name}, {"hardwareSurface", bool(pixel->flags & AV_PIX_FMT_FLAG_HWACCEL)}});
    auto type = AV_HWDEVICE_TYPE_NONE;
    while ((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE) devices.append(av_hwdevice_get_type_name(type));
    return {{"version", av_version_info()}, {"configuration", avcodec_configuration()}, {"license", avcodec_license()},
        {"encoders", encoders}, {"muxers", muxers}, {"pixelFormats", pixels}, {"hardwareDeviceTypes", devices}};
}
QJsonObject runCapabilityProbe(const QStringList& arguments, const QByteArray& input, const std::function<bool()>& cancelled, int deadlineMs, const QString& executable) {
    QProcess process;
    process.start(executable.isEmpty() ? QDir(QCoreApplication::applicationDirPath()).filePath("ExportCapabilityProbe.exe") : executable, arguments);
    QElapsedTimer timer; timer.start();
    if (!process.waitForStarted(1000)) return errorResult("Capability helper could not start: " + process.errorString() + ". Rebuild or reinstall the application.");
    process.write(input); process.closeWriteChannel();
    while (!process.waitForFinished(25)) {
        if ((cancelled && cancelled()) || timer.elapsed() >= deadlineMs) {
            process.kill(); process.waitForFinished(1000);
            return errorResult(cancelled && cancelled() ? "Capability check cancelled." : "Capability check timed out. Choose a faster native speed, smaller dimensions or another encoder, or refresh capabilities.");
        }
    }
    const auto result = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || result.isEmpty())
        return errorResult("Capability helper failed/crashed: " + QString::fromUtf8(process.readAllStandardError().right(1000)) + ". Choose Software or refresh capabilities.");
    const auto diagnostic = QString::fromUtf8(process.readAllStandardError().right(1400)).trimmed();
    if (!diagnostic.isEmpty()) { auto detailed = result; detailed["diagnostic"] = diagnostic;
        if (result["usable"].toBool()) detailed["warning"] = "Encoder diagnostics: " + diagnostic;
        return detailed;
    }
    return result;
}
QByteArray probeSettings(const project::ExportSettings& s, const QString& encoder) {
    QJsonObject options; for (auto it = s.encoderOptions.begin(); it != s.encoderOptions.end(); ++it) options[it.key()] = it.value();
    return QJsonDocument(QJsonObject{{"encoder", encoder}, {"requestedEncoder", s.videoEncoder}, {"target", s.compatibilityProfile}, {"container", s.container}, {"codec", s.videoCodec},
        {"pixel", s.pixelFormat}, {"width", s.width}, {"height", s.height}, {"rateNum", s.frameRate.numerator}, {"rateDen", s.frameRate.denominator},
        {"quality", s.quality}, {"qualityMode", s.qualityMode}, {"options", options}, {"audioEncoder", s.audioEncoder}, {"audioCodec", s.audioCodec}, {"audioBitrate", s.audioBitrate},
        {"preset", s.preset}, {"audio", s.audioEnabled}, {"sampleRate", s.sampleRate}, {"channels", s.channels}}).toJson(QJsonDocument::Compact);
}
QString capabilityCacheFilePath() {
    auto location = qEnvironmentVariable("VIDEO_EDITOR_CACHE_DIR");
    if (location.isEmpty()) location = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (location.isEmpty()) {
        const auto appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (appData.isEmpty()) return {};
        location = QDir(appData).filePath("cache");
    }
    return location.isEmpty() ? QString{} : QDir(location).filePath("export-capabilities.json");
}
QByteArray encoderCatalogCacheKey(const project::ExportSettings& s) {
    return QJsonDocument(QJsonObject{{"target", s.compatibilityProfile}, {"width", s.width}, {"height", s.height},
        {"rateNumerator", QString::number(s.frameRate.numerator)}, {"rateDenominator", QString::number(s.frameRate.denominator)},
        {"audioEnabled", s.audioEnabled}, {"sampleRate", s.sampleRate}, {"channels", s.channels}, {"audioBitrate", s.audioBitrate}})
        .toJson(QJsonDocument::Compact);
}
QJsonArray cachedEncoderCatalog(const project::ExportSettings& s, const QString& path) {
    const auto root = readCapabilityCache(path); if (root.isEmpty()) return {};
    const auto entry = root["catalogs"].toObject()[QString::fromLatin1(encoderCatalogCacheKey(s))].toObject();
    const auto probes = entry["probes"].toArray();
    return isCompleteCatalog(probes) ? probes : QJsonArray{};
}
bool cacheEncoderCatalog(const project::ExportSettings& s, const QJsonArray& probes, const QString& path) {
    if (!isCompleteCatalog(probes)) return false;
    auto root = readCapabilityCache(path); if (root.isEmpty()) root = emptyCapabilityCache();
    auto catalogs = root["catalogs"].toObject();
    catalogs[QString::fromLatin1(encoderCatalogCacheKey(s))] = QJsonObject{
        {"cachedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}, {"probes", probes}};
    while (catalogs.size() > 32) catalogs.erase(catalogs.begin());
    root["catalogs"] = catalogs; mergeCompatibleProbes(root, probes);
    return writeCapabilityCache(root, path);
}
QJsonObject cachedCompatibleProbe(const project::ExportSettings& s, const QString& encoder, const QString& path) {
    const auto configuration = QJsonDocument::fromJson(probeSettings(s, encoder)).object();
    const auto root = readCapabilityCache(path); if (root.isEmpty()) return {};
    const auto probe = root["compatibleSettings"].toObject()[QString::fromLatin1(compatibleProbeKey(configuration))].toObject();
    return probe["encoder"].toString() == encoder && probe["configuration"].toObject() == configuration ? probe : QJsonObject{};
}
void cacheCompatibleProbes(const QJsonArray& probes, const QString& path) {
    if (probes.isEmpty()) return;
    auto root = readCapabilityCache(path); if (root.isEmpty()) root = emptyCapabilityCache();
    mergeCompatibleProbes(root, probes); writeCapabilityCache(root, path);
}
QJsonObject probeEncoder(const project::ExportSettings& s, const QString& encoder, const std::function<bool()>& cancelled, int deadlineMs) {
    QJsonObject result;
    try {
        const auto error = compiledEncoderError(s, encoder);
        result = error.isEmpty() ? runCapabilityProbe({"--encoder"}, probeSettings(s, encoder), cancelled, deadlineMs) : errorResult(error);
    } catch (const std::exception& e) { result = errorResult(QString::fromUtf8(e.what())); }
    result["encoder"] = encoder; result["pixelFormat"] = encoderPixelFormat(s, encoder);
    result["configuration"] = QJsonDocument::fromJson(probeSettings(s, encoder)).object(); return result;
}
QJsonObject discoverCapabilities(const project::ExportSettings& s, bool devices, const std::function<bool()>& cancelled, bool allCustomEncoders, bool forceRefresh, const QString& cachePath) {
    const auto build = compiledCapabilities(); QJsonArray probes, deviceProbes;
    if (s.compatibilityProfile == "custom" && allCustomEncoders) {
        if (!forceRefresh) {
            const auto cached = cachedEncoderCatalog(s, cachePath);
            if (!cached.isEmpty()) return {{"build", build}, {"encoderProbes", cached}, {"deviceProbes", QJsonArray{}},
                {"encoder", QString{}}, {"pixelFormat", QString{}}, {"warning", QString{}}, {"error", QString{}}, {"catalogCacheHit", true}};
        }
        // Keep the selected tuple exact; test other catalog entries with their own
        // best container, automatic pixel format, and native defaults at this size/rate.
        probes.append(probeEncoder(s, s.videoEncoder, cancelled));
        for (const auto& name : videoEncoderNames()) {
            if (name == s.videoEncoder) continue;
            if (cancelled && cancelled()) break;
            auto candidate = adaptToEncoder(s, name);
            candidate.audioEnabled = s.audioEnabled;
            probes.append(probeEncoder(candidate, name, cancelled, 2500));
        }
        if (!(cancelled && cancelled())) cacheEncoderCatalog(s, probes, cachePath);
    } else {
        // Test all adapter-backed alternatives so unavailable UI rows have specific reasons.
        auto all = s; if (s.compatibilityProfile != "custom") all.videoEncoder = "auto";
        for (const auto& name : encoderCandidates(all)) {
            if (cancelled && cancelled()) break;
            auto exact = s; exact.videoEncoder = name;
            const bool hardwareDependent = hardwareDependentEncoder(name, build);
            auto cached = !forceRefresh && !hardwareDependent ? cachedCompatibleProbe(exact, name, cachePath) : QJsonObject{};
            probes.append(cached.isEmpty() ? probeEncoder(exact, name, cancelled) : cached);
        }
    }
    if (devices) for (const auto& type : build["hardwareDeviceTypes"].toArray()) {
        if (cancelled && cancelled()) break;
        auto probe = runCapabilityProbe({"--device", type.toString()}, {}, cancelled, 4000); probe["type"] = type; deviceProbes.append(probe);
    }
    if (!(cancelled && cancelled())) cacheCompatibleProbes(probes, cachePath);
    const auto resolution = resolveEncoder(s, probes);
    return {{"build", build}, {"encoderProbes", probes}, {"deviceProbes", deviceProbes}, {"encoder", resolution.encoder},
        {"pixelFormat", resolution.pixelFormat}, {"warning", resolution.warning}, {"error", resolution.error}};
}
EncoderResolution resolveEncoder(const project::ExportSettings& s, const QJsonArray& probes) {
    EncoderResolution result;
    result.error = exportCombinationError(s); if (!result.error.isEmpty()) return result;
    QStringList reasons;
    for (const auto& name : encoderCandidates(s)) {
        bool found = false;
        for (const auto& value : probes) {
            const auto p = value.toObject(); if (p["encoder"].toString() != name) continue; found = true;
            if (p["usable"].toBool()) {
                result.encoder = name; result.pixelFormat = encoderPixelFormat(s, name);
                if (!reasons.isEmpty()) result.warning = "Using " + name + " after unavailable hardware: " + reasons.join("; ");
                if (!p["warning"].toString().isEmpty()) result.warning += (result.warning.isEmpty() ? QString{} : " ") + p["warning"].toString();
                return result;
            }
            reasons.append(name + ": " + p["reason"].toString() + (p["diagnostic"].toString().isEmpty() ? QString{} : " " + p["diagnostic"].toString())); break;
        }
        if (!found) reasons.append(name + ": not validated on this computer");
    }
    result.error = "Encoder choice '" + s.videoEncoder + "' is unavailable: " + reasons.join("; ") +
        (s.compatibilityProfile == "custom" ? ". Adjust the native options, dimensions, pixel format or container, choose another encoder, or refresh capabilities. Software is available with a fixed playback target." : ". Choose Software or Automatic, or refresh capabilities.");
    return result;
}
QJsonObject localDeviceProbe(const QString& name) {
    AVBufferRef* device = nullptr; const auto type = av_hwdevice_find_type_by_name(name.toUtf8().constData());
    if (type == AV_HWDEVICE_TYPE_NONE) return errorResult("Device type is not compiled into this build.");
    const auto code = av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0); av_buffer_unref(&device);
    try { checkAv(code, "Create " + name + " device"); return {{"usable", true}, {"reason", "Device created; encoder usability is tested separately."}}; }
    catch (const std::exception& e) { return errorResult(QString::fromUtf8(e.what())); }
}
QJsonObject localEncoderProbe(const QJsonObject& request) {
    av_log_set_level(AV_LOG_WARNING); // Retain consequential backend warnings without codec statistics.
    project::ExportSettings s; s.compatibilityProfile = request["target"].toString(); s.videoEncoder = request["encoder"].toString();
    s.container = request["container"].toString(); s.videoCodec = request["codec"].toString(); s.pixelFormat = request["pixel"].toString();
    s.width = request["width"].toInt(); s.height = request["height"].toInt(); s.frameRate = {request["rateNum"].toInteger(), request["rateDen"].toInteger()};
    s.quality = request["quality"].toDouble(); s.preset = request["preset"].toString(); s.audioEnabled = request["audio"].toBool();
    s.qualityMode = request["qualityMode"].toString("native"); s.audioEncoder = request["audioEncoder"].toString("aac"); s.audioCodec = request["audioCodec"].toString("aac");
    const auto options = request["options"].toObject(); for (auto it = options.begin(); it != options.end(); ++it) s.encoderOptions[it.key()] = it.value().toString();
    s.audioBitrate = request["audioBitrate"].toInt(192000);
    s.sampleRate = request["sampleRate"].toInt(); s.channels = request["channels"].toInt();
    AVFormatContext* mux = nullptr; AVCodecContext *video = nullptr, *audio = nullptr;
    AVFrame* frame = av_frame_alloc(); AVPacket* packet = av_packet_alloc();
    QJsonObject result; int packets = 0;
    try {
        if (auto error = compiledEncoderError(s, s.videoEncoder); !error.isEmpty()) throw std::runtime_error(error.toStdString());
        checkAv(avformat_alloc_output_context2(&mux, nullptr, s.container.toUtf8().constData(), nullptr), "Create muxer");
        auto open = [&](const char* name, bool isAudio) {
            const auto* codec = avcodec_find_encoder_by_name(name); auto* c = avcodec_alloc_context3(codec);
            if (isAudio) audio = c; else video = c;
            if (!c) throw std::runtime_error("Cannot allocate encoder context.");
            if (mux->oformat->flags & AVFMT_GLOBALHEADER) c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            if (isAudio) configureAudioEncoder(c, s); else configureVideoEncoder(c, s, s.videoEncoder);
            checkAv(avcodec_open2(c, codec, nullptr), "Open " + QString::fromUtf8(name));
            auto* stream = avformat_new_stream(mux, nullptr); if (!stream) throw std::runtime_error("Cannot allocate stream.");
            stream->time_base = c->time_base; if (!isAudio) stream->avg_frame_rate = c->framerate;
            checkAv(avcodec_parameters_from_context(stream->codecpar, c), "Copy stream parameters");
            if (!isAudio && s.videoCodec == "hevc" && s.container == "mp4") stream->codecpar->codec_tag = MKTAG('h','v','c','1');
            return stream;
        };
        auto* v = open(s.videoEncoder.toUtf8().constData(), false); AVStream* a = s.audioEnabled ? open(s.audioEncoder.toUtf8().constData(), true) : nullptr;
        checkAv(avio_open_dyn_buf(&mux->pb), "Open memory output");
        checkAv(avformat_write_header(mux, nullptr), "Write container header");
        if (!frame || !packet) throw std::runtime_error("Cannot allocate probe frame.");
        auto encode = [&](AVCodecContext* c, AVStream* stream, AVFrame* f) {
            checkAv(sendEncoderFrame(c, f), "Send probe frame");
            while (true) {
                int code = avcodec_receive_packet(c, packet); if (code == AVERROR(EAGAIN) || code == AVERROR_EOF) break;
                checkAv(code, "Receive probe packet"); ++packets;
                if (c->codec_type == AVMEDIA_TYPE_VIDEO && packet->duration <= 0) packet->duration = 1;
                av_packet_rescale_ts(packet, c->time_base, stream->time_base); packet->stream_index = stream->index;
                checkAv(av_interleaved_write_frame(mux, packet), "Write probe packet"); av_packet_unref(packet);
            }
        };
        prepareVideoFrame(video, frame); QImage black(s.width, s.height, QImage::Format_RGB32); black.fill(Qt::black);
        for (int n = 0; n < 3; ++n) {
            fillVideoFrame(video, frame, black);
            frame->pts = n; frame->duration = 1; encode(video, v, frame);
        }
        encode(video, v, nullptr); const int videoPackets = packets;
        if (audio) {
            av_frame_unref(frame); frame->format = audio->sample_fmt; frame->sample_rate = 48000; frame->nb_samples = audio->frame_size > 0 ? audio->frame_size : 1024;
            checkAv(av_channel_layout_copy(&frame->ch_layout, &audio->ch_layout), "Copy probe audio layout");
            checkAv(av_frame_get_buffer(frame, 0), "Allocate probe audio");
            checkAv(av_samples_set_silence(frame->data, 0, frame->nb_samples, 2, audio->sample_fmt), "Fill probe audio");
            frame->pts = 0; encode(audio, a, frame); encode(audio, a, nullptr);
        }
        checkAv(av_write_trailer(mux), "Finalize probe container");
        if (videoPackets < 1 || (audio && packets == videoPackets)) throw std::runtime_error("Encoder opened but produced no packets.");
        result = {{"usable", true}, {"reason", "Exact settings opened, encoded and muxed successfully."}, {"packets", packets}};
    } catch (const std::exception& e) { result = errorResult(QString::fromUtf8(e.what())); }
    if (mux && mux->pb) { uint8_t* bytes = nullptr; avio_close_dyn_buf(mux->pb, &bytes); av_free(bytes); mux->pb = nullptr; }
    av_packet_free(&packet); av_frame_free(&frame); avcodec_free_context(&audio); avcodec_free_context(&video); avformat_free_context(mux);
    return result;
}
}

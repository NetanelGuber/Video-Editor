#include "MainWindow.h"
#include "Diagnostics.h"
#include "media/Inspection.h"
#include "media/WaveformWidget.h"
#include "project/ProjectStore.h"
#include <QApplication>
#include <QAction>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QTreeWidget>
#include <cstdio>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace editor;
namespace {
QByteArray read(const QString& path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(qPrintable("Cannot read " + path));
    return file.readAll();
}
void write(const QString& path, const QByteArray& bytes) {
    QFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) throw std::runtime_error(qPrintable("Cannot write " + path));
}
bool waitIdle(MainWindow& window, int& maxGap, int timeout = 300000) {
    if (!window.mediaBusy()) return true;
    QEventLoop loop; QTimer tick, watchdog; QElapsedTimer elapsed;
    elapsed.start(); tick.setInterval(10); watchdog.setSingleShot(true);
    QObject::connect(&tick, &QTimer::timeout, &loop, [&] {
        maxGap = std::max(maxGap, static_cast<int>(elapsed.restart()));
        if (!window.mediaBusy()) loop.quit();
    });
    QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
    tick.start(); watchdog.start(timeout); loop.exec(); return !window.mediaBusy();
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    const auto args = app.arguments(); if (args.size() != 4) return 2;
    const QDir root(args[1]), output(args[2]), prepared(args[3]);
    QDir().mkpath(output.absolutePath());
    QJsonArray failures, checks, reports;
    auto check = [&](bool condition, const QString& text) {
        checks.append(text); if (!condition) { failures.append(text); std::fprintf(stderr, "FAIL: %s\n", qPrintable(text)); }
    };
    int maxGap = 0;
    try {
        const auto lock = QJsonDocument::fromJson(read(root.filePath("dependencies.lock.json"))).object();
        const auto ffprobe = root.filePath(lock["packages"].toArray()[1].toObject()["prefix"].toString() + "/bin/ffprobe.exe");
        QStringList paths;
        const auto inventory = QJsonDocument::fromJson(read(root.filePath("evidence/session-0/test-set/inventory.json"))).object()["files"].toArray();
        for (const auto& value : inventory) paths.append(value.toObject()["path"].toString());
        for (const auto* name : {"synthetic-4k-h264-aac.mp4", "synthetic-4k-hevc10-aac.mp4", "synthetic-vfr-h264.mp4", "synthetic-rotation90-h264.mp4"})
            paths.append(root.filePath("fixtures/generated/pinned/" + QString::fromLatin1(name)));
        QVector<media::Inspection> samples;
        int asset = 0;
        for (const auto& path : paths) {
            auto result = media::inspect(path);
            check(result.error.isEmpty() && !result.cancelled, "Native inspection: " + path + " " + result.error);
            check(!result.thumbnail.isNull(), "Thumbnail generated: " + path);
            QProcess probe; probe.start(ffprobe, {"-v", "error", "-show_streams", "-show_format", "-of", "json", path});
            if (!probe.waitForFinished(30000) || probe.exitCode() != 0) throw std::runtime_error("Independent FFprobe failed");
            const auto independent = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
            const auto format = independent["format"].toObject();
            check(result.media.container == format["format_name"].toString() && result.media.sizeBytes == format["size"].toString().toLongLong(), "Container/size agree with FFprobe: " + path);
            const auto independentUs = static_cast<qint64>(std::llround(format["duration"].toString().toDouble() * 1000000));
            check(std::abs(result.durationUs - independentUs) <= 1, "Container duration agrees with FFprobe within 1 us: " + path);
            int audioCount = 0, videoCount = 0;
            for (const auto& st : independent["streams"].toArray()) {
                const auto o = st.toObject(); const auto kind = o["codec_type"].toString();
                if (kind != "audio" && kind != "video") continue;
                if (kind == "audio") ++audioCount; else ++videoCount;
                const auto found = std::find_if(result.media.streams.cbegin(), result.media.streams.cend(), [&](const project::Stream& s) { return s.index == o["index"].toInt(); });
                check(found != result.media.streams.cend(), "Stream index retained: " + path);
                if (found == result.media.streams.cend()) continue;
                const auto& s = *found;
                check(s.codec == o["codec_name"].toString() && s.kind == kind &&
                    QString("%1/%2").arg(s.timeBase.numerator).arg(s.timeBase.denominator) == o["time_base"].toString(), "Codec/kind/time base agree: " + path);
                if (o.contains("duration_ts")) check(s.durationTicks == o["duration_ts"].toInteger(), "Exact stream duration ticks agree: " + path);
                if (o.contains("start_pts")) check(s.startTicks == o["start_pts"].toInteger(), "Exact stream start ticks agree: " + path);
                if (kind == "video") {
                    check(s.width == o["width"].toInt() && s.height == o["height"].toInt() &&
                        QString("%1/%2").arg(s.frameRate.numerator).arg(s.frameRate.denominator) == o["avg_frame_rate"].toString(), "Video dimensions/average fps agree: " + path);
                    check(s.bitDepth == (o["pix_fmt"].toString().contains("10") ? 10 : 8), "Video bit depth agrees: " + path);
                    int rotation = 0;
                    for (const auto& side : o["side_data_list"].toArray()) if (side.toObject().contains("rotation")) rotation = side.toObject()["rotation"].toInt();
                    check(s.rotation == rotation, "Display orientation agrees: " + path);
                    check(s.variableFrameRate == (path.contains("synthetic-vfr") || path.contains("rotation90")), "Sample timing classification matches Session 0: " + path);
                } else check(s.sampleRate == o["sample_rate"].toString().toInt() && s.channels == o["channels"].toInt(), "Audio sample rate/channels agree: " + path);
            }
            check(result.media.streams.size() == audioCount + videoCount, "All audio/video streams preserved: " + path);
            check(audioCount == 0 ? result.waveform.isEmpty() : result.waveform.size() == 256 &&
                std::all_of(result.waveform.cbegin(), result.waveform.cend(), [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; }) &&
                result.waveformSeconds > 0 && result.waveformSeconds <= 30.001, "Waveform presence/window/peaks (silence permitted): " + path);
            if (audioCount > 0) {
                const auto audioStream = std::find_if(result.media.streams.cbegin(), result.media.streams.cend(), [](const project::Stream& s) { return s.kind == "audio"; });
                const auto ffmpeg = QFileInfo(ffprobe).dir().filePath("ffmpeg.exe");
                QProcess reference;
                reference.start(ffmpeg, {"-v", "error", "-i", path, "-map", "0:" + QString::number(audioStream->index), "-t", "30", "-c:a", "pcm_f32le", "-f", "f32le", "pipe:1"});
                if (!reference.waitForFinished(30000) || reference.exitCode() != 0) throw std::runtime_error("Independent waveform decode failed");
                const auto pcm = reference.readAllStandardOutput();
                float referencePeak = 0;
                for (qsizetype n = 0; n + 4 <= pcm.size(); n += 4) {
                    float value = 0; std::memcpy(&value, pcm.constData() + n, 4);
                    if (std::isfinite(value)) referencePeak = std::max(referencePeak, std::min(1.0f, std::abs(value)));
                }
                check(!result.waveform.isEmpty() && std::abs(referencePeak - *std::max_element(result.waveform.cbegin(), result.waveform.cend())) < 0.00001f,
                    "Waveform peak agrees with independent FFmpeg PCM decode: " + path);
            }
            if (path.contains("rotation90")) check(result.thumbnail.height() > result.thumbnail.width(), "Rotation applied to thumbnail pixels");
            const auto base = "sample-" + QString::number(++asset);
            result.thumbnail.save(output.filePath(base + ".png"));
            auto p = project::newProject(); p.media = {result.media};
            auto record = QJsonDocument::fromJson(project::serialize(p)).object()["media"].toArray()[0].toObject();
            record["durationUs"] = QString::number(result.durationUs); record["notes"] = result.notes;
            record["waveformSeconds"] = result.waveformSeconds;
            QJsonArray peaks; for (const auto value : result.waveform) peaks.append(value); record["peaks"] = peaks;
            reports.append(record);
            write(output.filePath(base + ".ffprobe.json"), QJsonDocument(independent).toJson());
            samples.append(std::move(result));
        }
        const auto audio = media::inspect(prepared.filePath("import/audio-only.wav"));
        check(audio.error.isEmpty() && audio.thumbnail.isNull() && audio.waveform.size() == 256 && audio.media.streams.size() == 1 && audio.media.streams[0].channels == 2, "Audio-only stereo waveform succeeds");
        const auto shortVideo = media::inspect(prepared.filePath("import/subfolder/short-video.mp4"));
        check(shortVideo.error.isEmpty(), "Prepared short video inspected");
        check(media::inspect(prepared.filePath("missing.mp4")).state == "Offline", "Missing file identified as Offline");
        check(!media::inspect(prepared.filePath("import/corrupt.mp4")).error.isEmpty(), "Malformed media fails usefully");
        check(media::inspect(paths[0], [] { return true; }).cancelled, "Probe responds to immediate cancellation");

        QString logPath, error; editor::logging::start(output.filePath("logs"), logPath, error);
        Diagnostics diagnostics(logPath); QSettings settings(output.filePath("settings.ini"), QSettings::IniFormat);
        {
            MainWindow window(settings, diagnostics); window.show();
            auto* tree = window.findChild<QTreeWidget*>("mediaBin");
            check(window.findChild<QAction*>("importFilesAction") && window.findChild<QAction*>("relinkMediaAction"), "Import/relink actions present");
            auto batch = paths; batch.append(paths[0].toUpper()); batch.append(paths[0]);
            batch.append(prepared.filePath("import")); batch.append(prepared.filePath("missing.mp4"));
            QElapsedTimer submit; submit.start(); check(window.importPaths(batch), "Background file/folder batch started");
            check(submit.elapsed() < 100 && window.mediaBusy(), "Submitting import does not perform probing on UI thread");
            check(!window.importPaths(paths), "Concurrent batch rejected while work is active");
            check(waitIdle(window, maxGap), "Background batch finishes");
            check(window.project().media.size() == paths.size() + 2 && tree->topLevelItemCount() == paths.size() + 4, "Recursive folder import, duplicate detection and failed rows coexist");
            check(window.isProjectDirty(), "Successful import marks project dirty");
            check(window.saveProjectPath(output.filePath("imported.veproject")), "Imported project saves");
            const auto imported = window.project();
            check(window.openProjectPath(output.filePath("imported.veproject")) && window.project() == imported, "Imported stream metadata/paths/IDs round-trip exactly");
            check(waitIdle(window, maxGap) && !window.isProjectDirty(), "Reopened media aids regenerate without dirtying project");
            tree->setCurrentItem(tree->topLevelItem(0));
            const auto displayed = window.findChild<QPlainTextEdit*>("mediaDetails")->toPlainText();
            check(displayed.contains("1920 × 1080") && displayed.contains("144/1 fps") && displayed.contains(imported.media[0].path), "Inspector displays persisted dimensions/rate and original path");
            check(!window.findChild<QLabel*>("mediaThumbnail")->pixmap().isNull() && !window.findChild<WaveformWidget*>("audioWaveform")->peaks().isEmpty(), "Selected UI row exposes thumbnail/waveform");

            auto offline = project::newProject("Relink test"); auto m = shortVideo.media;
            m.path = prepared.filePath("missing-original.mp4"); offline.media = {m};
            auto& sequence = offline.sequences[0]; sequence.durationFrames = 30;
            project::Clip clip; clip.id = project::newId(); clip.name = "Preserved edit"; clip.mediaId = m.id;
            clip.streamIndex = m.streams[0].index; clip.durationFrames = 30; clip.sourceInTicks = 100;
            clip.sourceDurationTicks = 1000; sequence.tracks[0].clips = {clip};
            const auto offlinePath = prepared.filePath("offline.veproject");
            check(project::ProjectStore::save(offline, offlinePath).isEmpty(), "Disposable offline project prepared automatically");
            check(window.openProjectPath(offlinePath) && tree->topLevelItem(0)->text(1) == "Offline", "Offline source remains clearly identified after reopen");
            const auto replacement = prepared.filePath(QString::fromUtf8("moved original — טסט.mp4"));
            if (!QFile::exists(replacement)) check(QFile::copy(shortVideo.media.path, replacement), "Disposable Unicode replacement copied");
            check(window.relinkMediaPath(m.id, replacement) && waitIdle(window, maxGap), "Unicode moved source relinks asynchronously");
            check(window.project().media[0].id == m.id && window.project().media[0].path == replacement && window.project().sequences == offline.sequences && window.isProjectDirty(), "Relink keeps stable ID and all clip edits, updates path/dirty state");
            const auto before = window.project();
            check(window.relinkMediaPath(m.id, paths[6]) && waitIdle(window, maxGap) && window.project() == before, "Incompatible replacement preserves original project and edits");
            check(window.relinkMediaPath(m.id, prepared.filePath("missing.mp4")) && waitIdle(window, maxGap) && window.project() == before, "Missing replacement preserves original project and edits");
            auto shorter = shortVideo.media;
            shorter.streams[0].durationTicks = 500;
            auto candidate = offline; candidate.media[0] = shorter; candidate.media[0].id = m.id;
            check(!project::validate(candidate).isEmpty(), "A replacement shorter than existing source bounds cannot validate");
            auto changedTimeBase = shortVideo.media;
            changedTimeBase.streams[0].timeBase = {1, 1000};
            check(!media::relinkError(shortVideo.media, changedTimeBase).isEmpty(), "Relink rejects a changed time base rather than reinterpreting clip ticks");
            check(window.saveProjectPath(output.filePath("relinked.veproject")), "Relinked project saves");
            auto relinked = project::ProjectStore::load(output.filePath("relinked.veproject"));
            check(relinked && *relinked.project == before, "Relink survives project reopen with identical edits");

            check(window.importPaths(paths), "Cancellation batch started");
            window.cancelImport(); check(waitIdle(window, maxGap, 30000) && window.project() == before, "Cancellation drops in-flight results and keeps completed project state");
            check(window.importPaths(paths), "Document-switch batch started");
            window.newDocument(); const auto newId = window.project().id;
            check(waitIdle(window, maxGap, 30000) && window.project().id == newId && window.project().media.isEmpty(), "Replacing document cancels stale results without polluting new project");
            // Cancel after at least one delivered result, not merely before the worker runs.
            check(window.importPaths(paths), "Partial-completion cancellation batch started");
            QTimer cancelAfterFirst;
            cancelAfterFirst.setInterval(1);
            QObject::connect(&cancelAfterFirst, &QTimer::timeout, &window, [&] {
                if (!window.project().media.isEmpty()) { window.cancelImport(); cancelAfterFirst.stop(); }
            });
            cancelAfterFirst.start();
            check(waitIdle(window, maxGap, 30000) && !window.project().media.isEmpty() && window.project().media.size() < paths.size(), "Cancel during a batch keeps completed imports and stops later additions");
            cancelAfterFirst.stop();
            window.saveProjectPath(output.filePath("partial.veproject"));
            window.newDocument();
            check(window.importPaths({"B:/Videos"}) && waitIdle(window, maxGap), "All-real-footage folder imports asynchronously");
            const auto realInventory = QJsonDocument::fromJson(read(root.filePath("evidence/session-0/all-media/inventory.json"))).object()["files"].toArray();
            check(window.project().media.size() == realInventory.size() && realInventory.size() == 30, "All 30 real files populate the media bin");
            for (const auto& value : realInventory) {
                const auto entry = value.toObject();
                const auto found = std::find_if(window.project().media.cbegin(), window.project().media.cend(), [&](const project::Media& media) {
                    return media::pathIdentity(media.path) == media::pathIdentity(entry["path"].toString());
                });
                check(found != window.project().media.cend(), "All-real import includes: " + entry["path"].toString());
                const auto source = QFileInfo(entry["path"].toString());
                const auto recordedTime = QDateTime::fromString(entry["lastWriteTimeUtc"].toString(), Qt::ISODateWithMs);
                check(source.size() == entry["sizeBytes"].toInteger() && recordedTime.isValid() &&
                    std::abs(source.lastModified().toMSecsSinceEpoch() - recordedTime.toMSecsSinceEpoch()) <= 1,
                    "Original size/mtime match Session 0 within Qt's 1 ms precision: " + entry["path"].toString());
            }
            window.saveProjectPath(output.filePath("all-real.veproject")); window.close();
        }
        editor::logging::stop();
        check(maxGap < 500, QString("UI heartbeat stays responsive (largest 10 ms timer gap: %1 ms)").arg(maxGap));
    } catch (const std::exception& exception) { check(false, QString::fromUtf8(exception.what())); }
    write(output.filePath("media-result.json"), QJsonDocument(QJsonObject{{"passed", failures.isEmpty()}, {"checks", checks}, {"failures", failures}, {"samples", reports}, {"maxUiHeartbeatGapMs", maxGap}, {"evidenceLevel", "Native FFmpeg library inspection and independent FFprobe; automated offscreen application import/relink/cancellation; human Windows rendering unverified"}}).toJson());
    return failures.isEmpty() ? 0 : 1;
}

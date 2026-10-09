#include "media/Inspection.h"
#include "playback/MonitorWidget.h"
#include "export/ExportWorker.h"
#include "timeline/Timeline.h"
#include "project/ProjectStore.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTimer>
#include <Windows.h>
#include <psapi.h>
#include <cstdio>
#include <algorithm>
#include <cmath>

using namespace editor;
namespace {
bool waitFor(const std::function<bool()>& predicate, int timeout = 15000) {
    QElapsedTimer clock; clock.start();
    while (clock.elapsed() < timeout) {
        if (predicate()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1);
    }
    return false;
}
QByteArray read(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
bool write(const QString& path, const QByteArray& data) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(data) == data.size(); }
qint64 workingSet() {
    PROCESS_MEMORY_COUNTERS_EX m{}; m.cb = sizeof(m);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m), sizeof(m));
    return static_cast<qint64>(m.WorkingSetSize);
}
quint64 cpuTime() {
    FILETIME a{}, b{}, c{}, d{}; GetProcessTimes(GetCurrentProcess(), &a, &b, &c, &d);
    auto value = [](FILETIME t) { return (static_cast<quint64>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    return value(c) + value(d);
}
project::Clip clip(const project::Media& m, const QString& kind, qint64 start, qint64 frames) {
    project::Clip c; c.id = project::newId(); c.name = m.name; c.kind = kind; c.mediaId = m.id; c.startFrame = start; c.durationFrames = frames;
    for (const auto& s : m.streams) if (s.kind == kind) {
        c.streamIndex = s.index; c.sourceDurationTicks = *project::framesToTicks(frames, {30, 1}, s.timeBase, project::Rounding::Nearest); break;
    }
    return c;
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    if (app.arguments().size() != 3) return 2;
    QDir root(app.arguments()[1]), out(app.arguments()[2]); QDir().mkpath(out.absolutePath());
    QJsonArray failures, benchmarks; int checks = 0;
    auto check = [&](bool passed, const QString& message) { ++checks; if (!passed) { failures.append(message); std::fprintf(stderr, "FAIL: %s\n", qPrintable(message)); } };
    QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/segoeui.ttf"));
    const auto h264 = root.filePath("fixtures/generated/pinned/synthetic-4k-h264-aac.mp4");
    const auto hevc = root.filePath("fixtures/generated/pinned/synthetic-4k-hevc10-aac.mp4");
    media::CacheOptions cache; cache.directory = out.filePath("cache-" + project::newId());
    QElapsedTimer time; time.start(); const auto cold = media::inspect(h264, {}, cache); const auto coldMs = time.elapsed();
    time.restart(); const auto warm = media::inspect(h264, {}, cache); const auto warmMs = time.elapsed();
    check(cold.error.isEmpty() && !cold.cacheHit && warm.cacheHit, "Cold aid generation and warm persistent cache hit");
    check(cold.thumbnail == warm.thumbnail && cold.waveform == warm.waveform && cold.waveformSeconds == warm.waveformSeconds && cold.media.streams == warm.media.streams, "Cached aids exactly match generated image/peaks; metadata re-probes originals");
    const auto key = media::aidCacheKey(h264); const auto entry = QDir(cache.directory).filePath(key + ".aids.json");
    const auto savedEntry = read(entry);
    write(entry, "broken json"); check(!media::inspect(h264, {}, cache).cacheHit && !read(entry).isEmpty(), "Corrupt aid entry regenerates without failing original import");
    auto malformed = QJsonDocument::fromJson(savedEntry).object(); malformed["waveform"] = QJsonArray{-10};
    write(entry, QJsonDocument(malformed).toJson()); check(!media::inspect(h264, {}, cache).cacheHit, "Invalid cached peak shape/value is rejected");
    write(entry, QByteArray(1024 * 1024 + 1, 'x')); check(!media::inspect(h264, {}, cache).cacheHit, "Oversized cache entry does not allocate unbounded read buffer");
    check(QFile::remove(entry) && !media::inspect(h264, {}, cache).cacheHit, "Deleted cache regenerates");
    auto noCache = cache; noCache.enabled = false; check(!media::inspect(h264, {}, noCache).cacheHit, "Disabled cache uses originals");
    auto blocked = cache; blocked.directory = out.filePath("cache-is-file"); write(blocked.directory, "file");
    check(media::inspect(h264, {}, blocked).error.isEmpty(), "Unwritable cache does not fail inspection");
    auto tiny = cache; tiny.directory = out.filePath("tiny"); tiny.budgetBytes = 1;
    check(media::inspect(h264, {}, tiny).error.isEmpty() && QDir(tiny.directory).entryList({"*.aids.json"}, QDir::Files).isEmpty(), "Tiny cache budget remains empty and usable");
    auto cancelledCache = cache; cancelledCache.directory = out.filePath("cancelled-cache");
    media::writeAids(key, cold, cancelledCache, [] { return true; });
    check(!QFileInfo::exists(cancelledCache.directory), "Cancelled aid write publishes nothing");
    check(media::inspect(h264, [] { return true; }, cancelledCache).cancelled, "Inspection cancellation propagates without cache publish");
    const auto copied = out.filePath("source-copy.mp4"); QFile::remove(copied); check(QFile::copy(h264, copied), "Create disposable source for cache invalidation");
    const auto copyKey = media::aidCacheKey(copied);
    check(media::inspect(copied, {}, cache).error.isEmpty(), "Cache disposable source before removal/modification");
    const auto originalTime = QFileInfo(copied).lastModified();
    {
        QFile file(copied);
        if (file.open(QIODevice::ReadWrite) && file.seek(file.size() - 1)) {
            const auto tail = file.read(1);
            if (!tail.isEmpty()) { const char last = tail.at(0) ^ 1; file.seek(file.size() - 1); file.write(&last, 1); file.setFileTime(originalTime, QFileDevice::FileModificationTime); }
        }
    }
    check(copyKey != media::aidCacheKey(copied), "Same-size source modification invalidates cache");
    check(QFile::remove(copied) && media::inspect(copied, {}, cache).state == "Offline", "Missing original stays offline despite prior cache");
    auto retained = cache; retained.directory = out.filePath("retained"); retained.entryLimit = 2; retained.budgetBytes = savedEntry.size() * 2 + 256;
    for (int n = 0; n < 6; ++n) media::writeAids(QString::number(n), cold, retained, {});
    const auto entries = QDir(retained.directory).entryInfoList({"*.aids.json"}, QDir::Files); qint64 diskBytes = 0; for (const auto& f : entries) diskBytes += f.size();
    check(entries.size() <= 2 && diskBytes <= retained.budgetBytes && !entries.isEmpty(), "Disk cache evicts to entry and byte budgets");
    const auto hevcMedia = media::inspect(hevc, {}, cache);
    auto source = playback::compileSource(cold.media).video.first();
    playback::DecodeWorker budgeted(source, 0, false, playback::DecodeMode::Cpu, nullptr, {1280, 720}, 4 * 1024 * 1024); budgeted.start();
    check(waitFor([&] { return budgeted.ready(); }), "Decoder fills constrained video queue");
    QThread::msleep(100); const auto boundedStats = budgeted.stats();
    check(boundedStats["queueBytesHighWater"].toInteger() <= 4 * 1024 * 1024 && boundedStats["queueHighWater"].toInt() <= 1, "Byte backpressure caps queue independently of six-frame limit");
    time.restart(); budgeted.cancel(); check(budgeted.wait(2000) && time.elapsed() < 2000, "Full byte-limited queue cancels promptly");
    playback::DecodeWorker tooSmall(source, 0, false, playback::DecodeMode::Cpu, nullptr, {1280, 720}, 16); tooSmall.start();
    check(tooSmall.wait(5000) && !tooSmall.stats()["error"].toString().isEmpty(), "Insufficient frame budget fails with actionable error"); tooSmall.cancel(); tooSmall.wait();
    playback::DecodeWorker recovered(source, 0, false, playback::DecodeMode::UnavailableForTest, nullptr, {640, 360}, 4 * 1024 * 1024); recovered.start();
    playback::VideoFrame frame; int drops = 0;
    check(waitFor([&] { return recovered.takeVideo(0, frame, drops); }) && !frame.image.isNull() && !recovered.stats()["fallback"].toString().isEmpty(), "Lower quality recovers after memory error and unavailable hardware falls back to CPU"); recovered.cancel(); recovered.wait();
    auto p = project::newProject("Session 10 — 4K editing profile"); p.media = {cold.media, hevcMedia.media};
    auto& seq = p.sequences.first(); seq.width = 3840; seq.height = 2160; seq.frameRate = {30, 1}; seq.durationFrames = 600;
    for (int n = 0; n < 10; ++n) {
        const auto& m = p.media[n % 2]; seq.tracks[0].clips.append(clip(m, "video", n * 60, 60));
        auto audio = clip(m, "audio", n * 60, 60); audio.gain = 0.3; audio.fadeInFrames = audio.fadeOutFrames = 6; seq.tracks[1].clips.append(audio);
    }
    project::Title title; title.id = project::newId(); title.text = "4K performance profile"; title.shadow = true; title.fontSize = 120; p.titles.append(title);
    project::Track titles; titles.id = project::newId(); titles.name = "Titles"; titles.kind = "title";
    project::Clip tc; tc.id = project::newId(); tc.name = "4K title"; tc.kind = "title"; tc.titleId = title.id; tc.durationFrames = 600; titles.clips.append(tc); seq.tracks.append(titles);
    check(project::validate(p).isEmpty(), "Assemble twenty-second 4K H.264/HEVC10/title/faded-audio sequence");
    timeline::TimelineEditor editor(p); const auto before = editor.state();
    check(editor.execute(timeline::SplitClip{seq.id, seq.tracks[0].id, seq.tracks[0].clips[0].id, 30, project::newId(), {}}).isEmpty() && editor.undo() && editor.state() == before, "4K clip edits and undo retain exact timing");
    check(project::deserialize(project::serialize(p)).project == std::optional<project::Project>(p), "4K edits round-trip without preview settings in project");
    QDir().mkpath(root.filePath("fixtures/generated/session-10"));
    const auto projectPath = root.filePath("fixtures/generated/session-10/4k-editing.veproject");
    p.exportSettings.width = 3840; p.exportSettings.height = 2160;
    check(project::ProjectStore::save(p, projectPath).isEmpty(), "Write ready-to-open 4K edited test project");
    const auto graph = playback::compileSequence(p);
    for (auto mode : {playback::DecodeMode::Cpu, playback::DecodeMode::D3D11}) {
        playback::MonitorWidget monitor("Performance sequence"); monitor.setDescription(graph, p.name);
        auto& controller = monitor.controller(); controller.setDecodeMode(mode);
        auto ready = [&] { const auto s = controller.stats(); return !s["buffering"].toBool() && s["presented"].toInt() > 0; };
        time.restart(); check(waitFor(ready), "Production 4K monitor ready"); const auto startupMs = time.elapsed();
        QJsonArray seeks;
        for (auto target : {500000LL, 3500000LL, 15500000LL, 0LL}) {
            const auto count = controller.stats()["presented"].toInt(); time.restart(); controller.seek(target);
            check(waitFor([&] { return ready() && controller.stats()["presented"].toInt() > count; }), "Edited seek produces decoded frame");
            const auto ms = time.elapsed(); seeks.append(ms); check(ms < 1000, "Edited 4K seek stays under one second");
        }
        const auto initialCpu = cpuTime(); const auto initialFrames = controller.stats()["presented"].toInt();
        QElapsedTimer elapsed; elapsed.start(); controller.play(); QJsonArray samples; qint64 nextSample = 0, peak = 0, peakQueue = 0;
        QJsonObject lastVideo; bool hardwareUsed = false; QString videoError;
        check(waitFor([&] {
            const auto video = controller.stats()["video"].toObject();
            if (video["decoded"].toInteger() > 0) {
                lastVideo = video; peakQueue = std::max(peakQueue, video["queueBytesHighWater"].toInteger());
                hardwareUsed |= video["path"].toString().contains("D3D11VA");
            }
            if (!video["error"].toString().isEmpty()) videoError = video["error"].toString();
            if (elapsed.elapsed() >= nextSample) { const auto bytes = workingSet(); peak = std::max(peak, bytes); samples.append(QJsonObject{{"ms", elapsed.elapsed()}, {"workingSetBytes", bytes}}); nextSample += 500; }
            return !controller.isPlaying();
        }, 30000), "Twenty-second edited sequence completes with audio master and video cuts");
        const auto wallMs = elapsed.elapsed(); const auto stats = controller.stats(); const int presented = stats["presented"].toInt() - initialFrames;
        check(controller.positionUs() == graph.durationUs && wallMs < 23000, "Edited playback reaches exact sequence end within pacing tolerance");
        check(presented >= 540 && presented <= 610, "At least 90 percent of 30 fps editing cadence across cuts");
        check(peak < 512LL * 1024 * 1024, "Edited preview process remains below 512 MiB measured working set");
        check(peakQueue > 0 && peakQueue <= 32 * 1024 * 1024 && videoError.isEmpty(), "Production decoders across cuts obey byte budget");
        if (mode == playback::DecodeMode::D3D11) check(hardwareUsed, "Actual target RX 9070 hardware path used");
        benchmarks.append(QJsonObject{{"decode", mode == playback::DecodeMode::Cpu ? "CPU" : "D3D11VA"}, {"startupMs", startupMs}, {"seekMs", seeks},
            {"wallMs", wallMs}, {"presented", presented}, {"presentedFps", presented * 1000.0 / wallMs}, {"peakWorkingSetBytes", peak},
            {"cpuOneCorePercent", (cpuTime() - initialCpu) / (wallMs * 100.0)}, {"memorySamples", samples}, {"controller", stats}, {"lastVideo", lastVideo}, {"peakQueueBytes", peakQueue}});
        const auto projectBytes = project::serialize(p);
        const auto at = controller.positionUs(); monitor.findChild<QComboBox*>("previewQuality")->setCurrentIndex(2);
        monitor.findChild<QComboBox*>("previewMemory")->setCurrentIndex(1);
        check(controller.previewSize() == QSize(640, 360) && controller.positionUs() == at && project::serialize(p) == projectBytes, "Production quality/memory controls preserve playhead and original edit graph");
        controller.seek(3500000); check(waitFor(ready), "Low-memory mode seeks 4K media");
        check(controller.stats()["video"].toObject()["queueBytesHighWater"].toInteger() <= 4 * 1024 * 1024, "Low-memory preview caps video queue at four MiB");
    }
    auto shortProject = p; shortProject.sequences[0].durationFrames = 120;
    for (auto& track : shortProject.sequences[0].tracks) {
        track.clips.removeIf([](const auto& c) { return c.startFrame >= 120; });
        for (auto& c : track.clips) c.durationFrames = std::min(c.durationFrames, 120 - c.startFrame);
    }
    auto settings = p.exportSettings; settings.outputPath = out.filePath("4k-original-export.mp4"); settings.preset = "ultrafast";
    exporting::ExportWorker exporter(playback::compileSequence(shortProject), settings); time.restart(); exporter.start();
    qint64 exportPeak = 0;
    check(waitFor([&] { exportPeak = std::max(exportPeak, workingSet()); return exporter.isFinished(); }, 120000), "Export four-second 4K edited sequence using originals"); exporter.cancel(); exporter.wait(); const auto exportMs = time.elapsed();
    check(exporter.result()["passed"].toBool(), "4K original export succeeds");
    const auto lock = QJsonDocument::fromJson(read(root.filePath("dependencies.lock.json"))).object();
    const auto ffbin = root.filePath(lock["packages"].toArray()[1].toObject()["prefix"].toString() + "/bin/");
    QProcess probe; probe.start(ffbin + "ffprobe.exe", {"-v", "error", "-count_frames", "-show_streams", "-show_format", "-of", "json", settings.outputPath});
    check(probe.waitForFinished(60000) && probe.exitCode() == 0, "Independent 4K output probe/decode");
    const auto metadata = QJsonDocument::fromJson(probe.readAllStandardOutput()).object(); write(out.filePath("4k-export-ffprobe.json"), QJsonDocument(metadata).toJson());
    const auto streams = metadata["streams"].toArray(); const auto v = streams.isEmpty() ? QJsonObject{} : streams.first().toObject();
    check(v["width"] == 3840 && v["height"] == 2160 && v["nb_read_frames"] == "120" && v["avg_frame_rate"] == "30/1", "Original 4K resolution, 30 fps and every edit frame exported despite preview profile");
    check(std::abs(metadata["format"].toObject()["duration"].toString().toDouble() - 4) < 0.002, "Export preserves exact four-second edit duration");
    QProcess decode; decode.start(ffbin + "ffmpeg.exe", {"-v", "error", "-i", settings.outputPath, "-map", "0", "-f", "null", "-"});
    check(decode.waitForFinished(60000) && decode.exitCode() == 0, "Independent CPU decoder reads full exported video/audio");
    QJsonArray pixelChecks;
    auto pixels = [&](const QString& path, int frameIndex, bool reduced) {
        QProcess process;
        const auto filter = QString("select=eq(n\\,%1)").arg(frameIndex) + (reduced ? ",scale=320:180" : "");
        process.start(ffbin + "ffmpeg.exe", {"-v", "error", "-i", path, "-vf", filter, "-frames:v", "1", "-an", "-pix_fmt", "bgra", "-f", "rawvideo", "pipe:1"});
        check(process.waitForFinished(30000) && process.exitCode() == 0, "Independent selected frame decode");
        const auto bytes = process.readAllStandardOutput(); const int w = reduced ? 320 : 3840, h = reduced ? 180 : 2160;
        if (bytes.size() != w * h * 4) return QImage{};
        return QImage(reinterpret_cast<const uchar*>(bytes.constData()), w, h, w * 4, QImage::Format_RGB32).copy();
    };
    for (int f : {0, 59, 60, 119}) {
        const auto raw = pixels(p.media[f / 60].path, f % 60, false);
        const auto actual = pixels(settings.outputPath, f, true);
        const auto expected = playback::renderFrame(playback::compileSequence(shortProject), raw, f * 1000000LL / 30, {3840, 2160}).scaled(320, 180, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        double difference = 0;
        if (!actual.isNull() && !raw.isNull()) {
            for (int y = 0; y < 180; ++y) for (int x = 0; x < 320; ++x) {
                const auto a = actual.pixelColor(x, y), b = expected.pixelColor(x, y);
                difference += std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
            }
            difference /= 320 * 180 * 3;
        } else difference = 1000;
        check(difference < 8, "Original 4K source/title pixels match export at edit boundary frame " + QString::number(f));
        pixelChecks.append(QJsonObject{{"frame", f}, {"meanRgbError", difference}});
        if (f == 0) { actual.save(out.filePath("4k-export-first.png")); expected.save(out.filePath("4k-reference-first.png")); }
    }
    QJsonObject report{{"passed", failures.isEmpty()}, {"checkCount", checks}, {"failures", failures}, {"benchmarks", benchmarks},
        {"cacheColdMs", coldMs}, {"cacheWarmMs", warmMs}, {"exportMs", exportMs}, {"exportPeakWorkingSetBytes", exportPeak}, {"export", exporter.result()}, {"exportPixelChecks", pixelChecks}, {"queueBudgetTest", boundedStats},
        {"proxyDecision", "Original CPU and D3D11VA preview meet the agreed 4K profile; optional proxy generation not warranted by this corpus."},
        {"evidenceLevel", "Native target-PC CPU/D3D11VA, production Qt offscreen monitor, WASAPI timing and independent 4K export decode; no visible-display or audible perception claims"}};
    write(out.filePath("performance-result.json"), QJsonDocument(report).toJson()); return failures.isEmpty() ? 0 : 1;
}

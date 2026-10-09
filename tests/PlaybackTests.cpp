#include "playback/PlaybackController.h"
#include "playback/MonitorWidget.h"
#include "media/Inspection.h"
#include "project/ProjectStore.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QCryptographicHash>
#include <QProcess>
#include <QPushButton>
#include <cstdio>
#include <algorithm>
#include <cmath>

using namespace editor;
namespace {
QByteArray read(const QString& path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
bool waitFor(const std::function<bool()>& predicate, int timeout = 15000) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < timeout) {
        if (predicate()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1);
    }
    return false;
}
QByteArray hashImage(const QImage& image) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (int y = 0; y < image.height(); ++y) hash.addData(QByteArrayView(reinterpret_cast<const char*>(image.constScanLine(y)), image.width() * 4));
    return hash.result().toHex();
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    const auto args = app.arguments(); if (args.size() != 3) return 2;
    QDir root(args[1]), output(args[2]); QDir().mkpath(output.absolutePath());
    QJsonArray failures, reports; int checks = 0;
    auto check = [&](bool passed, const QString& reason) { ++checks; if (!passed) { failures.append(reason); std::fprintf(stderr, "FAIL: %s\n", qPrintable(reason)); } };
    QStringList paths;
    const auto inventory = QJsonDocument::fromJson(read(root.filePath("evidence/session-0/test-set/inventory.json"))).object()["files"].toArray();
    for (const auto& entry : inventory) paths.append(entry.toObject()["path"].toString());
    for (const auto* name : {"synthetic-4k-h264-aac.mp4", "synthetic-4k-hevc10-aac.mp4", "synthetic-vfr-h264.mp4", "synthetic-rotation90-h264.mp4"})
        paths.append(root.filePath("fixtures/generated/pinned/" + QString::fromLatin1(name)));
    paths.append(root.filePath("fixtures/generated/session-5/flash-beep.mp4"));
    project::Media syncMedia;
    for (const auto& path : paths) {
        const auto inspected = media::inspect(path); check(inspected.error.isEmpty(), "Inspect " + path);
        const auto description = playback::compileSource(inspected.media);
        check(!description.video.isEmpty() && description.durationUs > 0, "Source render description " + path);
        if (description.video.isEmpty()) continue;
        if (path.endsWith("flash-beep.mp4")) syncMedia = inspected.media;
        for (const auto mode : {playback::DecodeMode::Cpu, playback::DecodeMode::D3D11}) {
            for (const qint64 target : {0LL, std::min(1500000LL, description.durationUs / 2), std::max(0LL, description.video[0].endUs - 50000)}) {
                const auto source = description.video[0]; playback::DecodeWorker worker(source, target, false, mode);
                QElapsedTimer latency; latency.start(); worker.start();
                playback::VideoFrame frame; int dropped = 0;
                const bool received = waitFor([&] { return worker.takeVideo(target, frame, dropped) || worker.done(); });
                const qint64 latencyMs = latency.elapsed();
                check(received && !frame.image.isNull(), QString("Frame at %1 / %2 / mode %3").arg(target).arg(path).arg(static_cast<int>(mode)));
                if (!frame.image.isNull()) {
                    check(frame.ptsUs <= target && target - frame.ptsUs <= 250000, "Seek frame covers target within VFR tolerance " + path);
                    check(frame.image.sizeInBytes() <= 1280 * 720 * 4, "Preview image bound " + path);
                    if (inspected.media.streams[0].rotation) check(frame.image.height() > frame.image.width(), "Rotation applied " + path);
                }
                worker.cancel(); check(worker.wait(10000), "Cancel decode " + path);
                const auto stats = worker.stats(); check(stats["error"].toString().isEmpty(), "Decode error: " + path + " " + stats["error"].toString());
                check(stats["queueHighWater"].toInt() <= static_cast<int>(playback::DecodeWorker::VideoLimit), "Video queue bound " + path);
                reports.append(QJsonObject{{"path", path}, {"requestedMode", mode == playback::DecodeMode::Cpu ? "CPU" : "D3D11VA"},
                    {"targetUs", target}, {"actualPtsUs", frame.ptsUs}, {"latencyMs", latencyMs}, {"stats", stats}, {"sha256", QString::fromLatin1(hashImage(frame.image))}});
            }
        }
        // All selected audio streams, including multiple audio streams and FLAC/ALAC fixtures.
        for (const auto& stream : inspected.media.streams) if (stream.kind == "audio") {
            playback::Source source{inspected.media.id, path, stream.index, 0, 0, 2000000, 0, 1};
            playback::DecodeWorker audio(source, 500000, true, playback::DecodeMode::Cpu); audio.start();
            playback::AudioBlock block; check(waitFor([&] { return audio.takeAudio(block) || audio.done(); }) && !block.samples.isEmpty(), "Audio resample " + path);
            bool finite = true; for (float sample : block.samples) finite &= std::isfinite(sample);
            check(finite && block.samples.size() <= 8192 && std::abs(block.ptsUs - 500000) <= 1000,
                QString("Audio trim/sample/block bound %1: pts=%2 samples=%3 finite=%4").arg(path).arg(block.ptsUs).arg(block.samples.size()).arg(finite));
            audio.cancel(); check(audio.wait(10000), "Audio cancellation " + path);
            check(audio.stats()["error"].toString().isEmpty(), "Audio decode error " + path);
        }
    }
    const auto description = playback::compileSource(syncMedia);
    if (!description.video.isEmpty()) {
        playback::DecodeWorker bounded(description.video[0], 0, false, playback::DecodeMode::Cpu); bounded.start();
        check(waitFor([&] { return bounded.stats()["queueHighWater"].toInt() == 6; }), "Paused video queue reaches fixed bound");
        const auto before = bounded.stats()["decoded"].toInteger(); QThread::msleep(100);
        check(bounded.stats()["decoded"].toInteger() == before, "Full paused queue applies backpressure");
        bounded.cancel(); check(bounded.wait(10000), "Cancellation wakes full queue");
        playback::DecodeWorker fallback(description.video[0], 1000000, false, playback::DecodeMode::UnavailableForTest); fallback.start();
        playback::VideoFrame frame; int dropped = 0;
        check(waitFor([&] { return fallback.takeVideo(1000000, frame, dropped); }) && !frame.image.isNull(), "Unavailable acceleration returns CPU frame");
        fallback.cancel(); fallback.wait(); check(fallback.stats()["path"] == "CPU fallback" && !fallback.stats()["fallback"].toString().isEmpty(), "Fallback reason recorded");
        playback::DecodeWorker missing({"missing", root.filePath("fixtures/generated/session-5/missing.mp4"), 0, 0, 0, 1000000, 0, 1}, 0, false, playback::DecodeMode::Cpu);
        missing.start(); check(waitFor([&] { return missing.isFinished(); }) && !missing.stats()["error"].toString().isEmpty(), "Missing media fails usefully");
        auto project = project::newProject("Session 5 playback check"); project.media.append(syncMedia);
        auto& sequence = project.sequences[0]; sequence.durationFrames = 210; sequence.frameRate = {30, 1};
        for (const auto& stream : syncMedia.streams) {
            auto& track = sequence.tracks[stream.kind == "video" ? 0 : 1];
            for (const auto start : {0LL, 120LL}) {
                project::Clip clip; clip.id = project::newId(); clip.name = "Flash and beep"; clip.kind = stream.kind; clip.mediaId = syncMedia.id;
                clip.streamIndex = stream.index; clip.startFrame = start; clip.durationFrames = 90;
                clip.sourceInTicks = start == 0 ? 0 : stream.timeBase.denominator / stream.timeBase.numerator * 2;
                clip.sourceDurationTicks = stream.timeBase.denominator / stream.timeBase.numerator * 3; track.clips.append(clip);
            }
        }
        const auto graph = playback::compileSequence(project);
        check(graph.sequence == project.sequences[0] && graph.media == project.media && graph.durationUs == 7000000, "Complete shared graph and integer time retained");
        check(!playback::activeSource(graph.video, 3500000) && playback::activeSource(graph.video, 4000000)->inUs == 2000000, "Gap and source trim mapping");
        const auto fixturePath = root.filePath("fixtures/generated/session-5/playback.veproject");
        check(project::ProjectStore::save(project, fixturePath).isEmpty(), "Prepare human sequence fixture");
        playback::MonitorWidget monitor("Test monitor"); monitor.setDescription(graph, "Test sequence");
        int frames = 0; QImage displayed;
        QObject::connect(&monitor.controller(), &playback::PlaybackController::frameReady, &app, [&](const QImage& image) { if (!image.isNull()) { ++frames; displayed = image; } });
        check(waitFor([&] { return frames > 0; }), "Paused source frame through controller");
        auto* presentation = monitor.findChild<QComboBox*>("presentationMode"); presentation->setCurrentIndex(1);
        check(presentation->currentIndex() == 0, "Offscreen OpenGL falls back to CPU presentation");
        monitor.controller().seek(3500000); check(waitFor([&] { return !monitor.controller().stats()["buffering"].toBool(); }), "Gap seek ready");
        check(monitor.controller().positionUs() == 3500000, "Paused seek clock stable");
        for (int n = 0; n < 60; ++n) monitor.controller().seek(n * 60000);
        monitor.controller().seek(4200000); frames = 0;
        check(waitFor([&] { return frames > 0; }), "Rapid scrubs coalesce to final frame");
        check(monitor.controller().positionUs() == 4200000, "Final scrub position preserved");
        monitor.findChild<QPushButton*>("nextFrameButton")->click();
        check(monitor.controller().positionUs() == 4233333, "Next frame uses exact rational sequence time");
        monitor.findChild<QPushButton*>("previousFrameButton")->click();
        check(monitor.controller().positionUs() == 4200000, "Previous frame returns to original sequence frame");
        frames = 0; check(waitFor([&] { return frames > 0; }), "Frame-step image becomes available");
        // Exercise the real endpoint for two seconds, including pause/resume. Audibility is human-tested.
        monitor.controller().play();
        check(waitFor([&] { return monitor.controller().positionUs() >= 5200000; }, 10000), "Audio-master or explicit wall-clock fallback advances");
        monitor.controller().pause(); const auto paused = monitor.controller().positionUs();
        QElapsedTimer pauseTime; pauseTime.start(); waitFor([&] { return pauseTime.elapsed() > 100; });
        check(monitor.controller().positionUs() == paused, "Pause holds sequence clock");
        reports.append(QJsonObject{{"controller", monitor.controller().stats()}});
        monitor.controller().play(); check(waitFor([&] { return !monitor.controller().isPlaying(); }, 10000), "Playback stops at sequence end");
        check(monitor.controller().positionUs() == graph.durationUs, "Exact sequence end reached");
        check(monitor.controller().stats()["maxUiTickGapMs"].toInt() < 500, "UI timer stays responsive during decode/seek/playback");
        // Reference frame comparison uses an independent FFmpeg process on the unscaled 640x360 fixture.
        const auto lock = QJsonDocument::fromJson(read(root.filePath("dependencies.lock.json"))).object();
        const auto ffmpeg = root.filePath(lock["packages"].toArray()[1].toObject()["prefix"].toString() + "/bin/ffmpeg.exe");
        const auto raw = output.filePath("reference.bgra");
        QProcess reference; reference.start(ffmpeg, {"-v", "error", "-y", "-ss", "1", "-i", syncMedia.path, "-frames:v", "1", "-pix_fmt", "bgra", "-f", "rawvideo", raw});
        check(reference.waitForFinished(15000) && reference.exitCode() == 0, "Independent reference frame");
        const auto bytes = read(raw);
        if (!frame.image.isNull() && bytes.size() == frame.image.sizeInBytes()) {
            const auto* actual = frame.image.constBits(); qint64 difference = 0;
            for (qsizetype n = 0; n < bytes.size(); ++n) difference += std::abs(static_cast<int>(actual[n]) - static_cast<unsigned char>(bytes[n]));
            check(difference / static_cast<double>(bytes.size()) < 2, "Native frame agrees with independent FFmpeg within mean 2/255 conversion tolerance");
        } else check(false, "Reference image dimensions");
    }
    QJsonObject result{{"passed", failures.isEmpty()}, {"failures", failures}, {"checkCount", checks}, {"samples", reports},
        {"evidenceLevel", "Native FFmpeg CPU/D3D11VA decode/seek/resample, bounded queue/cancellation, offscreen controller and real WASAPI endpoint; no visual/audibility acceptance"}};
    QFile file(output.filePath("playback-result.json")); if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(result).toJson()) < 0) return 2;
    return failures.isEmpty() ? 0 : 1;
}

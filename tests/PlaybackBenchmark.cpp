#include "playback/DecodeWorker.h"
#include "media/Inspection.h"
#include <QApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLPaintDevice>
#include <QPainter>
#include <QJsonDocument>
#include <QFile>
#include <QElapsedTimer>
#include <QJsonArray>
#include <Windows.h>
#include <psapi.h>
#include <mmsystem.h>
#include <algorithm>
#include <memory>

using namespace editor;
namespace {
quint64 fileTime(FILETIME t) { return static_cast<quint64>(t.dwHighDateTime) << 32 | t.dwLowDateTime; }
quint64 cpuTime() { FILETIME a{}, b{}, c{}, d{}; GetProcessTimes(GetCurrentProcess(), &a, &b, &c, &d); return fileTime(c) + fileTime(d); }
QJsonObject memory() { PROCESS_MEMORY_COUNTERS_EX counters{}; counters.cb = sizeof(counters);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
    return {{"workingSetBytes", static_cast<qint64>(counters.WorkingSetSize)}, {"peakWorkingSetBytes", static_cast<qint64>(counters.PeakWorkingSetSize)}, {"privateBytes", static_cast<qint64>(counters.PrivateUsage)}};
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); const auto args = app.arguments(); if (args.size() != 6) return 2;
    // Match the GUI's precise timer pacing, rather than the default 15.6 ms console sleep quantum.
    timeBeginPeriod(1); struct Resolution { ~Resolution() { timeEndPeriod(1); } } resolution;
    const auto inspected = media::inspect(args[1]); const auto description = playback::compileSource(inspected.media);
    if (!inspected.error.isEmpty() || description.video.isEmpty()) return 3;
    const bool accelerated = args[2] == "d3d11", glRequested = args[3] == "gl";
    const int durationMs = args[4].toInt();
    QOffscreenSurface surface; QOpenGLContext context; QString renderer, fallback;
    std::unique_ptr<QOpenGLFramebufferObject> fbo;
    std::unique_ptr<QOpenGLPaintDevice> glDevice;
    QImage target(1280, 720, QImage::Format_RGB32); target.fill(Qt::black);
    if (glRequested) {
        if (context.create()) { surface.setFormat(context.format()); surface.create(); }
        if (surface.isValid() && context.makeCurrent(&surface)) {
            renderer = QString::fromLatin1(reinterpret_cast<const char*>(context.functions()->glGetString(GL_RENDERER)));
            fbo = std::make_unique<QOpenGLFramebufferObject>(1280, 720);
            if (fbo->isValid()) glDevice = std::make_unique<QOpenGLPaintDevice>(1280, 720);
        }
        if (!glDevice) fallback = "OpenGL context/FBO unavailable; CPU presentation used";
    }
    const auto initialMemory = memory(); const auto initialCpu = cpuTime();
    const auto mode = accelerated ? playback::DecodeMode::D3D11 : playback::DecodeMode::Cpu;
    playback::DecodeWorker decoder(description.video[0], 0, false, mode); decoder.start();
    QElapsedTimer elapsed; elapsed.start(); playback::VideoFrame frame; int dropped = 0, presented = 0;
    while (!decoder.takeVideo(0, frame, dropped) && !decoder.done() && elapsed.elapsed() < 15000) { app.processEvents(); QThread::msleep(1); }
    const auto startupMs = elapsed.elapsed(); bool first = !frame.image.isNull();
    QElapsedTimer playbackClock; playbackClock.start(); qint64 presentationUs = 0, nextMemoryMs = 0; QJsonArray memorySamples;
    while (playbackClock.elapsed() < durationMs) {
        if (first || decoder.takeVideo(playbackClock.nsecsElapsed() / 1000, frame, dropped)) {
            first = false; QElapsedTimer paint; paint.start();
            if (glDevice) fbo->bind();
            QPainter painter(glDevice ? static_cast<QPaintDevice*>(glDevice.get()) : static_cast<QPaintDevice*>(&target));
            painter.fillRect(QRect(0, 0, 1280, 720), Qt::black);
            auto size = frame.image.size().scaled(QSize(1280, 720), Qt::KeepAspectRatio);
            QRect area(QPoint{}, size); area.moveCenter(QPoint(640, 360));
            painter.setRenderHint(QPainter::SmoothPixmapTransform); painter.drawImage(area, frame.image); painter.end();
            if (glDevice) context.functions()->glFinish(); // Measure completed GPU work, not submission alone.
            presentationUs += paint.nsecsElapsed() / 1000; ++presented;
        }
        app.processEvents(); QThread::msleep(1);
        if (playbackClock.elapsed() >= nextMemoryMs) { auto snapshot = memory(); snapshot["elapsedMs"] = playbackClock.elapsed();
            memorySamples.append(snapshot); nextMemoryMs += 500; }
    }
    QImage painted = (glDevice ? fbo->toImage() : target).convertToFormat(QImage::Format_RGB32);
    const auto lastPresentedPts = frame.ptsUs;
    QImage expected(1280, 720, QImage::Format_RGB32); expected.fill(Qt::black);
    { QPainter painter(&expected); const auto size = frame.image.size().scaled(QSize(1280, 720), Qt::KeepAspectRatio);
        QRect area(QPoint{}, size); area.moveCenter(QPoint(640, 360));
        painter.setRenderHint(QPainter::SmoothPixmapTransform); painter.drawImage(area, frame.image); }
    qint64 difference = 0;
    for (int y = 0; y < expected.height(); ++y) for (int x = 0; x < expected.width() * 4; ++x)
        difference += std::abs(static_cast<int>(expected.constScanLine(y)[x]) - painted.constScanLine(y)[x]);
    const double pixelDifference = difference / static_cast<double>(expected.sizeInBytes());
    const auto endMemory = memory(); const auto cpu100ns = cpuTime() - initialCpu;
    decoder.cancel(); decoder.wait(); const auto stats = decoder.stats();
    const qint64 seekTarget = std::min(1500000LL, description.video[0].endUs / 2);
    playback::DecodeWorker seeker(description.video[0], seekTarget, false, mode); elapsed.restart(); seeker.start();
    frame = {};
    while (!seeker.takeVideo(seekTarget, frame, dropped) && !seeker.done() && elapsed.elapsed() < 15000) { app.processEvents(); QThread::msleep(1); }
    const auto seekMs = elapsed.elapsed(); seeker.cancel(); seeker.wait();
    QJsonObject report{{"path", args[1]}, {"requestedDecode", args[2]}, {"requestedPresentation", args[3]},
        {"presentation", glDevice ? "OpenGL FBO / QPainter" : "CPU QImage / QPainter"}, {"glRenderer", renderer}, {"presentationFallback", fallback},
        {"startupMs", startupMs}, {"seekMs", seekMs}, {"playbackWallMs", durationMs}, {"presented", presented}, {"dropped", dropped},
        {"lastPresentedPtsUs", lastPresentedPts}, {"presentationPixelMeanAbsoluteDifference", pixelDifference},
        {"meanPresentationUs", presented ? static_cast<double>(presentationUs) / presented : 0},
        {"cpuOneCorePercent", cpu100ns / (durationMs + startupMs) / 100.0}, {"memoryBefore", initialMemory}, {"memoryAfter", endMemory}, {"memorySamples", memorySamples}, {"decoder", stats},
        {"seekDecoder", seeker.stats()}, {"passed", presented > 0 && !frame.image.isNull() && stats["error"].toString().isEmpty() && pixelDifference < 2},
        {"evidenceLevel", "Paced native decoder plus CPU image or hardware OpenGL offscreen FBO; no visible QOpenGLWidget/display-refresh/audibility acceptance"}};
    QFile file(args[5]); if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(report).toJson()) < 0) return 4;
    painted.save(args[5] + ".png");
    return report["passed"].toBool() ? 0 : 1;
}

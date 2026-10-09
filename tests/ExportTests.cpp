#include "export/ExportWorker.h"
#include "export/ExportDialog.h"
#include "playback/AudioMixer.h"
#include "media/Inspection.h"
#include "timeline/Timeline.h"
#include "MainWindow.h"
#include "Diagnostics.h"
#include "project/ProjectStore.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QAction>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace editor;
namespace {
bool waitFor(const std::function<bool()>& predicate, int timeout = 60000) {
    QElapsedTimer clock; clock.start();
    while (clock.elapsed() < timeout) {
        if (predicate()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1);
    }
    return false;
}
QByteArray read(const QString& path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
bool write(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QByteArray process(const QString& exe, const QStringList& args, bool& passed) {
    QProcess p; p.start(exe, args); passed = p.waitForStarted() && p.waitForFinished(90000) && p.exitCode() == 0;
    if (!passed) { std::fprintf(stderr, "Process failed: %s\n", p.readAllStandardError().constData()); p.kill(); p.waitForFinished(); }
    return p.readAllStandardOutput();
}
project::Clip mediaClip(const project::Media& media, const QString& kind, qint64 start, qint64 length) {
    project::Clip c; c.id = project::newId(); c.name = media.name; c.mediaId = media.id; c.kind = kind; c.startFrame = start; c.durationFrames = length;
    for (const auto& s : media.streams) if (s.kind == kind) { c.streamIndex = s.index; c.sourceDurationTicks = *project::framesToTicks(length, {30, 1}, s.timeBase, project::Rounding::Nearest); break; }
    return c;
}
QImage frameAt(const QByteArray& pixels, qint64 frame, int width, int height) {
    const auto offset = frame * width * height * 4;
    if (offset < 0 || offset + width * height * 4 > pixels.size()) return {};
    return QImage(reinterpret_cast<const uchar*>(pixels.constData() + offset), width, height, width * 4, QImage::Format_RGB32).copy();
}
double imageError(const QImage& a, const QImage& b) {
    if (a.size() != b.size() || a.isNull()) return 1000;
    double sum = 0;
    for (int y = 0; y < a.height(); ++y) for (int x = 0; x < a.width(); ++x) {
        const auto p = a.pixelColor(x, y), q = b.pixelColor(x, y);
        sum += std::abs(p.red() - q.red()) + std::abs(p.green() - q.green()) + std::abs(p.blue() - q.blue());
    }
    return sum / (a.width() * a.height() * 3);
}
QVector<float> pcm(const QByteArray& bytes) {
    QVector<float> result(bytes.size() / 4); if (!bytes.isEmpty()) std::memcpy(result.data(), bytes.data(), static_cast<size_t>(result.size()) * 4); return result;
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    if (app.arguments().size() != 3) return 2;
    QDir root(app.arguments()[1]), out(app.arguments()[2]); QDir().mkpath(out.absolutePath());
    QJsonArray failures, exports, comparisons; int checks = 0;
    auto check = [&](bool ok, const QString& message) { ++checks; if (!ok) { failures.append(message); std::fprintf(stderr, "FAIL: %s\n", qPrintable(message)); } };
    check(QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/segoeui.ttf")) >= 0, "Load installed font for offscreen reference titles");
    const auto lock = QJsonDocument::fromJson(read(root.filePath("dependencies.lock.json"))).object();
    const auto bin = root.filePath(lock["packages"].toArray()[1].toObject()["prefix"].toString() + "/bin/");
    const QString ffmpeg = bin + "ffmpeg.exe", ffprobe = bin + "ffprobe.exe";
    bool ok = false;
    QDir().mkpath(root.filePath("fixtures/generated/session-8"));
    const auto greenPath = root.filePath("fixtures/generated/session-8/green.mp4");
    process(ffmpeg, {"-hide_banner", "-loglevel", "error", "-nostdin", "-y", "-f", "lavfi", "-i", "color=c=green:s=320x180:r=24:d=2", "-c:v", "libx264", "-pix_fmt", "yuv420p", greenPath}, ok);
    check(ok, "Create independent 24 fps overlay source");
    const auto sync = media::inspect(root.filePath("fixtures/generated/session-5/flash-beep.mp4"));
    const auto green = media::inspect(greenPath);
    const auto tone = media::inspect(root.filePath("fixtures/generated/session-7/soundtrack.wav"));
    check(sync.error.isEmpty() && green.error.isEmpty() && tone.error.isEmpty(), "Inspect real test media");
    auto p = project::newProject("Session 8 export reference"); p.media = {sync.media, green.media, tone.media};
    auto& seq = p.sequences[0]; seq.width = 640; seq.height = 360; seq.durationFrames = 180;
    auto first = mediaClip(sync.media, "video", 0, 120), last = mediaClip(sync.media, "video", 150, 30);
    for (const auto& s : sync.media.streams) if (s.kind == "video") last.sourceInTicks = *project::framesToTicks(150, seq.frameRate, s.timeBase, project::Rounding::Nearest);
    seq.tracks[0].clips = {first, last}; seq.tracks[1].clips = {mediaClip(sync.media, "audio", 0, 180)};
    project::Track overlay; overlay.id = project::newId(); overlay.name = "24 fps overlay"; overlay.clips = {mediaClip(green.media, "video", 60, 30)}; seq.tracks.append(overlay);
    project::Track soundtrack; soundtrack.id = project::newId(); soundtrack.name = "Soundtrack"; soundtrack.kind = "audio"; soundtrack.gain = 0.5;
    project::AudioBus musicBus{project::newId(), "Music", 0.8, 0.15, false, false}; seq.audioBuses.append(musicBus);
    soundtrack.audioBusId = musicBus.id; soundtrack.pan = 0.1;
    soundtrack.audioAutomation.volume = {{0, 1, "linear"}, {180, 0.6, "eased"}};
    soundtrack.audioAutomation.pan = {{0, -0.2, "linear"}, {180, 0.2, "linear"}};
    auto sound = mediaClip(tone.media, "audio", 90, 90); sound.gain = 0.4; sound.pan = -0.05; sound.fadeInFrames = 30; sound.fadeOutFrames = 30;
    sound.audioAutomation.volume = {{0, 0.5, "linear"}, {90, 1, "linear"}};
    sound.audioAutomation.pan = {{0, -0.1, "linear"}, {90, 0.1, "linear"}};
    soundtrack.clips = {sound}; seq.tracks.append(soundtrack);
    project::Title title; title.id = project::newId(); title.text = "Export reference"; title.fontSize = 32; title.y = 0.8; title.background = "#80000000"; title.shadow = true; p.titles = {title};
    project::Track titles; titles.id = project::newId(); titles.name = "Titles"; titles.kind = "title";
    project::Clip t; t.id = project::newId(); t.name = title.text; t.kind = "title"; t.titleId = title.id; t.startFrame = 30; t.durationFrames = 90; titles.clips = {t}; seq.tracks.append(titles);
    check(project::validate(p).isEmpty(), "Reference project validates");
    check(project::deserialize(project::serialize(p)).project == std::optional<project::Project>(p), "Automated bus mix saves and reopens without changing its render graph");
    const auto graph = playback::compileSequence(p);
    auto settings = p.exportSettings; settings.width = 320; settings.height = 180; settings.preset = "veryfast"; settings.quality = 18; settings.outputPath = out.filePath("reference.mp4");
    exporting::ExportDialog sequenceRateDialog(graph, project::ExportSettings{});
    check(sequenceRateDialog.findChild<QSpinBox*>("exportRateNumerator")->value() == seq.frameRate.numerator &&
        sequenceRateDialog.findChild<QSpinBox*>("exportRateDenominator")->value() == seq.frameRate.denominator,
        "No Primary Video uses the sequence frame rate as the automatic export FPS"); sequenceRateDialog.reject();
    auto primaryProject = p; primaryProject.sequences[0].frameRate = {25, 1};
    primaryProject.sequences[0].primaryVideoMediaId = sync.media.id;
    project::Rational primaryRate{0, 1}, slowerImportedRate{0, 1};
    for (const auto& stream : sync.media.streams) if (stream.kind == "video") {
        primaryProject.sequences[0].primaryVideoStreamIndex = stream.index; primaryRate = stream.frameRate; break;
    }
    for (const auto& stream : green.media.streams) if (stream.kind == "video") { slowerImportedRate = stream.frameRate; break; }
    const auto primaryGraph = playback::compileSequence(primaryProject);
    exporting::ExportDialog primaryRateDialog(primaryGraph, project::ExportSettings{});
    check(primaryRate != slowerImportedRate && primaryRateDialog.findChild<QSpinBox*>("exportRateNumerator")->value() == primaryRate.numerator &&
        primaryRateDialog.findChild<QSpinBox*>("exportRateDenominator")->value() == primaryRate.denominator,
        "Automatic export FPS follows the designated primary stream instead of the lowest imported source rate"); primaryRateDialog.reject();
    auto manualRate = project::ExportSettings{}; manualRate.frameRateCustomized = true; manualRate.frameRate = {60, 1};
    exporting::ExportDialog manualRateDialog(primaryGraph, manualRate);
    check(manualRateDialog.findChild<QSpinBox*>("exportRateNumerator")->value() == 60 &&
        manualRateDialog.findChild<QSpinBox*>("exportRateDenominator")->value() == 1,
        "Manual output FPS override remains selected with a Primary Video"); manualRateDialog.reject();
    auto run = [&](const playback::RenderDescription& d, project::ExportSettings s) {
        exporting::ExportWorker worker(d, s); int ticks = 0, highest = 0, previous = -1; bool monotonic = true;
        QTimer heartbeat; heartbeat.setInterval(5); QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; }); heartbeat.start();
        QObject::connect(&worker, &exporting::ExportWorker::progress, &app, [&](int percent) { monotonic &= percent >= previous; previous = percent; highest = percent; });
        worker.start(); const bool done = waitFor([&] { return worker.isFinished(); });
        worker.cancel(); worker.wait(); QCoreApplication::processEvents();
        const auto result = worker.result(); exports.append(result);
        check(done && result["passed"].toBool(), "Export completes: " + result["error"].toString());
        check(monotonic && highest == 100 && ticks > 0, "Export progress is monotonic and UI heartbeat stays live");
        return result;
    };
    const auto result = run(graph, settings);
    check(result["frames"].toInteger() == 180 && result["samples"].toInteger() == 288000, "Export schedules exact frame and sample counts");
    check(result["audioMix"].toObject().contains("masterPeak") && result["audioMix"].toObject().contains("clippedSamples"), "Export reports the same mix peak and clipping diagnostics used by preview");
    auto probe = [&](const QString& path) {
        const auto bytes = process(ffprobe, {"-v", "error", "-count_frames", "-show_streams", "-show_format", "-of", "json", path}, ok);
        check(ok, "Independent ffprobe opens " + path); return QJsonDocument::fromJson(bytes).object();
    };
    const auto metadata = probe(settings.outputPath); write(out.filePath("ffprobe-reference.json"), QJsonDocument(metadata).toJson());
    check(std::abs(metadata["format"].toObject()["duration"].toString().toDouble() - 6.0) < 0.002, "MP4 duration matches six-second edit within 2 ms");
    const auto streams = metadata["streams"].toArray(); check(streams.size() == 2, "MP4 has video and audio streams");
    if (streams.size() == 2) {
        const auto v = streams[0].toObject(), a = streams[1].toObject();
        check(v["codec_name"] == "h264" && v["pix_fmt"] == "yuv420p" && v["width"] == 320 && v["height"] == 180 && v["avg_frame_rate"] == "30/1" && v["nb_read_frames"] == "180", "Independent H.264 format, size, frame rate and count");
        check(a["codec_name"] == "aac" && a["sample_rate"] == "48000" && a["channels"] == 2 && std::abs(a["duration"].toString().toDouble() - 6) < 0.002, "Independent AAC stereo rate and duration");
    }
    const auto pixels = process(ffmpeg, {"-v", "error", "-i", settings.outputPath, "-an", "-pix_fmt", "bgra", "-f", "rawvideo", "pipe:1"}, ok);
    check(ok && pixels.size() == 180 * 320 * 180 * 4, "Independent decoder reads every exported frame including first and last");
    const auto syncPixels = process(ffmpeg, {"-v", "error", "-i", sync.media.path, "-t", "6", "-an", "-pix_fmt", "bgra", "-f", "rawvideo", "pipe:1"}, ok); check(ok, "Independent source reference decoder");
    const auto greenPixels = process(ffmpeg, {"-v", "error", "-i", green.media.path, "-frames:v", "1", "-an", "-pix_fmt", "bgra", "-f", "rawvideo", "pipe:1"}, ok); check(ok, "Independent overlay reference decoder");
    for (int frame : {0, 29, 30, 59, 60, 89, 90, 119, 120, 149, 150, 179}) {
        QImage raw;
        if (frame >= 60 && frame < 90) raw = frameAt(greenPixels, 0, 320, 180);
        else if (frame < 120 || frame >= 150) raw = frameAt(syncPixels, frame * 2, 640, 360);
        const auto reference = playback::renderFrame(graph, raw, frame * 1000000LL / 30, {320, 180});
        const auto actual = frameAt(pixels, frame, 320, 180); const auto error = imageError(reference, actual);
        comparisons.append(QJsonObject{{"frame", frame}, {"meanRgbError", error}});
        check(error < 8, "Export/reference pixels at frame " + QString::number(frame) + " error " + QString::number(error));
        if (frame == 30) { actual.save(out.filePath("export-title.png")); reference.save(out.filePath("reference-title.png")); }
    }
    // Lossy AAC may ring and pad its final frame. Compare aligned PCM only over the exact edit interval.
    const auto audioBytes = process(ffmpeg, {"-v", "error", "-i", settings.outputPath, "-vn", "-ac", "2", "-ar", "48000", "-f", "f32le", "pipe:1"}, ok);
    auto actualAudio = pcm(audioBytes); check(ok && actualAudio.size() >= 288000 * 2 && actualAudio.size() <= (288000 + 1023) * 2, "AAC decoder trims priming; tail padding is below 1024 samples");
    playback::AudioMixer mixer(graph.audio, 0, graph.durationUs); mixer.start(); QVector<float> referenceAudio;
    check(waitFor([&] { playback::AudioBlock b; while (mixer.takeAudio(b)) referenceAudio += b.samples; return mixer.done(); }), "Collect shared reference PCM"); mixer.cancel(); mixer.wait();
    double squaredError = 0;
    for (qsizetype n = 0; n < std::min(actualAudio.size(), referenceAudio.size()); ++n) { const auto error = actualAudio[n] - referenceAudio[n]; squaredError += error * error; }
    const auto rms = std::sqrt(squaredError / std::max<qsizetype>(1, referenceAudio.size())); check(rms < 0.012, "AAC PCM matches gain/fade/overlap mix within lossy RMS tolerance: " + QString::number(rms));
    for (int second : {0, 1, 5}) {
        const auto flash = frameAt(pixels, second * 30, 320, 180).pixelColor(10, 10);
        double energy = 0; if (actualAudio.size() >= (second * 48000 + 2400) * 2) for (int n = second * 48000; n < second * 48000 + 2400; ++n) energy += actualAudio[n * 2] * actualAudio[n * 2];
        check(flash.red() > 235 && flash.green() > 235 && flash.blue() > 235 && energy > 1, "Flash and beep share exact output second " + QString::number(second));
    }
    // Settings changes preserve edit history and serialize without schema expansion.
    timeline::TimelineEditor editor(p); const auto original = editor.state();
    check(editor.execute(timeline::SetExportSettings{settings}).isEmpty() && editor.state().project.exportSettings == settings, "Export choices use command history");
    check(editor.undo() && editor.state() == original && editor.redo(), "Undo/redo restores export choices exactly");
    check(project::deserialize(project::serialize(editor.state().project)).project == std::optional<project::Project>(editor.state().project), "Export settings round-trip through schema 3");
    // Cancel a nontrivial 1080p render after it has begun, preserving destination and removing temp files.
    auto slow = settings; slow.width = 1920; slow.height = 1080; slow.preset = "slow"; slow.outputPath = out.filePath("cancelled.mp4");
    const QByteArray sentinel("existing destination bytes"); check(write(slow.outputPath, sentinel), "Create cancellation sentinel");
    const auto beforeFiles = out.entryList(QDir::Files | QDir::Hidden, QDir::Name);
    exporting::ExportWorker cancelled(graph, slow); bool requested = false;
    QObject::connect(&cancelled, &exporting::ExportWorker::progress, &app, [&](int percent) { if (percent > 0 && !requested) { requested = true; cancelled.cancel(); } });
    cancelled.start(); check(waitFor([&] { return cancelled.isFinished(); }), "Cancellation terminates export worker"); cancelled.cancel(); cancelled.wait(); QCoreApplication::processEvents();
    check(requested && cancelled.result()["cancelled"].toBool() && read(slow.outputPath) == sentinel, "Mid-render cancellation preserves existing destination bytes");
    check(out.entryList(QDir::Files | QDir::Hidden, QDir::Name) == beforeFiles, "Cancellation removes temporary export");
    exports.append(cancelled.result()); auto retry = settings; retry.outputPath = slow.outputPath; run(graph, retry);
    check(read(retry.outputPath) != sentinel && probe(retry.outputPath)["streams"].toArray().size() == 2, "Same destination exports successfully after cancellation");
    // Decoder failures after preflight must not publish partial output.
    const auto brokenPath = out.filePath("corrupt.mp4"); write(brokenPath, "not a media file"); auto broken = graph; broken.video[0].path = brokenPath;
    auto bad = settings; bad.outputPath = out.filePath("failed.mp4"); write(bad.outputPath, sentinel);
    exporting::ExportWorker failure(broken, bad); failure.start(); check(waitFor([&] { return failure.isFinished(); }), "Corrupt media fails without hanging"); failure.cancel(); failure.wait();
    check(!failure.result()["passed"].toBool() && !failure.result()["error"].toString().isEmpty() && read(bad.outputPath) == sentinel, "Decode failure preserves existing output and reports reason");
    auto brokenAudio = graph; brokenAudio.audio[0].path = brokenPath;
    exporting::ExportWorker audioFailure(brokenAudio, bad); audioFailure.start(); check(waitFor([&] { return audioFailure.isFinished(); }), "Corrupt audio fails without hanging"); audioFailure.cancel(); audioFailure.wait();
    check(!audioFailure.result()["passed"].toBool() && audioFailure.result()["error"].toString().contains("Audio") && read(bad.outputPath) == sentinel, "Mixer source error fails export rather than publishing silence");
    auto invalid = settings; invalid.width = 321; check(!exporting::validateExport(graph, invalid).isEmpty(), "Reject odd 4:2:0 dimensions");
    invalid = settings; invalid.outputPath = sync.media.path; check(!exporting::validateExport(graph, invalid).isEmpty(), "Reject source overwrite");
    invalid = settings; invalid.frameRate = {241, 1}; check(!exporting::validateExport(graph, invalid).isEmpty(), "Reject unsupported frame rate");
    invalid = settings; invalid.preset = "unknown"; check(!exporting::validateExport(graph, invalid).isEmpty(), "Reject unknown preset");
    auto empty = graph; empty.sequence->durationFrames = 0; check(!exporting::validateExport(empty, settings).isEmpty(), "Reject empty sequence");
    auto offline = graph; offline.audio[0].path = out.filePath("absent.wav"); check(!exporting::validateExport(offline, settings).isEmpty(), "Report offline audio before rendering");
    auto effect = graph; project::Effect e; e.id = project::newId(); e.type = "future"; effect.sequence->tracks[0].clips[0].effects = {e}; check(!exporting::validateExport(effect, settings).isEmpty(), "Reject enabled unevaluated effect explicitly");
    invalid = settings; invalid.outputPath = out.filePath("missing-directory/output.mp4"); exporting::ExportWorker fileFailure(graph, invalid); fileFailure.start(); check(waitFor([&] { return fileFailure.isFinished(); }), "Bad output directory fails promptly"); fileFailure.wait(); check(!fileFailure.result()["passed"].toBool(), "Useful file-write failure");
    auto muted = settings; muted.audioEnabled = false; muted.outputPath = out.filePath("video-only.mp4"); run(graph, muted); check(probe(muted.outputPath)["streams"].toArray().size() == 1, "Audio disabled produces video-only MP4");
    // Fractional sequence/output rates: ceiling video coverage, nearest sample boundary, no float drift.
    auto fractional = graph; fractional.sequence->durationFrames = 91; fractional.frameRate = {30000, 1001}; fractional.sequence->frameRate = fractional.frameRate;
    fractional.video.clear(); fractional.audio.clear(); fractional.titles.clear(); fractional.sequence->tracks.clear();
    fractional.durationUs = *project::framesToTicks(91, fractional.frameRate, {1, 1000000}, project::Rounding::Nearest);
    auto fractionalSettings = settings; fractionalSettings.outputPath = out.filePath("fractional.mp4"); fractionalSettings.frameRate = {24, 1};
    const auto fractionalResult = run(fractional, fractionalSettings); const auto fractionalProbe = probe(fractionalSettings.outputPath);
    check(fractionalResult["frames"].toInteger() == 73 && fractionalResult["samples"].toInteger() == 145746, "Exact 30000/1001 sequence converted to 24 fps with ceiling coverage");
    check(std::abs(fractionalProbe["format"].toObject()["duration"].toString().toDouble() - 73.0 / 24) < 0.002, "Converted output duration differs by less than one output frame");
    // Full-resolution composition preserves title scale, sequence aspect and black output bars.
    const auto full = playback::renderFrame(graph, frameAt(syncPixels, 60, 640, 360), 1000000, {1920, 1080});
    const auto small = playback::renderFrame(graph, frameAt(syncPixels, 60, 640, 360), 1000000, {320, 180});
    check(full.size() == QSize(1920, 1080) && imageError(full.scaled(320, 180, Qt::IgnoreAspectRatio, Qt::SmoothTransformation), small) < 4, "Full-resolution title rasterization matches scaled preview within font raster tolerance");
    const auto bars = playback::renderFrame(graph, frameAt(syncPixels, 0, 640, 360), 0, {320, 240});
    check(bars.pixelColor(160, 0) == QColor(Qt::black) && bars.pixelColor(160, 239) == QColor(Qt::black), "Different output aspect preserves sequence aspect with black bars");
    // Export representative originals and the pinned 4K/10-bit/VFR/rotation cases through the same path.
    QStringList samplePaths;
    for (const auto& file : QJsonDocument::fromJson(read(root.filePath("evidence/session-0/test-set/inventory.json"))).object()["files"].toArray()) samplePaths.append(file.toObject()["path"].toString());
    for (const auto& file : QJsonDocument::fromJson(read(root.filePath("evidence/session-0/synthetic/inventory.json"))).object()["files"].toArray()) samplePaths.append(file.toObject()["path"].toString());
    check(samplePaths.size() == 10, "Representative real/synthetic sample set contains ten sources");
    int sampleIndex = 0;
    for (const auto& path : samplePaths) {
        const auto inspected = media::inspect(path); check(inspected.error.isEmpty(), "Inspect export sample " + path);
        if (!inspected.error.isEmpty()) continue;
        auto sample = project::newProject("Session 8 sample coverage"); sample.media = {inspected.media}; auto& s = sample.sequences[0]; s.durationFrames = 12;
        s.tracks[0].clips = {mediaClip(inspected.media, "video", 0, 12)};
        bool hasAudio = false;
        for (const auto& stream : inspected.media.streams) {
            if (stream.kind == "video") {
                s.width = stream.width; s.height = stream.height;
                if (std::abs(stream.rotation) % 180 == 90) std::swap(s.width, s.height);
                s.tracks[0].clips[0].sourceInTicks = *project::framesToTicks(30, {30, 1}, stream.timeBase, project::Rounding::Nearest);
            }
            if (stream.kind == "audio" && !hasAudio) {
                hasAudio = true; s.tracks[1].clips = {mediaClip(inspected.media, "audio", 0, 12)};
                s.tracks[1].clips[0].sourceInTicks = *project::framesToTicks(30, {30, 1}, stream.timeBase, project::Rounding::Nearest);
            }
        }
        check(project::validate(sample).isEmpty(), "Source-trimmed sample project validates");
        auto sampleSettings = settings; sampleSettings.outputPath = out.filePath(QString("sample-%1.mp4").arg(sampleIndex++)); sampleSettings.audioEnabled = hasAudio;
        const auto sampleResult = run(playback::compileSequence(sample), sampleSettings);
        const auto sampleMetadata = probe(sampleSettings.outputPath);
        check(sampleResult["frames"].toInteger() == 12 && sampleMetadata["streams"].toArray()[0].toObject()["nb_read_frames"] == "12", "Original/synthetic sample preserves first/last output frames");
        process(ffmpeg, {"-v", "error", "-i", sampleSettings.outputPath, "-f", "null", "-"}, ok); check(ok, "Separate FFmpeg decoder plays complete sample export");
    }
    auto titleOnly = project::newProject("Full-resolution title-only export"); titleOnly.titles = {title}; auto& ts = titleOnly.sequences[0]; ts.durationFrames = 3;
    auto titleTrack = titles; titleTrack.clips[0].startFrame = 0; titleTrack.clips[0].durationFrames = 3; ts.tracks.append(titleTrack);
    auto highSettings = settings; highSettings.width = 1920; highSettings.height = 1080; highSettings.audioEnabled = false; highSettings.outputPath = out.filePath("full-resolution.mp4");
    run(playback::compileSequence(titleOnly), highSettings);
    const auto highProbe = probe(highSettings.outputPath);
    check(highProbe["streams"].toArray()[0].toObject()["width"] == 1920 && highProbe["streams"].toArray()[0].toObject()["nb_read_frames"] == "3", "Title-only export encodes native 1080p frames without the preview cap");
    // Real production dialog and menu, driven offscreen without computer use.
    exporting::ExportDialog dialog(graph, settings); dialog.show();
    check(dialog.findChild<QComboBox*>("exportFormat")->count() == exporting::presets().size() + 1, "Dialog offers fixed presets and custom encoder/container");
    dialog.findChild<QSpinBox*>("exportWidth")->setValue(321); dialog.findChild<QPushButton*>("exportStart")->click();
    check(dialog.findChild<QLabel*>("exportMessage")->text().contains("even") && !dialog.findChild<QPushButton*>("exportStart")->isEnabled(), "Dialog warns immediately and blocks incompatible dimensions");
    dialog.findChild<QSpinBox*>("exportWidth")->setValue(320); dialog.findChild<QLineEdit*>("exportPath")->setText(out.filePath("dialog.mp4"));
    dialog.findChild<QSpinBox*>("exportRateNumerator")->setValue(60); dialog.findChild<QSpinBox*>("exportRateDenominator")->setValue(2);
    QFile::remove(out.filePath("dialog.mp4")); int savedChoices = 0;
    QObject::connect(&dialog, &exporting::ExportDialog::settingsChosen, [&](auto s) { ++savedChoices; check(s.width == 320 && s.frameRate == project::Rational{30, 1}, "Dialog emits exact persisted settings"); });
    check(waitFor([&] { return dialog.findChild<QPushButton*>("exportStart")->isEnabled(); }), "Exact current settings pass background capability check");
    dialog.findChild<QPushButton*>("exportStart")->click();
    check(!dialog.findChild<QPushButton*>("exportStart")->isEnabled() && dialog.findChild<QPushButton*>("exportCancel")->isEnabled(), "Running dialog disables options and enables cancellation");
    check(waitFor([&] { return dialog.findChild<QPushButton*>("exportStart")->isEnabled(); }) && dialog.findChild<QProgressBar*>("exportProgress")->value() == 100 && savedChoices == 1, "Production dialog reports successful export and restores controls");
    const auto dialogOriginal = read(out.filePath("dialog.mp4"));
    auto answerOverwrite = [&](QMessageBox::StandardButton answer) {
        QTimer::singleShot(0, [=] { for (auto* w : QApplication::topLevelWidgets()) if (w->objectName() == "exportOverwriteDialog") qobject_cast<QMessageBox*>(w)->done(answer); });
    };
    answerOverwrite(QMessageBox::No); dialog.findChild<QPushButton*>("exportStart")->click();
    check(savedChoices == 1 && read(out.filePath("dialog.mp4")) == dialogOriginal, "Declining replace preserves output and does not start another export");
    answerOverwrite(QMessageBox::Yes); dialog.findChild<QPushButton*>("exportStart")->click();
    check(waitFor([&] { return dialog.findChild<QPushButton*>("exportStart")->isEnabled(); }) && savedChoices == 2 && dialog.findChild<QLabel*>("exportMessage")->text().contains("complete"), "Confirmed replacement exports again through the production dialog");
    dialog.findChild<QLineEdit*>("exportPath")->setText(out.filePath("missing-directory/dialog.mp4")); dialog.findChild<QPushButton*>("exportStart")->click();
    check(waitFor([&] { return dialog.findChild<QPushButton*>("exportStart")->isEnabled(); }) && dialog.findChild<QLabel*>("exportMessage")->text().contains("Cannot create"), "Dialog displays output failure and restores export controls");
    dialog.reject();
    auto longSettings = settings; longSettings.width = 1920; longSettings.height = 1080; longSettings.preset = "slow"; longSettings.outputPath = out.filePath("dialog-close.mp4"); QFile::remove(longSettings.outputPath);
    exporting::ExportDialog closing(graph, longSettings); closing.show();
    check(waitFor([&] { return closing.findChild<QPushButton*>("exportStart")->isEnabled(); }), "Long export settings validate asynchronously");
    closing.findChild<QPushButton*>("exportStart")->click(); closing.reject();
    check(waitFor([&] { return !closing.isVisible(); }) && !QFileInfo::exists(longSettings.outputPath), "Closing busy dialog cancels asynchronously without publishing output");
    const auto projectPath = out.filePath("reference.veproject"); check(project::ProjectStore::save(p, projectPath).isEmpty(), "Save reviewable reference project");
    QDir().mkpath(root.filePath("fixtures/generated/session-8")); const auto manualPath = root.filePath("fixtures/generated/session-8/export-reference.veproject");
    check(project::ProjectStore::save(p, manualPath).isEmpty(), "Prepare human export project");
    auto cancelProject = p; cancelProject.name = "Session 8 long cancellation check"; auto& cancelSequence = cancelProject.sequences[0]; cancelSequence.durationFrames = 1800;
    cancelSequence.tracks[0].clips.clear(); cancelSequence.tracks[1].clips.clear(); cancelSequence.tracks[2].clips.clear(); cancelSequence.tracks[3].clips.clear();
    for (int n = 0; n < 5; ++n) {
        cancelSequence.tracks[0].clips.append(mediaClip(sync.media, "video", n * 360, 360));
        cancelSequence.tracks[1].clips.append(mediaClip(sync.media, "audio", n * 360, 360));
    }
    cancelSequence.tracks[4].clips[0].startFrame = 0; cancelSequence.tracks[4].clips[0].durationFrames = 1800;
    check(project::ProjectStore::save(cancelProject, root.filePath("fixtures/generated/session-8/cancel-long.veproject")).isEmpty(), "Prepare one-minute human cancellation project");
    QSettings preferences(out.filePath("ui-settings.ini"), QSettings::IniFormat); Diagnostics diagnostics(out.filePath("ui-data")); MainWindow window(preferences, diagnostics);
    check(window.openProjectPath(projectPath), "Production main window opens reference edit");
    QTimer::singleShot(0, [&] { for (auto* w : QApplication::topLevelWidgets()) if (w->objectName() == "exportDialog") qobject_cast<QDialog*>(w)->reject(); });
    auto* action = window.findChild<QAction*>("exportSequenceAction"); check(action && action->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_E), "File export menu and shortcut exist"); if (action) action->trigger();
    check(window.project() == p, "Opening and closing export dialog preserves project");
    const QJsonObject report{{"passed", failures.isEmpty()}, {"checkCount", checks}, {"failures", failures}, {"exports", exports}, {"pixelComparisons", comparisons}, {"audioRmsError", rms},
        {"limits", "RGB mean error <8/255; AAC RMS error <0.012; AAC tail <1024 samples; duration <2 ms at matching rate or <1 output frame when converting fps; human separate-player check pending"}};
    write(out.filePath("export-result.json"), QJsonDocument(report).toJson());
    std::printf("Session 8 export: %d checks, %lld failures\n", checks, static_cast<long long>(failures.size())); return failures.isEmpty() ? 0 : 1;
}

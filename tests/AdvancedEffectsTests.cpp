#include "project/Effects.h"
#include "project/ProjectStore.h"
#include "timeline/TimelineWidget.h"
#include "playback/VisualEffects.h"
#include "playback/PlaybackController.h"
#include "playback/AudioMixer.h"
#include "export/ExportWorker.h"
#include "media/Inspection.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numbers>

using namespace editor;
namespace {
bool waitFor(const std::function<bool()>& predicate, int timeout = 60000) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < timeout) { if (predicate()) return true; QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1); }
    return predicate();
}
QByteArray read(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
bool write(const QString& path, const QByteArray& bytes) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(); }
QByteArray process(const QString& exe, const QStringList& args, bool& ok) {
    QProcess p; p.start(exe, args); ok = p.waitForStarted() && p.waitForFinished(60000) && p.exitCode() == 0;
    if (!ok) { std::fprintf(stderr, "%s\n", p.readAllStandardError().constData()); p.kill(); p.waitForFinished(); }
    return p.readAllStandardOutput();
}
project::Clip makeClip(const project::Media& m, const QString& kind, qint64 frames = 120) {
    project::Clip c; c.id = project::newId(); c.name = m.name; c.mediaId = m.id; c.kind = kind; c.durationFrames = frames;
    for (const auto& s : m.streams) if (s.kind == kind) { c.streamIndex = s.index; c.sourceDurationTicks = *project::framesToTicks(frames, {30, 1}, s.timeBase, project::Rounding::Nearest); break; }
    return c;
}
QImage imageAt(const QByteArray& bytes, qint64 n) {
    constexpr qint64 stride = 320 * 180 * 4; const auto offset = n * stride;
    if (offset < 0 || offset + stride > bytes.size()) return {};
    return QImage(reinterpret_cast<const uchar*>(bytes.constData() + offset), 320, 180, 320 * 4, QImage::Format_RGB32).copy();
}
double error(const QImage& a, const QImage& b) {
    if (a.size() != b.size() || a.isNull()) return 1000;
    double sum = 0; for (int y = 0; y < a.height(); ++y) for (int x = 0; x < a.width(); ++x) {
        const auto p = a.pixelColor(x, y), q = b.pixelColor(x, y);
        sum += std::abs(p.red() - q.red()) + std::abs(p.green() - q.green()) + std::abs(p.blue() - q.blue());
    }
    return sum / (a.width() * a.height() * 3);
}
void dialogEdit(const std::function<void(QWidget*)>& edit) { QTimer::singleShot(0, [edit] { for (auto* w : QApplication::topLevelWidgets()) if (w->objectName() == "effectProperties") { edit(w); return; } }); }
void accept(QWidget* w) { w->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click(); }
double pitch(const QVector<float>& pcm, qint64 begin, qint64 length) {
    double best = -1; int frequency = 0;
    for (int hz = 410; hz <= 470; ++hz) { double re = 0, im = 0;
        for (qint64 i = 0; i < length && (begin + i) * 2 < pcm.size(); ++i) {
            const auto phase = 2 * std::numbers::pi * hz * i / 48000;
            re += pcm[(begin + i) * 2] * std::cos(phase); im += pcm[(begin + i) * 2] * std::sin(phase);
        }
        const auto power = re * re + im * im; if (power > best) { best = power; frequency = hz; }
    }
    return best > 1 ? frequency : 0;
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false); if (app.arguments().size() != 3) return 2;
    QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/segoeui.ttf"));
    QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/arial.ttf"));
    QDir root(app.arguments()[1]), out(app.arguments()[2]); QDir().mkpath(out.absolutePath());
    QJsonArray failures, exports, measurements; int checks = 0; double worst = 0;
    auto check = [&](bool ok, const QString& label) { ++checks; if (!ok) { failures.append(label); std::fprintf(stderr, "FAIL: %s\n", qPrintable(label)); } };
    const auto lock = QJsonDocument::fromJson(read(root.filePath("dependencies.lock.json"))).object();
    const auto bin = root.filePath(lock["packages"].toArray()[1].toObject()["prefix"].toString() + "/bin/");
    QDir generated(root.filePath("fixtures/generated/session-12")); QDir().mkpath(generated.absolutePath()); bool ok = false;
    process(bin + "ffmpeg.exe", {"-v", "error", "-nostdin", "-y", "-f", "lavfi", "-i", "testsrc2=s=320x180:r=30:d=4", "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=4", "-c:v", "libx264", "-crf", "0", "-pix_fmt", "yuv420p", "-c:a", "pcm_s16le", generated.filePath("motion-tone.mkv")}, ok);
    check(ok, "Create moving SDR reference and independent 440 Hz audio");
    for (const auto& color : {QString("blue"), QString("lime")}) {
        QStringList args{"-v", "error", "-nostdin", "-y", "-f", "lavfi", "-i", "color=c=" + color + ":s=320x180:r=30:d=4"};
        if (color == "lime") args << "-vf" << "drawbox=x=120:y=50:w=80:h=80:color=red:t=fill";
        args << "-c:v" << "libx264" << "-crf" << "0" << "-pix_fmt" << "yuv420p" << generated.filePath(color + ".mp4"); process(bin + "ffmpeg.exe", args, ok); check(ok, "Create independent keyed/background footage");
    }
    const auto motion = media::inspect(generated.filePath("motion-tone.mkv")), blue = media::inspect(generated.filePath("blue.mp4")), green = media::inspect(generated.filePath("lime.mp4"));
    check(motion.error.isEmpty() && blue.error.isEmpty() && green.error.isEmpty(), "Inspect reference sources"); if (!failures.isEmpty()) return 1;
    const auto rawMotion = process(bin + "ffmpeg.exe", {"-v", "error", "-i", motion.media.path, "-an", "-f", "rawvideo", "-pix_fmt", "bgra", "pipe:1"}, ok); check(ok, "Independent raw moving source decode");
    const auto timestampBytes = process(bin + "ffprobe.exe", {"-v", "error", "-select_streams", "v:0", "-show_frames", "-show_entries", "frame=best_effort_timestamp_time", "-of", "json", motion.media.path}, ok);
    const auto timestamps = QJsonDocument::fromJson(timestampBytes).object()["frames"].toArray(); check(ok && timestamps.size() == 120, "Independently probe all actual source timestamps, including coarse MKV timebase");
    const auto rawGreen = process(bin + "ffmpeg.exe", {"-v", "error", "-i", green.media.path, "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "bgra", "pipe:1"}, ok);
    const auto rawBlue = process(bin + "ffmpeg.exe", {"-v", "error", "-i", blue.media.path, "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "bgra", "pipe:1"}, ok);
    auto p = project::newProject("Session 12 reference"); p.media = {motion.media, blue.media, green.media};
    auto& s = p.sequences[0]; s.width = 320; s.height = 180; s.durationFrames = 120;
    s.tracks[0].clips = {makeClip(motion.media, "video")}; s.tracks[1].clips = {makeClip(motion.media, "audio")};
    p.exportSettings.width = 320; p.exportSettings.height = 180; p.exportSettings.quality = 10; p.exportSettings.preset = "veryfast";
    check(project::validate(p).isEmpty(), "Reference project is valid");
    // Analytic SDR pixels, defaults, clipping, transparency and ordered composition.
    QImage solid(320, 180, QImage::Format_ARGB32_Premultiplied); solid.fill(QColor(64, 96, 128));
    auto c = s.tracks[0].clips[0];
    for (const auto& type : {QString("color"), QString("lut"), QString("mask"), QString("speed")}) {
        c.effects = {project::defaultEffect(type)}; check(error(solid, playback::applyEffects(solid, c, 0).image) == 0, type + " default is identity");
    }
    c.effects = {project::defaultEffect("color")}; c.effects[0].parameters["exposure"] = 1;
    auto pixel = playback::applyEffects(solid, c, 0).image.pixelColor(160, 90); check(pixel == QColor(128, 192, 255), "Exposure doubles encoded RGB and clips highlights");
    c.effects[0].parameters["exposure"] = 0; c.effects[0].parameters["saturation"] = 0;
    pixel = playback::applyEffects(solid, c, 0).image.pixelColor(160, 90); check(pixel == QColor(92, 92, 92), "Zero saturation uses documented Rec709 luma weights");
    c.effects[0].parameters["saturation"] = 1; c.effects[0].parameters["temperature"] = 1;
    pixel = playback::applyEffects(solid, c, 0).image.pixelColor(160, 90); check(pixel == QColor(128, 96, 64), "Temperature gains are explicit creative channel correction");
    c.effects[0].parameters["temperature"] = 0; c.effects[0].parameters["tint"] = 1;
    pixel = playback::applyEffects(solid, c, 0).image.pixelColor(160, 90); check(pixel == QColor(91, 48, 181), "Tint channel gains are deterministic");
    c.effects[0].parameters["tint"] = 0; c.effects[0].parameters["contrast"] = 0;
    check(playback::applyEffects(solid, c, 0).image.pixelColor(160, 90) == QColor(128, 128, 128), "Zero contrast resets all RGB to midpoint");
    QImage transparent = solid; transparent.fill(Qt::transparent); check(playback::applyEffects(transparent, c, 0).image.pixelColor(160, 90).alpha() == 0, "Color operations preserve transparent alpha");
    c.effects = {project::defaultEffect("mask")}; c.effects[0].parameters["left"] = 0.25; c.effects[0].parameters["right"] = 0.75; c.effects[0].parameters["feather"] = 0.1;
    auto masked = playback::applyEffects(solid, c, 0).image;
    check(masked.pixelColor(10, 90).alpha() == 0 && masked.pixelColor(160, 90).alpha() == 255 && masked.pixelColor(90, 90).alpha() > 0 && masked.pixelColor(90, 90).alpha() < 255, "Rectangle mask clips and feathers inside its edge");
    c.effects[0].parameters["invert"] = true; check(playback::applyEffects(solid, c, 0).image.pixelColor(160, 90).alpha() == 0, "Inverted mask reverses coverage");
    c.effects[0].parameters["right"] = 0.1; c.effects[0].parameters["invert"] = false; check(playback::applyEffects(solid, c, 0).image.pixelColor(160, 90).alpha() == 0, "Crossed mask edges produce empty layer");
    c.effects = {project::defaultEffect("chromaKey")}; QImage keys(3, 1, QImage::Format_ARGB32_Premultiplied); keys.setPixelColor(0, 0, Qt::green); keys.setPixelColor(1, 0, QColor(0, 190, 0)); keys.setPixelColor(2, 0, Qt::red);
    auto keyed = playback::applyEffects(keys, c, 0).image; check(keyed.pixelColor(0, 0).alpha() == 0 && keyed.pixelColor(1, 0).alpha() > 0 && keyed.pixelColor(1, 0).alpha() < 255 && keyed.pixelColor(2, 0).alpha() == 255, "Chroma key has transparent exact key, soft edge and opaque subject");
    c.effects[0].parameters["spill"] = 1; check(playback::applyEffects(keys, c, 0).image.pixelColor(1, 0).green() < keyed.pixelColor(1, 0).green(), "Spill suppression reduces dominant key channel at soft edges");
    QByteArray cube("TITLE \"Swap red and blue\"\nLUT_3D_SIZE 2\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n");
    for (int b = 0; b < 2; ++b) for (int g = 0; g < 2; ++g) for (int r = 0; r < 2; ++r) cube += QByteArray::number(b) + ' ' + QByteArray::number(g) + ' ' + QByteArray::number(r) + '\n';
    const auto lutPath = generated.filePath("swap.cube"); write(lutPath, cube); auto lut = project::defaultEffect("lut");
    check(project::importCube(lut, lutPath).isEmpty(), "Bounded normalized Cube import"); c.effects = {lut};
    check(playback::applyEffects(solid, c, 0).image.pixelColor(160, 90) == QColor(128, 96, 64), "Trilinear LUT swaps channels at an interior RGB point");
    write(out.filePath("invalid.cube"), "LUT_3D_SIZE 33\n0 0 0\n"); auto beforeLut = lut;
    check(!project::importCube(lut, out.filePath("invalid.cube")).isEmpty() && lut == beforeLut, "Incomplete LUT import is transactional");
    write(out.filePath("invalid.cube"), "LUT_3D_SIZE 2\nDOMAIN_MAX 2 1 1\n"); check(!project::importCube(lut, out.filePath("invalid.cube")).isEmpty(), "Non-normalized LUT domains fail usefully");
    // Time remapping: integral references and endpoint normalization, no reverse/freeze/out-of-bounds.
    auto speed = project::defaultEffect("speed"); speed.parameters["rate"] = 2;
    const auto sid = s.id, vtid = s.tracks[0].id, atid = s.tracks[1].id, vid = s.tracks[0].clips[0].id, aid = s.tracks[1].clips[0].id;
    timeline::TimelineEditor editor(p); const auto baseline = editor.state();
    check(editor.execute(timeline::SetClipEffects{sid, vtid, vid, {speed}}).isEmpty(), "Constant speed command succeeds");
    const auto fastProject = editor.state().project; const auto fast = fastProject.sequences[0].tracks[0].clips[0];
    check(fast.durationFrames == 60 && fast.sourceDurationTicks == p.sequences[0].tracks[0].clips[0].sourceDurationTicks && fastProject.sequences[0].tracks[1] == p.sequences[0].tracks[1], "2x halves duration, preserves source bounds and leaves audio track intact");
    editor.undo(); check(editor.state() == baseline, "Speed undo restores exact state"); editor.redo(); check(editor.state().project == fastProject, "Speed redo restores exact settings");
    auto ramp = speed; ramp.keyframes["rate"] = {{0, 0.5, "linear"}, {60, 2, "linear"}};
    auto rampClip = p.sequences[0].tracks[0].clips[0]; rampClip.effects = {ramp};
    check(std::abs(project::speedIntegral(rampClip, 60) - 75) < 1e-9 && std::abs(project::speedIntegral(rampClip, 30) - 26.25) < 1e-9, "Linear speed integral has independent analytic values");
    ramp.keyframes["rate"][0].curve = "eased"; rampClip.effects = {ramp}; check(std::abs(project::speedIntegral(rampClip, 30) - 23.4375) < 1e-9, "Eased speed integrates smoothstep analytically");
    ramp.keyframes["rate"][0].curve = "hold"; rampClip.effects = {ramp}; check(project::speedIntegral(rampClip, 60) == 30 && project::speedIntegral(rampClip, 61) == 32, "Hold speed jumps at exact key boundary");
    ramp.keyframes["rate"][0].curve = "linear";
    editor.replaceProject(p); check(editor.execute(timeline::SetClipEffects{sid, vtid, vid, {ramp}}).isEmpty(), "Speed ramp applies transactionally");
    const auto rampProject = editor.state().project; const auto original = rampProject.sequences[0].tracks[0].clips[0];
    check(original.durationFrames == 83 && timeline::sourceBoundary(original, 0) == original.sourceInTicks && timeline::sourceBoundary(original, 83) == original.sourceInTicks + original.sourceDurationTicks, "Ramp resolves integer duration and exact source endpoints");
    check(editor.execute(timeline::SplitClip{sid, vtid, vid, 31, project::newId(), {project::newId()}}).isEmpty(), "Ramp split uses remapped source boundary");
    const auto splitProject = editor.state().project;
    const auto halves = splitProject.sequences[0].tracks[0].clips;
    for (qint64 f = 0; f <= original.durationFrames; ++f) {
        const auto& part = f < 31 ? halves[0] : halves[1]; const auto mapped = timeline::sourceBoundary(part, f < 31 ? f : f - 31);
        check(mapped && std::abs(*mapped - *timeline::sourceBoundary(original, f)) <= 1, "Split ramp retains original source mapping within one source tick");
    }
    editor.replaceProject(rampProject); check(editor.execute(timeline::TrimClip{sid, vtid, vid, 11, 52}).isEmpty(), "Ramp trim maps source endpoints before shifting key origin");
    const auto trimmed = editor.state().project.sequences[0].tracks[0].clips[0];
    for (qint64 f = 0; f <= trimmed.durationFrames; ++f) check(std::abs(*timeline::sourceBoundary(trimmed, f) - *timeline::sourceBoundary(original, f + 11)) <= 1, "Trimmed ramp preserves original content time");
    const auto trimProject = editor.state().project;
    auto invalid = speed; invalid.parameters["rate"] = 0; const auto beforeBad = editor.state();
    check(!editor.execute(timeline::SetClipEffects{sid, vtid, vid, {invalid}}).isEmpty() && editor.state() == beforeBad, "Invalid zero speed leaves model/history untouched");
    invalid = speed; invalid.id = project::newId(); check(!editor.execute(timeline::SetClipEffects{sid, vtid, vid, {speed, invalid}}).isEmpty(), "Multiple speed effects are rejected");
    auto saved = rampProject; saved.sequences[0].tracks[0].clips[0].effects.append(lut);
    check(project::ProjectStore::save(saved, out.filePath("embedded-lut.veproject")).isEmpty(), "Speed/LUT save validates and writes atomically");
    QFile::rename(lutPath, generated.filePath("swap temporarily moved.cube"));
    check(project::ProjectStore::load(out.filePath("embedded-lut.veproject")).project == std::optional<project::Project>(saved), "Reopen preserves ramp and embedded LUT even when LUT source path is missing");
    QFile::rename(generated.filePath("swap temporarily moved.cube"), lutPath);
    // Native UI edits use production dialog, undo and cancel paths.
    timeline::TimelineWidget widget; widget.setProject(p); widget.execute(timeline::SetSelection{{sid, {vtid}, {vid}}});
    dialogEdit([&](QWidget* w) { auto* type = w->findChild<QComboBox*>("effectType"); type->setCurrentIndex(type->findData("color")); w->findChild<QPushButton*>("effectAdd")->click(); w->findChild<QDoubleSpinBox*>("effect_exposure")->setValue(1); QCoreApplication::processEvents(); w->grab().save(out.filePath("color-dialog.png")); accept(w); }); widget.editClipEffects();
    check(widget.editor().state().project.sequences[0].tracks[0].clips[0].effects[0].parameters["exposure"] == 1, "Native color controls commit through undo system");
    widget.undo(); check(widget.editor().state().project == p, "Native color undo is exact");
    for (const auto& typeName : {QString("mask"), QString("chromaKey"), QString("lut")}) {
        dialogEdit([&](QWidget* w) {
            auto* type = w->findChild<QComboBox*>("effectType"); type->setCurrentIndex(type->findData(typeName)); w->findChild<QPushButton*>("effectAdd")->click();
            if (typeName == "mask") { w->findChild<QCheckBox*>("effect_invert")->setChecked(true); w->findChild<QDoubleSpinBox*>("effect_feather")->setValue(0.05); }
            if (typeName == "chromaKey") w->findChild<QLineEdit*>("effect_color")->setText("#ff0000ff");
            if (typeName == "lut") w->findChild<QLineEdit*>("effect_path")->setText(lutPath);
            QCoreApplication::processEvents(); w->grab().save(out.filePath(typeName + "-dialog.png")); accept(w);
        }); widget.editClipEffects();
        const auto e = widget.editor().state().project.sequences[0].tracks[0].clips[0].effects.last();
        check(e.type == typeName && project::validateEffect(e, 120).isEmpty(), "Native mask/key/LUT controls commit valid settings");
        if (typeName == "lut") check(e.parameters["size"] == 2, "Native LUT path imports embedded table");
        dialogEdit([](QWidget* w) { w->findChild<QPushButton*>("effectReset")->click(); accept(w); }); widget.editClipEffects();
        check(widget.editor().state().project.sequences[0].tracks[0].clips[0].effects[0] == project::defaultEffect(typeName, e.id), "Native reset restores new effect defaults");
        widget.setProject(p); widget.execute(timeline::SetSelection{{sid, {vtid}, {vid}}});
    }
    dialogEdit([&](QWidget* w) { auto* type = w->findChild<QComboBox*>("effectType"); type->setCurrentIndex(type->findData("speed")); w->findChild<QPushButton*>("effectAdd")->click(); w->findChild<QDoubleSpinBox*>("effect_rate")->setValue(2); accept(w); }); widget.editClipEffects();
    check(widget.editor().state().project.sequences[0].tracks[0].clips[0].durationFrames == 60, "Native speed control changes duration");
    dialogEdit([](QWidget* w) { w->findChild<QPushButton*>("effectReset")->click(); accept(w); }); widget.editClipEffects();
    check(widget.editor().state().project.sequences[0].tracks[0].clips[0].durationFrames == 120, "Speed Reset to defaults restores natural duration");
    dialogEdit([](QWidget* w) {
        auto* table = w->findChild<QTableWidget*>("effectKeyframes");
        w->findChild<QPushButton*>("keyframeAdd")->click(); qobject_cast<QDoubleSpinBox*>(table->cellWidget(0, 2))->setValue(0.5);
        w->findChild<QPushButton*>("keyframeAdd")->click(); qobject_cast<QLineEdit*>(table->cellWidget(1, 1))->setText("60"); qobject_cast<QDoubleSpinBox*>(table->cellWidget(1, 2))->setValue(2); accept(w);
    }); widget.editClipEffects(); check(widget.editor().state().project.sequences[0].tracks[0].clips[0].durationFrames == 83, "Native rate keyframe table applies speed ramp");
    dialogEdit([](QWidget* w) { w->findChild<QCheckBox*>("effectEnabled")->setChecked(false); accept(w); }); widget.editClipEffects();
    check(widget.editor().state().project.sequences[0].tracks[0].clips[0].durationFrames == 120, "Native speed bypass restores natural duration");
    widget.setProject(p); widget.execute(timeline::SetSelection{{sid, {atid}, {aid}}});
    dialogEdit([&](QWidget* w) { check(w->findChild<QComboBox*>("effectType")->count() == 1, "Audio effects picker contains only pitch-preserving speed"); w->findChild<QPushButton*>("effectAdd")->click(); w->findChild<QDoubleSpinBox*>("effect_rate")->setValue(0.5); QCoreApplication::processEvents(); w->grab().save(out.filePath("speed-dialog.png")); accept(w); }); widget.editClipEffects();
    check(widget.editor().state().project.sequences[0].tracks[1].clips[0].durationFrames == 240, "Native audio slow motion preserves source and grows sequence");
    const auto uiState = widget.editor().state(); dialogEdit([](QWidget* w) { w->findChild<QDoubleSpinBox*>("effect_rate")->setValue(8); qobject_cast<QDialog*>(w)->reject(); }); widget.editClipEffects(); check(widget.editor().state() == uiState, "Cancel speed edit preserves project and history");
    // Decode/export references use independent source frames and analytical source-time formulas.
    auto runExport = [&](const project::Project& project, const QString& name) {
        auto settings = project.exportSettings; settings.outputPath = out.filePath(name + ".mp4"); settings.audioEnabled = false;
        exporting::ExportWorker worker(playback::compileSequence(project), settings); worker.start(); const bool finished = waitFor([&] { return worker.isFinished(); }); worker.cancel(); worker.wait(); exports.append(worker.result());
        check(finished && worker.result()["passed"].toBool(), "Export " + name + ": " + worker.result()["error"].toString());
        const auto bytes = process(bin + "ffmpeg.exe", {"-v", "error", "-i", settings.outputPath, "-an", "-f", "rawvideo", "-pix_fmt", "bgra", "pipe:1"}, ok); check(ok, "Independent output decode " + name); return bytes;
    };
    const auto fastBytes = runExport(fastProject, "speed-2x");
    for (int f = 0; f < 60; ++f) {
        int index = 0; const auto outputUs = *project::framesToTicks(f, {30, 1}, {1, 1000000}, project::Rounding::Nearest);
        for (int i = 0; i < timestamps.size(); ++i) if (std::llround(timestamps[i].toObject()["best_effort_timestamp_time"].toString().toDouble() * 500000) <= outputUs) index = i;
        const auto difference = error(imageAt(fastBytes, f), imageAt(rawMotion, index)); worst = std::max(worst, difference); check(difference < 3, "Constant-speed export selects frame by independently probed source timestamp");
    }
    const auto rampBytes = runExport(rampProject, "speed-ramp");
    auto independentIntegral = [](double frame) { return frame <= 60 ? 0.5 * frame + 0.0125 * frame * frame : 75 + (frame - 60) * 2; };
    auto independentIndex = [&](int f) { return std::min(119, static_cast<int>(std::floor(independentIntegral(f) / independentIntegral(83) * 120 + 1e-6))); };
    for (int f = 0; f < 83; ++f) { const auto difference = error(imageAt(rampBytes, f), imageAt(rawMotion, independentIndex(f))); worst = std::max(worst, difference); check(difference < 3, "Ramped export selects analytically remapped source frames"); }
    auto fractional = fastProject; fractional.sequences[0].frameRate = {30000, 1001}; fractional.sequences[0].tracks[1].clips.clear(); fractional.sequences[0].durationFrames = 60; fractional.exportSettings.frameRate = {30000, 1001};
    const auto fractionalBytes = runExport(fractional, "fractional-speed"); check(fractionalBytes.size() == 60 * 320 * 180 * 4, "Fractional-rate speed export has exact output frame count");
    for (int f = 0; f < 60; ++f) {
        int index = 0; const auto timelineUs = *project::framesToTicks(f, {30000, 1001}, {1, 1000000}, project::Rounding::Nearest);
        for (int i = 0; i < timestamps.size(); ++i) if (std::llround(timestamps[i].toObject()["best_effort_timestamp_time"].toString().toDouble() * 500500) <= timelineUs) index = i;
        check(error(imageAt(fractionalBytes, f), imageAt(rawMotion, index)) < 3, "Fractional-rate speed retains independent timestamp selection");
    }
    const auto splitBytes = runExport(splitProject, "split-ramp"), trimBytes = runExport(trimProject, "trim-ramp");
    for (int f : {0, 1, 30, 31, 32, 60, 82}) check(error(imageAt(splitBytes, f), imageAt(rampBytes, f)) < 3, "Split ramp output keeps source-frame continuity");
    for (int f : {11, 12, 31, 32, 60, 62}) check(error(imageAt(trimBytes, f), imageAt(rampBytes, f)) < 3, "Trim ramp output keeps source-frame continuity");
    playback::PlaybackController controller; controller.setPreviewProfile({320, 180}); QImage shown;
    QObject::connect(&controller, &playback::PlaybackController::frameReady, [&](const QImage& image) { shown = image; }); controller.setDescription(playback::compileSequence(rampProject));
    for (int f : {0, 1, 15, 30, 31, 60, 82}) { const auto presented = controller.stats()["presented"].toInt(); controller.seek(*project::framesToTicks(f, {30, 1}, {1, 1000000}, project::Rounding::Nearest));
        check(waitFor([&] { return !controller.stats()["buffering"].toBool() && controller.stats()["presented"].toInt() > presented; }), "Production ramp preview seeks successfully");
        check(error(shown, imageAt(rawMotion, independentIndex(f))) < 0.2, "Production preview seeks same analytically remapped frame");
    }
    auto visual = p; auto& vs = visual.sequences[0]; vs.tracks[1].clips.clear(); vs.tracks[0].clips = {makeClip(blue.media, "video")};
    project::Track overlay; overlay.id = project::newId(); overlay.name = "Key/mask/color/LUT reference";
    for (int i = 0; i < 4; ++i) { auto cl = makeClip(green.media, "video", 30); cl.startFrame = i * 30; cl.effects = {project::defaultEffect("chromaKey")};
        if (i == 1) { auto mask = project::defaultEffect("mask"); mask.parameters["right"] = 0.5; cl.effects.append(mask); }
        if (i == 2) { auto color = project::defaultEffect("color"); color.keyframes["saturation"] = {{0, 0, "linear"}, {29, 1, "linear"}}; cl.effects.append(color); }
        if (i == 3) { auto copy = lut; copy.id = project::newId(); cl.effects.append(copy); } overlay.clips.append(cl);
    }
    vs.tracks.append(overlay); const auto graph = playback::compileSequence(visual); const auto visualBytes = runExport(visual, "color-mask-key-lut");
    for (int f = 0; f < 120; ++f) { QMap<QString, QImage> layers{{vs.tracks[0].clips[0].id, imageAt(rawBlue, 0)}}; layers[overlay.clips[f / 30].id] = imageAt(rawGreen, 0);
        const auto expected = playback::renderLayers(graph, layers, *project::framesToTicks(f, {30, 1}, {1, 1000000}, project::Rounding::Nearest), {320, 180}); const auto difference = error(imageAt(visualBytes, f), expected); worst = std::max(worst, difference); check(difference < 3, "Key/mask/animated color/embedded LUT export matches shared reference");
    }
    check(imageAt(visualBytes, 0).pixelColor(20, 20).blue() > 245 && imageAt(visualBytes, 0).pixelColor(160, 90).red() > 245 && imageAt(visualBytes, 30).pixelColor(180, 90).blue() > 245 && imageAt(visualBytes, 119).pixelColor(160, 90).blue() > 245, "Independent mask/key/LUT export endpoint colors reveal correct lower layer");
    controller.setDescription(graph);
    for (int f : {0, 30, 60, 74, 89, 90, 119}) { const auto presented = controller.stats()["presented"].toInt(); controller.seek(*project::framesToTicks(f, {30, 1}, {1, 1000000}, project::Rounding::Nearest));
        check(waitFor([&] { return !controller.stats()["buffering"].toBool() && controller.stats()["presented"].toInt() > presented; }) && error(shown, imageAt(visualBytes, f)) < 3, "Production preview matches independent color/mask/key/LUT export");
    }
    // Pitch and bounded streaming, including extremes, ramps, exact seek samples and AAC output.
    auto audioProject = p; audioProject.sequences[0].tracks[0].clips.clear();
    auto collectAudio = [&](const playback::RenderDescription& d, qint64 startUs = 0) {
        playback::AudioMixer mixer(d.audio, startUs, d.durationUs); mixer.start(); QVector<float> pcm;
        check(waitFor([&] { playback::AudioBlock block; while (mixer.takeAudio(block)) pcm += block.samples; return mixer.done(); }), "Pitch-preserving audio mix completes"); mixer.cancel(); mixer.wait();
        check(mixer.stats()["error"].toString().isEmpty() && mixer.stats()["queueHighWater"].toInt() <= playback::AudioMixer::QueueLimit, "Tempo audio has no error and bounded mixer queue"); return pcm;
    };
    for (double rate : {0.125, 0.5, 2.0, 8.0}) {
        auto audioSpeed = project::defaultEffect("speed"); audioSpeed.parameters["rate"] = rate; timeline::TimelineEditor audioEditor(audioProject);
        check(audioEditor.execute(timeline::SetClipEffects{sid, atid, aid, {audioSpeed}}).isEmpty(), "Apply pitch-preserving audio speed");
        auto ap = audioEditor.state().project; auto& as = ap.sequences[0]; as.durationFrames = as.tracks[1].clips[0].durationFrames;
        const auto d = playback::compileSequence(ap); const auto pcm = collectAudio(d); const auto begin = std::min<qint64>(12000, pcm.size() / 8);
        const auto hz = pitch(pcm, begin, std::min<qint64>(12000, pcm.size() / 2 - begin));
        check(std::abs(hz - 440) <= 2, "Audio tempo retains 440 Hz pitch at " + QString::number(rate) + "x");
        check(pcm.size() == *project::framesToSamples(as.durationFrames, {30, 1}, 48000, project::Rounding::Nearest) * 2, "Audio output uses exact remapped timeline sample count");
        measurements.append(QJsonObject{{"rate", rate}, {"pitchHz", hz}, {"samples", pcm.size() / 2}});
        if (rate == 2) {
            const auto seekPcm = collectAudio(d, 500000); check(seekPcm == pcm.sliced(48000), "Pitch-preserving audio seek is sample-identical to full-clip render");
            auto settings = ap.exportSettings; settings.outputPath = out.filePath("pitch-preserved.mp4"); settings.audioEnabled = true;
            exporting::ExportWorker worker(d, settings); worker.start(); check(waitFor([&] { return worker.isFinished(); }) && worker.result()["passed"].toBool(), "Pitch-preserving AAC export completes"); worker.cancel(); worker.wait(); exports.append(worker.result());
            const auto bytes = process(bin + "ffmpeg.exe", {"-v", "error", "-i", settings.outputPath, "-vn", "-f", "f32le", "-ac", "2", "-ar", "48000", "pipe:1"}, ok); QVector<float> decoded(bytes.size() / 4); std::memcpy(decoded.data(), bytes.constData(), static_cast<size_t>(bytes.size()));
            check(ok && std::abs(pitch(decoded, 12000, 12000) - 440) <= 2, "Independently decoded AAC retains source pitch");
            double diff = 0; for (int i = 24000; i < 72000 && i < decoded.size() && i < pcm.size(); ++i) diff += std::abs(decoded[i] - pcm[i]);
            check(diff / 48000 < 0.005, "AAC export audio matches production mix within compression tolerance");
        }
    }
    timeline::TimelineEditor audioRampEditor(audioProject); auto audioRamp = ramp; audioRamp.id = project::newId(); check(audioRampEditor.execute(timeline::SetClipEffects{sid, atid, aid, {audioRamp}}).isEmpty(), "Audio speed ramp applies");
    auto arp = audioRampEditor.state().project; arp.sequences[0].durationFrames = arp.sequences[0].tracks[1].clips[0].durationFrames;
    const auto rampPcm = collectAudio(playback::compileSequence(arp)); for (int begin : {12000, 48000, 90000}) { const auto hz = pitch(rampPcm, begin, 12000); measurements.append(QJsonObject{{"rampStartSample", begin}, {"pitchHz", hz}}); check(std::abs(hz - 440) <= 3, "Audio ramp preserves pitch across ramp progression"); }
    const auto rampSeekPcm = collectAudio(playback::compileSequence(arp), 500000); check(rampSeekPcm == rampPcm.sliced(48000), "Rubber Band ramp seek is sample-identical to full-clip render");
    auto rampAudioSettings = arp.exportSettings; rampAudioSettings.outputPath = out.filePath("pitch-preserved-ramp.mp4");
    exporting::ExportWorker audioRampExport(playback::compileSequence(arp), rampAudioSettings); audioRampExport.start();
    check(waitFor([&] { return audioRampExport.isFinished(); }) && audioRampExport.result()["passed"].toBool(), "Pitch-preserving ramp AAC export succeeds"); audioRampExport.cancel(); audioRampExport.wait(); exports.append(audioRampExport.result());
    const auto rampAacBytes = process(bin + "ffmpeg.exe", {"-v", "error", "-i", rampAudioSettings.outputPath, "-vn", "-f", "f32le", "-ac", "2", "-ar", "48000", "pipe:1"}, ok);
    QVector<float> rampAac(rampAacBytes.size() / 4); std::memcpy(rampAac.data(), rampAacBytes.constData(), static_cast<size_t>(rampAacBytes.size()));
    check(ok && std::abs(pitch(rampAac, 48000, 12000) - 440) <= 3, "Independent AAC ramp decode preserves pitch");
    double rampAacError = 0; for (int i = 24000; i < 200000 && i < rampAac.size() && i < rampPcm.size(); ++i) rampAacError += std::abs(rampAac[i] - rampPcm[i]);
    check(rampAacError / 176000 < 0.005, "Independent AAC ramp matches production mix within compression tolerance");
    playback::AudioMixer cancelRamp(playback::compileSequence(arp).audio, 2000000, 2766667); cancelRamp.start(); cancelRamp.cancel();
    check(cancelRamp.wait(2000), "Retimed audio late-seek cancellation releases filter/decode resources promptly");
    process(bin + "ffmpeg.exe", {"-v", "error", "-nostdin", "-y", "-f", "lavfi", "-i", "aevalsrc=sin(2*PI*440*t)*(between(t\\,0.925\\,1.075)+between(t\\,1.925\\,2.075)+between(t\\,2.925\\,3.075)):s=48000:d=4", "-c:a", "pcm_s16le", generated.filePath("pulse-timing.wav")}, ok);
    check(ok, "Create gated independent audio transient reference"); const auto pulses = media::inspect(generated.filePath("pulse-timing.wav")); check(pulses.error.isEmpty(), "Inspect gated reference");
    auto pulseProject = project::newProject("Audio ramp transient timing"); pulseProject.media = {pulses.media}; pulseProject.sequences[0].durationFrames = 120; pulseProject.sequences[0].tracks[1].clips = {makeClip(pulses.media, "audio")};
    timeline::TimelineEditor pulseEditor(pulseProject); auto pulseRamp = ramp; pulseRamp.id = project::newId();
    check(pulseEditor.execute(timeline::SetClipEffects{pulseProject.activeSequenceId, pulseProject.sequences[0].tracks[1].id, pulseProject.sequences[0].tracks[1].clips[0].id, {pulseRamp}}).isEmpty(), "Apply pitch-preserving transient ramp");
    pulseProject = pulseEditor.state().project; pulseProject.sequences[0].durationFrames = 83;
    const auto pulsePcm = collectAudio(playback::compileSequence(pulseProject)); double maximumTransientError = 0;
    write(out.filePath("pulse-ramp.f32le"), QByteArray(reinterpret_cast<const char*>(pulsePcm.constData()), pulsePcm.size() * sizeof(float)));
    for (double sourceSecond : {1.0, 2.0, 3.0}) {
        const auto targetIntegral = sourceSecond / 4 * independentIntegral(83);
        const double frame = targetIntegral <= 75 ? (-0.5 + std::sqrt(0.25 + 0.05 * targetIntegral)) / 0.025 : 60 + (targetIntegral - 75) / 2;
        const auto expectedSample = frame / 30 * 48000; double weight = 0, weightedTime = 0;
        const auto first = static_cast<qint64>(std::max(0.0, expectedSample - 12000)), last = static_cast<qint64>(std::min<double>(pulsePcm.size() / 2, expectedSample + 12000));
        for (qint64 sample = first; sample < last; ++sample) { const auto power = pulsePcm[sample * 2] * pulsePcm[sample * 2]; weight += power; weightedTime += power * sample; }
        const auto differenceMs = weight > 1 ? std::abs(weightedTime / weight - expectedSample) / 48 : 1000;
        measurements.append(QJsonObject{{"sourceTransientSecond", sourceSecond}, {"expectedSample", expectedSample}, {"actualSample", weight > 1 ? weightedTime / weight : -1}, {"energy", weight}, {"differenceMs", differenceMs}});
        maximumTransientError = std::max(maximumTransientError, differenceMs); check(differenceMs < 60, "Audio ramp transient follows source remap within 60 ms filter-window tolerance");
    }
    measurements.append(QJsonObject{{"maximumRampTransientErrorMs", maximumTransientError}});
    check(project::ProjectStore::save(visual, generated.filePath("color-mask-key-lut.veproject")).isEmpty() && project::ProjectStore::save(rampProject, generated.filePath("speed-ramp.veproject")).isEmpty() && project::ProjectStore::save(pulseProject, generated.filePath("audio-ramp.veproject")).isEmpty(), "Prepared reference projects save for optional visible review");
    const QJsonObject report{{"passed", failures.isEmpty()}, {"checks", checks}, {"failures", failures}, {"exports", exports}, {"audioMeasurements", measurements}, {"worstExportMeanRgbError", worst}, {"evidence", "Native model, analytical SDR/time references, production Qt offscreen controls/preview, bounded streaming audio with spectral pitch and seek equivalence, independently decoded MP4/AAC. No computer use or human visual acceptance."}};
    write(out.filePath("advanced-effects-result.json"), QJsonDocument(report).toJson()); std::printf("Session 12: %d checks, %lld failures\n", checks, static_cast<long long>(failures.size())); return failures.isEmpty() ? 0 : 1;
}


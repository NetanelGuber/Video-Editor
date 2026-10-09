#include "timeline/TimelineWidget.h"
#include "timeline/PropertyDialogs.h"
#include "project/Effects.h"
#include "project/ProjectStore.h"
#include "playback/PlaybackController.h"
#include "playback/VisualEffects.h"
#include "export/ExportWorker.h"
#include "media/Inspection.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
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
#include <limits>

using namespace editor;
namespace {
bool waitFor(const std::function<bool()>& predicate, int timeout = 60000) {
    QElapsedTimer time; time.start();
    while (time.elapsed() < timeout) { if (predicate()) return true; QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1); }
    return predicate();
}
QByteArray read(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
bool write(const QString& path, const QByteArray& bytes) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(); }
QByteArray process(const QString& exe, const QStringList& args, bool& ok) {
    QProcess p; p.start(exe, args); ok = p.waitForStarted() && p.waitForFinished(60000) && p.exitCode() == 0;
    if (!ok) { std::fprintf(stderr, "%s\n", p.readAllStandardError().constData()); p.kill(); p.waitForFinished(); }
    return p.readAllStandardOutput();
}
project::Clip clip(const project::Media& m, qint64 start, qint64 duration) {
    project::Clip c; c.id = project::newId(); c.name = m.name; c.mediaId = m.id; c.startFrame = start; c.durationFrames = duration;
    for (const auto& s : m.streams) if (s.kind == "video") { c.streamIndex = s.index; c.sourceDurationTicks = *project::framesToTicks(duration, {30, 1}, s.timeBase, project::Rounding::Nearest); break; }
    return c;
}
double error(const QImage& a, const QImage& b) {
    if (a.size() != b.size() || a.isNull()) return 1000;
    double sum = 0;
    for (int y = 0; y < a.height(); ++y) for (int x = 0; x < a.width(); ++x) {
        const auto p = a.pixelColor(x, y), q = b.pixelColor(x, y);
        sum += std::abs(p.red() - q.red()) + std::abs(p.green() - q.green()) + std::abs(p.blue() - q.blue());
    }
    return sum / (a.width() * a.height() * 3);
}
QImage imageAt(const QByteArray& bytes, qint64 n, QSize size = {320, 180}) {
    const auto stride = size.width() * size.height() * 4;
    const auto offset = n * stride;
    if (offset < 0 || offset + stride > bytes.size()) return {};
    return QImage(reinterpret_cast<const uchar*>(bytes.constData() + offset), size.width(), size.height(), size.width() * 4, QImage::Format_RGB32).copy();
}
void editDialog(const std::function<void(QWidget*)>& edit) {
    QTimer::singleShot(0, [edit] {
        for (auto* w : QApplication::topLevelWidgets()) if (w->objectName() == "effectProperties") { edit(w); return; }
    });
}
void accept(QWidget* w) { w->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click(); }
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    if (app.arguments().size() != 3) return 2;
    QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/segoeui.ttf"));
    QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/arial.ttf"));
    QDir root(app.arguments()[1]), out(app.arguments()[2]); QDir().mkpath(out.absolutePath());
    QJsonArray failures, exports, comparisons; int checks = 0;
    auto check = [&](bool ok, const QString& name) { ++checks; if (!ok) { failures.append(name); std::fprintf(stderr, "FAIL: %s\n", qPrintable(name)); } };
    const auto lock = QJsonDocument::fromJson(read(root.filePath("dependencies.lock.json"))).object();
    const auto bin = root.filePath(lock["packages"].toArray()[1].toObject()["prefix"].toString() + "/bin/");
    const auto generated = root.filePath("fixtures/generated/session-11"); QDir().mkpath(generated);
    bool ok = false;
    for (const auto& color : {QString("red"), QString("blue")}) {
        process(bin + "ffmpeg.exe", {"-v", "error", "-nostdin", "-y", "-f", "lavfi", "-i", "color=c=" + color + ":s=320x180:r=30:d=12", "-c:v", "libx264", "-crf", "0", "-pix_fmt", "yuv420p", QDir(generated).filePath(color + ".mp4")}, ok);
        check(ok, "Create independent solid " + color + " source");
    }
    const auto red = media::inspect(QDir(generated).filePath("red.mp4")), blue = media::inspect(QDir(generated).filePath("blue.mp4"));
    check(red.error.isEmpty() && blue.error.isEmpty(), "Inspect synthetic source streams");
    if (!failures.isEmpty()) return 1;
    auto p = project::newProject("Session 11 — effects and fades reference"); p.media = {blue.media, red.media};
    auto& seq = p.sequences[0]; seq.width = 320; seq.height = 180; seq.durationFrames = 360;
    seq.tracks[0].clips = {clip(blue.media, 0, 300)};
    project::Track overlay; overlay.id = project::newId(); overlay.name = "Effects reference";
    for (int n = 0; n < 12; ++n) {
        auto c = clip(red.media, n * 30, 30); c.name = QString("Reference %1").arg(n + 1);
        auto fade = project::defaultEffect("videoFade"); fade.parameters["inFrames"] = "10"; fade.parameters["outFrames"] = "10";
        if (n == 1 || n == 2 || n == 3 || n == 4 || n == 11) {
            fade.parameters["inMode"] = "color"; fade.parameters["outMode"] = "color";
            fade.parameters["inColor"] = n == 2 ? "#ffffffff" : n == 3 ? "#ff12a4c8" : "#ff000000";
            fade.parameters["outColor"] = n == 1 ? "#ffffffff" : n == 2 ? "#ff000000" : "#ff8030d0";
        }
        if (n == 4 || n == 5) { fade.parameters["inFrames"] = "30"; fade.parameters["outFrames"] = "30"; }
        if (n == 3 || n == 6) { fade.parameters["inCurve"] = "eased"; fade.parameters["outFrames"] = "6"; }
        if (n == 11) { fade.parameters["inCurve"] = "hold"; fade.parameters["outCurve"] = "eased"; }
        c.effects = {fade};
        if (n == 6) {
            auto transform = project::defaultEffect("transform"); transform.parameters["scaleX"] = 0.6; transform.parameters["scaleY"] = 0.6;
            transform.keyframes["x"] = {{0, -0.25, "eased"}, {29, 0.25, "linear"}};
            auto crop = project::defaultEffect("crop"); crop.parameters["left"] = 0.2; c.effects = {crop, transform, fade};
        }
        if (n == 7 || n == 8) {
            auto mode = project::defaultEffect("composite"); mode.parameters["mode"] = n == 7 ? "multiply" : "screen"; c.effects = {mode};
        }
        if (n == 9) {
            auto opacity = project::defaultEffect("opacity"); opacity.keyframes["value"] = {{0, 0.0, "hold"}, {10, 0.5, "linear"}, {29, 1, "linear"}}; c.effects = {opacity};
        }
        overlay.clips.append(c);
    }
    seq.tracks.append(overlay);
    p.exportSettings.width = 320; p.exportSettings.height = 180;
    const auto sid = seq.id, tid = overlay.id, cid = overlay.clips[0].id;
    check(project::validate(p).isEmpty(), "Reference graph validates with independent in/out colors, curves and durations");
    const auto projectPath = QDir(generated).filePath("effects-reference.veproject");
    check(project::ProjectStore::save(p, projectPath).isEmpty(), "Ready-to-open reference project saved atomically");
    check(project::ProjectStore::load(projectPath).project == std::optional<project::Project>(p), "Effect order, identities, bypass/defaults, origins and keyframes survive reopen");
    auto repeated = p; for (int n = 0; n < 50; ++n) repeated = *project::deserialize(project::serialize(repeated)).project;
    check(repeated == p, "Fifty schema-5 cycles retain exact animated effects");
    const auto currentFixture = project::deserialize(read(root.filePath("fixtures/projects/effects-v4.veproject")));
    check(currentFixture && currentFixture.migratedFrom == 4 && currentFixture.project->sequences[0].tracks[2].clips[6].effects[1].keyframes.size() > 0 &&
        currentFixture.project->sequences[0].audioBuses.isEmpty() && project::deserialize(project::serialize(*currentFixture.project)).migratedFrom == 0,
        "Checked-in schema-4 effect fixture migrates to schema 5 with default audio routing");
    for (int version : {1, 2, 3}) {
        const auto migrated = project::deserialize(read(root.filePath(QString("fixtures/projects/multitrack-v%1.veproject").arg(version))));
        check(migrated && migrated.migratedFrom == version && project::deserialize(project::serialize(*migrated.project)).migratedFrom == 0, QString("Schema %1 explicitly migrates to 5").arg(version));
    }
    auto animation = project::defaultEffect("opacity");
    animation.keyframes["value"] = {{0, 0, "hold"}, {10, 0.5, "linear"}, {20, 1, "eased"}, {30, 0, "linear"}};
    check(project::effectValue(animation, "value", 0) == 0 && project::effectValue(animation, "value", 9) == 0 && project::effectValue(animation, "value", 10) == 0.5, "Hold jumps at the next exact frame boundary");
    check(project::effectValue(animation, "value", 15) == 0.75 && project::effectValue(animation, "value", 20) == 1, "Linear interpolates exact frame fractions");
    check(std::abs(project::effectValue(animation, "value", 22) - 0.896) < 1e-12 && project::effectValue(animation, "value", 30) == 0 && project::effectValue(animation, "value", 100) == 0, "Eased smoothstep and post-last holding are deterministic");
    animation.timeOffsetFrames = -10; check(project::effectValue(animation, "value", 0) == 0, "Negative trim extension holds first key"); animation.timeOffsetFrames = 0;
    const qint64 huge = 9007199254740993LL;
    animation.keyframes["value"] = {{huge, 0, "linear"}, {huge + 2, 1, "linear"}};
    check(project::effectValue(animation, "value", huge + 1) == 0.5, "Frame positions above 2^53 use exact integer differences");
    for (const auto rate : {project::Rational{30, 1}, project::Rational{30000, 1001}, project::Rational{24000, 1001}, project::Rational{144, 1}}) {
        auto d = playback::compileSequence(p); d.frameRate = rate;
        for (qint64 frame = 0; frame < 100; ++frame) {
            const auto us = *project::framesToTicks(frame, rate, {1, 1000000}, project::Rounding::Nearest);
            check(playback::sequenceFrameAt(d, us) == frame && (frame == 0 || playback::sequenceFrameAt(d, us - 1) == frame - 1), "Fractional sequence boundary maps exactly to authoritative frame");
        }
    }
    auto edited = p; auto& c = edited.sequences[0].tracks[2].clips[0];
    auto opacity = project::defaultEffect("opacity"); opacity.keyframes["value"] = {{0, 0, "linear"}, {29, 1, "linear"}}; c.effects.prepend(opacity);
    timeline::TimelineEditor editor(edited); const auto before = editor.state();
    QVector<project::Effect> stack = c.effects; std::reverse(stack.begin(), stack.end()); stack[0].enabled = false;
    check(editor.execute(timeline::SetClipEffects{sid, tid, cid, stack}).isEmpty() && editor.state().project != edited, "Order/bypass changes route through transactional command");
    const auto after = editor.state(); check(editor.undo() && editor.state() == before && editor.redo() && editor.state() == after, "Effect edits undo/redo exact model and selection"); editor.undo();
    auto reject = [&](QVector<project::Effect> effects, const QString& name) { const auto state = editor.state(); const auto count = editor.undoCount(); check(!editor.execute(timeline::SetClipEffects{sid, tid, cid, effects}).isEmpty() && editor.state() == state && editor.undoCount() == count, name); };
    auto invalid = c.effects; invalid[0].keyframes["value"] = {{0, 0, "linear"}, {0, 1, "linear"}}; reject(invalid, "Duplicate keys rejected without partial edit/history");
    invalid = c.effects; invalid[0].keyframes["value"][0].value = 2; reject(invalid, "Out-of-range key rejected transactionally");
    invalid = c.effects; invalid[0].keyframes["value"][0].curve = "unknown"; reject(invalid, "Unknown curve rejected");
    invalid = c.effects; invalid[0].timeOffsetFrames = std::numeric_limits<qint64>::max(); reject(invalid, "Effect clock overflow rejected");
    invalid = c.effects; invalid.last().parameters["inFrames"] = "31"; reject(invalid, "Fade cannot exceed clip duration");
    invalid = c.effects; invalid.last().parameters["inColor"] = "#80000000"; reject(invalid, "Fade solid color cannot be translucent");
    invalid = c.effects; invalid.last().parameters["inFrames"] = 10; reject(invalid, "Fade frame count cannot be a lossy JSON number");
    invalid = c.effects; invalid[0].parameters["value"] = -1; reject(invalid, "Invalid constant parameter rejected");
    const auto keys = c.effects[0].keyframes;
    check(editor.execute(timeline::TrimClip{sid, tid, cid, 5, 20}).isEmpty(), "Trim animated clip");
    const auto trimmed = editor.state().project.sequences[0].tracks[2].clips[0];
    check(trimmed.effects[0].timeOffsetFrames == 5 && trimmed.effects[0].keyframes == keys && project::effectValue(trimmed.effects[0], "value", 0) == 5.0 / 29, "Trim retains content-relative keys and advances origin");
    check(editor.undo() && editor.state() == before, "Animated trim undoes exactly");
    check(editor.execute(timeline::TrimClip{sid, tid, cid, 0, 4}).isEmpty() && editor.state().project.sequences[0].tracks[2].clips[0].effects.last().parameters["inFrames"] == "4" && editor.state().project.sequences[0].tracks[2].clips[0].effects.last().parameters["outFrames"] == "4", "Short trims independently clamp both fade durations to the new clip");
    editor.undo();
    const auto rightId = project::newId(); QStringList effectIds; for (const auto& e : c.effects) { Q_UNUSED(e); effectIds.append(project::newId()); }
    check(editor.execute(timeline::SplitClip{sid, tid, cid, 15, rightId, effectIds}).isEmpty(), "Split animated clip with new effect identities");
    const auto halves = editor.state().project.sequences[0].tracks[2].clips;
    check(halves[1].effects[0].timeOffsetFrames == 15 && halves[1].effects[0].keyframes == keys && project::effectValue(halves[1].effects[0], "value", 0) == 15.0 / 29, "Split right half preserves animation continuity");
    check(halves[0].effects.last().parameters["outFrames"] == "0" && halves[1].effects.last().parameters["inFrames"] == "0" && halves[0].effects.last().parameters["inFrames"] == "10" && halves[1].effects.last().parameters["outFrames"] == "10", "Split retains only original outer-edge fades");
    auto splitProject = editor.state().project;
    check(editor.undo() && editor.state() == before, "Split keys, origins, fade edges and IDs undo exactly");
    check(editor.execute(timeline::MoveClip{sid, tid, cid, tid, 5}).isEmpty() && editor.state().project.sequences[0].tracks[2].clips[0].effects == c.effects, "Move keeps keyframes and local fade endpoints");
    check(editor.undo() && editor.state() == before, "Move effects undo exactly");
    check(project::ProjectStore::save(splitProject, out.filePath("split-reopen.veproject")).isEmpty() && project::ProjectStore::load(out.filePath("split-reopen.veproject")).project == std::optional<project::Project>(splitProject), "Split origins, outer-edge fades and new effect identities survive reopen");
    check(editor.execute(timeline::SetTrackLocked{sid, tid, true}).isEmpty(), "Lock effect track"); reject(c.effects, "Effect changes on locked track rejected"); editor.undo();
    // Analytical raster references, independent of decoder/export compression.
    QImage redImage(320, 180, QImage::Format_RGB32); redImage.fill(Qt::red);
    QImage blueImage(320, 180, QImage::Format_RGB32); blueImage.fill(Qt::blue);
    const auto graph = playback::compileSequence(p);
    auto layersAt = [&](const playback::RenderDescription& d, qint64 frame, const QImage& r, const QImage& b) {
        const auto us = *project::framesToTicks(frame, d.frameRate, {1, 1000000}, project::Rounding::Nearest); QMap<QString, QImage> layers;
        for (const auto& source : playback::activeVideoSources(d, us)) layers[source.clipId] = source.path == red.media.path ? r : b;
        return playback::renderLayers(d, layers, us, {320, 180});
    };
    auto pixel = [&](qint64 f) { return layersAt(graph, f, redImage, blueImage).pixelColor(160, 90); };
    check(pixel(0) == QColor(Qt::blue) && pixel(9) == QColor(Qt::red) && pixel(29) == QColor(Qt::blue), "Opacity fade endpoints reveal underlying track");
    const auto mid = pixel(4); check(std::abs(mid.red() - 113) <= 2 && mid.green() == 0 && std::abs(mid.blue() - 142) <= 2, "Linear fade center matches analytical independent RGB weights");
    check(pixel(30) == QColor(Qt::black) && pixel(59) == QColor(Qt::white) && pixel(60) == QColor(Qt::white) && pixel(89) == QColor(Qt::black), "Independent black/white in/out endpoint colors");
    check(pixel(90) == QColor("#12a4c8") && pixel(119) == QColor("#8030d0"), "Custom color endpoints are exact");
    check(pixel(120) == QColor(Qt::black) && pixel(149) == QColor("#8030d0"), "Overlapping color fades retain in/out endpoints in application order");
    check(pixel(150) == QColor(Qt::blue) && pixel(179) == QColor(Qt::blue) && pixel(164).red() >= 62 && pixel(164).red() <= 65, "Overlapping opacity fades multiply and reveal blue");
    check(pixel(210) == QColor(Qt::black) && pixel(240) == QColor(Qt::magenta), "Multiply/screen composition matches independent color algebra");
    check(pixel(270) == QColor(Qt::blue) && pixel(279) == QColor(Qt::blue) && std::abs(pixel(280).red() - 127) <= 1 && pixel(299) == QColor(Qt::red), "Animated hold/linear opacity renders on exact frame boundaries");
    check(pixel(300) == QColor(Qt::black) && pixel(329) == QColor(Qt::black), "Opacity fades without lower video reveal black");
    check(pixel(330) == QColor(Qt::black) && pixel(338) == QColor(Qt::black) && pixel(339) == QColor(Qt::red) && pixel(359) == QColor("#8030d0"), "Hold and eased fade endpoints");
    auto one = p; auto& oneClip = one.sequences[0].tracks[2].clips[0]; oneClip.effects[0].parameters["inFrames"] = "1"; oneClip.effects[0].parameters["outFrames"] = "1";
    const auto oneGraph = playback::compileSequence(one);
    check(layersAt(oneGraph, 0, redImage, blueImage).pixelColor(160, 90) == QColor(Qt::blue) && layersAt(oneGraph, 1, redImage, blueImage).pixelColor(160, 90) == QColor(Qt::red) && layersAt(oneGraph, 29, redImage, blueImage).pixelColor(160, 90) == QColor(Qt::blue), "One-frame fades affect only the endpoint frame");
    oneClip.effects[0].parameters["inFrames"] = "0"; oneClip.effects[0].parameters["outFrames"] = "10"; oneClip.effects[0].parameters["outCurve"] = "hold";
    check(layersAt(playback::compileSequence(one), 28, redImage, blueImage).pixelColor(160, 90) == QColor(Qt::red) && layersAt(playback::compileSequence(one), 29, redImage, blueImage).pixelColor(160, 90) == QColor(Qt::blue), "Hold fade-out keeps the clip until its final endpoint frame");
    auto overlapping = p; auto& overlapFade = overlapping.sequences[0].tracks[2].clips[4].effects[0];
    overlapFade.parameters["inColor"] = "#ff0000ff"; overlapFade.parameters["outColor"] = "#ff00ff00";
    const auto mixed = layersAt(playback::compileSequence(overlapping), 134, redImage, blueImage).pixelColor(160, 90);
    check(std::abs(mixed.red() - 64) <= 2 && std::abs(mixed.green() - 123) <= 2 && std::abs(mixed.blue() - 68) <= 2, "Overlapping selected colors blend in then out with independent analytical weights");
    for (auto& e : oneClip.effects) e.enabled = false;
    check(layersAt(playback::compileSequence(one), 0, redImage, blueImage).pixelColor(160, 90) == QColor(Qt::red), "Bypass retains settings and skips evaluation");
    QImage canvas(320, 180, QImage::Format_ARGB32_Premultiplied); canvas.fill(Qt::red);
    auto cropEffect = project::defaultEffect("crop"); cropEffect.parameters["left"] = 0.5;
    auto transformEffect = project::defaultEffect("transform"); transformEffect.parameters["x"] = -0.25;
    auto basic = oneClip; basic.effects = {cropEffect, transformEffect}; auto a = playback::applyEffects(canvas, basic, 0).image;
    basic.effects = {transformEffect, cropEffect}; auto b = playback::applyEffects(canvas, basic, 0).image;
    check(a.pixelColor(100, 90).alpha() == 255 && b.pixelColor(100, 90).alpha() == 0 && a != b, "Effect stack order changes crop/transform pixels");
    basic.effects = {project::defaultEffect("transform")}; basic.effects[0].parameters["scaleX"] = 0.5; basic.effects[0].parameters["scaleY"] = 0.5;
    a = playback::applyEffects(canvas, basic, 0).image;
    check(a.pixelColor(0, 0).alpha() == 0 && a.pixelColor(160, 90) == QColor(Qt::red), "Transform scales about canvas center leaving transparent surroundings");
    basic.effects[0].parameters["rotation"] = 90; a = playback::applyEffects(canvas, basic, 0).image;
    check(a.pixelColor(160, 20).alpha() == 255 && a.pixelColor(90, 90).alpha() == 0, "Rotation maps centered rectangle independently");
    // Production native dialog operations through Qt offscreen events, no computer use.
    timeline::TimelineWidget widget; widget.setProject(edited); widget.execute(timeline::SetSelection{{sid, {tid}, {cid}}});
    const auto uiBefore = widget.editor().state();
    editDialog([&](QWidget* w) {
        w->findChild<QListWidget*>("effectStack")->setCurrentRow(1);
        w->findChild<QLineEdit*>("effect_inFrames")->setText("7"); w->findChild<QLineEdit*>("effect_outFrames")->setText("13");
        w->findChild<QComboBox*>("effect_inMode")->setCurrentText("color"); w->findChild<QLineEdit*>("effect_inColor")->setText("#ffffffff");
        w->findChild<QComboBox*>("effect_outMode")->setCurrentText("color"); w->findChild<QLineEdit*>("effect_outColor")->setText("#ff1280a0");
        w->findChild<QComboBox*>("effect_outCurve")->setCurrentText("eased");
        QCoreApplication::processEvents(); w->grab().save(out.filePath("effects-dialog.png")); accept(w);
    });
    widget.findChild<QAction*>("timelineClipEffects")->trigger();
    const auto uiAfter = widget.editor().state(); const auto uiFade = uiAfter.project.sequences[0].tracks[2].clips[0].effects[1];
    check(uiFade.parameters["inFrames"] == "7" && uiFade.parameters["outFrames"] == "13" && uiFade.parameters["inColor"] == "#ffffffff" && uiFade.parameters["outColor"] == "#ff1280a0" && uiFade.parameters["outCurve"] == "eased", "Production effects action edits independent fade values");
    widget.undo(); check(widget.editor().state() == uiBefore, "Dialog changes are one exact undo entry"); widget.redo(); check(widget.editor().state() == uiAfter, "Dialog changes redo exactly");
    // Avoid retaining widget references across the modal editor's recreated property panel.
    editDialog([](QWidget* w) { auto* list = w->findChild<QListWidget*>("effectStack"); list->setCurrentRow(1); w->findChild<QLineEdit*>("effect_inFrames")->setText("3"); qobject_cast<QDialog*>(w)->reject(); });
    widget.editClipEffects();
    check(widget.editor().state() == uiAfter, "Cancel does not change model or history");
    widget.setPlayhead(5);
    editDialog([&](QWidget* w) {
        auto* table = w->findChild<QTableWidget*>("effectKeyframes"); check(table && table->rowCount() == 2, "Existing keys populate editable table");
        w->findChild<QPushButton*>("keyframeAdd")->click();
        table = w->findChild<QTableWidget*>("effectKeyframes"); qobject_cast<QDoubleSpinBox*>(table->cellWidget(2, 2))->setValue(0.8); qobject_cast<QComboBox*>(table->cellWidget(2, 3))->setCurrentText("eased");
        accept(w);
    });
    widget.editClipEffects();
    auto uiKeys = widget.editor().state().project.sequences[0].tracks[2].clips[0].effects[0].keyframes["value"];
    check(uiKeys.size() == 3 && uiKeys[1].frame == 5 && uiKeys[1].value == 0.8 && uiKeys[1].curve == "eased", "Native keyframe insertion sorts frames and edits interpolation");
    const auto keyState = widget.editor().state(); widget.undo(); check(widget.editor().state() == uiAfter, "Keyframe edit undoes exactly"); widget.redo(); check(widget.editor().state() == keyState, "Keyframe edit redoes exactly");
    editDialog([&](QWidget* w) {
        w->findChild<QListWidget*>("effectStack")->setCurrentRow(1); w->findChild<QLineEdit*>("effect_inFrames")->setText("100"); accept(w);
        check(!w->findChild<QLabel*>("effectError")->text().isEmpty() && w->isVisible(), "Dialog rejects invalid duration and stays editable");
        qobject_cast<QDialog*>(w)->reject();
    }); widget.editClipEffects(); check(widget.editor().state() == keyState, "Invalid dialog values never enter history");
    editDialog([](QWidget* w) { w->findChild<QPushButton*>("effectDown")->click(); w->findChild<QCheckBox*>("effectEnabled")->setChecked(false); accept(w); }); widget.editClipEffects();
    const auto reordered = widget.editor().state().project.sequences[0].tracks[2].clips[0].effects;
    check(reordered[0].type == "videoFade" && reordered[1].type == "opacity" && !reordered[1].enabled, "Native reorder and bypass preserve effect identities"); widget.undo();
    editDialog([](QWidget* w) { w->findChild<QPushButton*>("effectReset")->click(); accept(w); }); widget.editClipEffects();
    const auto resetEffect = widget.editor().state().project.sequences[0].tracks[2].clips[0].effects[0];
    check(resetEffect.id == opacity.id && resetEffect.keyframes.isEmpty() && resetEffect.parameters["value"] == 1.0, "Reset clears animation and restores defaults with stable identity"); widget.undo();
    editDialog([](QWidget* w) { w->findChild<QComboBox*>("effectType")->setCurrentIndex(0); w->findChild<QPushButton*>("effectAdd")->click(); accept(w); }); widget.editClipEffects();
    check(widget.editor().state().project.sequences[0].tracks[2].clips[0].effects.size() == 3, "Native add creates reusable default effect");
    editDialog([](QWidget* w) { w->findChild<QListWidget*>("effectStack")->setCurrentRow(2); w->findChild<QPushButton*>("effectRemove")->click(); accept(w); }); widget.editClipEffects();
    check(widget.editor().state() == keyState, "Native removal restores previous stack exactly");
    editDialog([&](QWidget* w) {
        auto* table = w->findChild<QTableWidget*>("effectKeyframes"); table->setCurrentCell(1, 0); w->findChild<QPushButton*>("keyframeRemove")->click(); accept(w);
    }); widget.editClipEffects();
    check(widget.editor().state().project.sequences[0].tracks[2].clips[0].effects[0].keyframes["value"].size() == 2, "Native keyframe deletion persists through undo system"); widget.undo();
    check(project::ProjectStore::save(widget.editor().state().project, out.filePath("ui-reopen.veproject")).isEmpty() && project::ProjectStore::load(out.filePath("ui-reopen.veproject")).project == std::optional<project::Project>(widget.editor().state().project), "Native keyframes and independent fade controls persist after reopen");
    // Independently decode source pixels; compare encoded output against shared render plus analytical endpoints.
    const auto redBytes = process(bin + "ffmpeg.exe", {"-v", "error", "-i", red.media.path, "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "bgra", "pipe:1"}, ok); check(ok, "Independently decode red source");
    const auto blueBytes = process(bin + "ffmpeg.exe", {"-v", "error", "-i", blue.media.path, "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "bgra", "pipe:1"}, ok); check(ok, "Independently decode blue source");
    const auto decodedRed = imageAt(redBytes, 0), decodedBlue = imageAt(blueBytes, 0);
    auto settings = p.exportSettings; settings.outputPath = out.filePath("effects-reference.mp4"); settings.audioEnabled = false; settings.preset = "veryfast"; settings.quality = 10;
    auto runExport = [&](const playback::RenderDescription& d, const project::ExportSettings& s) {
        exporting::ExportWorker worker(d, s); worker.start(); const auto complete = waitFor([&] { return worker.isFinished(); }); worker.cancel(); worker.wait();
        exports.append(worker.result()); check(complete && worker.result()["passed"].toBool(), "Effect export completes: " + worker.result()["error"].toString());
        return process(bin + "ffmpeg.exe", {"-v", "error", "-i", s.outputPath, "-f", "rawvideo", "-pix_fmt", "bgra", "pipe:1"}, ok);
    };
    const auto pixels = runExport(graph, settings); check(ok && pixels.size() == 360 * 320 * 180 * 4, "Independent decoder finds exactly 360 effect frames");
    double worst = 0;
    for (int f = 0; f < 360; ++f) {
        const auto expected = layersAt(graph, f, decodedRed, decodedBlue), actual = imageAt(pixels, f);
        const auto difference = error(expected, actual); worst = std::max(worst, difference);
        check(difference < 3, QString("Export frame %1 matches effects/underlying-layer reference (<3 RGB mean)").arg(f));
        if (f % 30 == 0 || f % 30 == 29) comparisons.append(QJsonObject{{"frame", f}, {"meanRgbError", difference}});
    }
    auto probe = process(bin + "ffprobe.exe", {"-v", "error", "-count_frames", "-show_streams", "-show_format", "-of", "json", settings.outputPath}, ok); write(out.filePath("ffprobe-effects.json"), probe);
    const auto metadata = QJsonDocument::fromJson(probe).object(); check(ok && metadata["streams"].toArray()[0].toObject()["nb_read_frames"] == "360" && std::abs(metadata["format"].toObject()["duration"].toString().toDouble() - 12) < 0.002, "Independent MP4 duration/frame count are exact");
    for (int f : {30, 59, 60, 89, 90, 119, 149, 300, 329, 359}) {
        const auto expected = pixel(f), actual = imageAt(pixels, f).pixelColor(160, 90);
        check(std::abs(expected.red() - actual.red()) < 5 && std::abs(expected.green() - actual.green()) < 5 && std::abs(expected.blue() - actual.blue()) < 5, "Independent compressed endpoint appearance agrees with analytical solid colors");
    }
    playback::PlaybackController controller; QImage displayed;
    controller.setPreviewProfile({320, 180});
    QObject::connect(&controller, &playback::PlaybackController::frameReady, [&](const QImage& image) { displayed = image; });
    controller.setDescription(graph);
    for (int f : {0, 4, 29, 30, 59, 60, 90, 120, 164, 180, 189, 205, 210, 240, 279, 280, 299, 300, 329, 338, 339, 359}) {
        const auto presented = controller.stats()["presented"].toInt();
        const auto us = *project::framesToTicks(f, graph.frameRate, {1, 1000000}, project::Rounding::Nearest); controller.seek(us);
        check(waitFor([&] { return !controller.stats()["buffering"].toBool() && controller.stats()["presented"].toInt() > presented; }), "Production preview seeks all active layers");
        check(error(displayed, layersAt(graph, f, decodedRed, decodedBlue)) < 0.1, QString("Production preview exact reference pixels at frame %1").arg(f));
        check(error(displayed, imageAt(pixels, f)) < 3, "Production preview agrees with independently decoded export");
    }
    controller.setPreviewProfile({1920, 1080}, true); controller.seek(4000000);
    check(waitFor([&] { return !controller.stats()["buffering"].toBool() && controller.stats()["videoLayers"].toArray().size() == 2; }), "Low-memory multilayer preview loads");
    qint64 bytes = 0; for (const auto& layer : controller.stats()["videoLayers"].toArray()) bytes += layer.toObject()["queueBytesHighWater"].toInteger();
    check(bytes <= 4 * 1024 * 1024, "Multilayer low-memory converted queues obey aggregate budget");
    for (int n = 0; n < 60; ++n) controller.seek((n * 191919) % graph.durationUs);
    check(waitFor([&] { return !controller.stats()["buffering"].toBool(); }), "Rapid multilayer scrub cancellation coalesces latest request");
    // Split and trimmed/moved animation must reach the same shared evaluation in export.
    auto boundarySettings = settings; boundarySettings.outputPath = out.filePath("split.mp4");
    const auto splitGraph = playback::compileSequence(splitProject); const auto splitPixels = runExport(splitGraph, boundarySettings);
    for (int f : {0, 9, 14, 15, 16, 20, 29}) check(error(imageAt(splitPixels, f), layersAt(splitGraph, f, decodedRed, decodedBlue)) < 3, "Split origin/outer fades match independent export at edit boundary");
    editor.execute(timeline::TrimClip{sid, tid, cid, 5, 20}); editor.execute(timeline::MoveClip{sid, tid, cid, tid, 7});
    const auto trimGraph = playback::compileSequence(editor.state().project); boundarySettings.outputPath = out.filePath("trim-move.mp4"); const auto trimPixels = runExport(trimGraph, boundarySettings);
    for (int f : {4, 7, 8, 16, 25, 26, 27}) check(error(imageAt(trimPixels, f), layersAt(trimGraph, f, decodedRed, decodedBlue)) < 3, "Trim/move keyframes and clamped fades match export at boundaries");
    auto fractional = p; fractional.sequences[0].frameRate = {30000, 1001}; fractional.sequences[0].durationFrames = 30;
    fractional.sequences[0].tracks[0].clips[0].durationFrames = 30;
    fractional.sequences[0].tracks[0].clips[0].sourceDurationTicks = *project::framesToTicks(30, {30000, 1001}, blue.media.streams[0].timeBase, project::Rounding::Nearest);
    fractional.sequences[0].tracks[2].clips.resize(1); fractional.sequences[0].tracks[2].clips[0].sourceDurationTicks = *project::framesToTicks(30, {30000, 1001}, red.media.streams[0].timeBase, project::Rounding::Nearest);
    auto fractionalSettings = settings; fractionalSettings.outputPath = out.filePath("fractional.mp4"); fractionalSettings.frameRate = {30000, 1001};
    const auto fractionalGraph = playback::compileSequence(fractional); const auto fractionalPixels = runExport(fractionalGraph, fractionalSettings);
    check(ok && fractionalPixels.size() == 30 * 320 * 180 * 4, "Fractional-rate effect export retains exact frame count");
    for (int f = 0; f < 30; ++f) check(error(imageAt(fractionalPixels, f), layersAt(fractionalGraph, f, decodedRed, decodedBlue)) < 3, "Fractional-rate fades map export samples to exact sequence frames");
    // A styled title uses the same visual stack and animates without a video decoder.
    auto titleProject = project::newProject("Animated title reference"); auto& titleSeq = titleProject.sequences[0]; titleSeq.width = 320; titleSeq.height = 180; titleSeq.durationFrames = 30;
    project::Title title; title.id = project::newId(); title.text = "Animated title"; title.fontFamily = "Arial"; title.fontSize = 32; titleProject.titles = {title};
    project::Track titleTrack; titleTrack.id = project::newId(); titleTrack.name = "Title effects"; titleTrack.kind = "title";
    project::Clip titleClip; titleClip.id = project::newId(); titleClip.name = title.text; titleClip.kind = "title"; titleClip.titleId = title.id; titleClip.durationFrames = 30;
    auto titleOpacity = project::defaultEffect("opacity"); titleOpacity.keyframes["value"] = {{0, 0, "linear"}, {29, 1, "linear"}};
    titleClip.effects = {titleOpacity}; titleTrack.clips = {titleClip}; titleSeq.tracks.append(titleTrack);
    check(project::validate(titleProject).isEmpty(), "Title effects share validated animation model");
    auto titleSettings = settings; titleSettings.outputPath = out.filePath("title-effects.mp4");
    const auto titleGraph = playback::compileSequence(titleProject); const auto titlePixels = runExport(titleGraph, titleSettings);
    for (int f : {0, 1, 14, 29}) check(error(imageAt(titlePixels, f), layersAt(titleGraph, f, {}, {})) < 3, "Animated title raster/export match independently decoded frames");
    QImage black(320, 180, QImage::Format_RGB32); black.fill(Qt::black);
    check(error(layersAt(titleGraph, 0, {}, {}), black) == 0 && error(layersAt(titleGraph, 29, {}, {}), black) > 1, "Title opacity endpoints are invisible then fully rendered");
    playback::PlaybackController titleController; titleController.setPreviewProfile({320, 180}); QImage shownTitle;
    QObject::connect(&titleController, &playback::PlaybackController::frameReady, [&](const QImage& image) { shownTitle = image; }); titleController.setDescription(titleGraph); titleController.play();
    check(waitFor([&] { return titleController.positionUs() >= 450000; }) && titleController.stats()["presented"].toInt() > 8 && error(shownTitle, black) > 0.1, "Title-only playback advances effect animation without newly decoded video"); titleController.pause();
    auto eight = fractional;
    for (int n = 0; n < 6; ++n) { auto t = eight.sequences[0].tracks[0]; t.id = project::newId(); for (auto& cl : t.clips) cl.id = project::newId(); eight.sequences[0].tracks.append(t); }
    playback::PlaybackController layerController; layerController.setPreviewProfile({1920, 1080}, true); layerController.setDescription(playback::compileSequence(eight));
    check(waitFor([&] { return !layerController.stats()["buffering"].toBool() && layerController.stats()["presented"].toInt() > 0; }), "Eight-layer low-memory graph starts and renders");
    const auto eightLayers = layerController.stats()["videoLayers"].toArray(); qint64 eightBytes = 0; bool layerErrors = false;
    for (const auto& layer : eightLayers) { eightBytes += layer.toObject()["queueBytesHighWater"].toInteger(); layerErrors |= !layer.toObject()["error"].toString().isEmpty(); }
    check(eightLayers.size() == 8 && !layerErrors && eightBytes <= 8 * 1024 * 1024, "Eight decoder queues stay within documented low-memory floor and report no errors");
    auto cancelledSettings = settings; cancelledSettings.outputPath = out.filePath("cancelled-effects.mp4"); write(cancelledSettings.outputPath, "prior result");
    auto longGraph = graph; longGraph.sequence->durationFrames = 90000; longGraph.durationUs = 3000000000LL;
    exporting::ExportWorker cancelled(longGraph, cancelledSettings); QObject::connect(&cancelled, &exporting::ExportWorker::progress, &app, [&](int progress) { if (progress > 0) cancelled.cancel(); });
    cancelled.start(); QTimer::singleShot(20, &app, [&] { cancelled.cancel(); });
    check(waitFor([&] { return cancelled.isFinished(); }) && cancelled.result()["cancelled"].toBool() && read(cancelledSettings.outputPath) == "prior result", "Multilayer effect export cancellation preserves last-good destination"); cancelled.wait();
    // Unknown versions stay lossless and visibly bypassable, while export refuses silent loss.
    auto unknown = p; auto& future = unknown.sequences[0].tracks[2].clips[0].effects[0]; future.version = 2;
    check(project::deserialize(project::serialize(unknown)).project == std::optional<project::Project>(unknown) && !exporting::validateExport(playback::compileSequence(unknown), settings).isEmpty(), "Unknown version preserved; enabled export fails usefully");
    future.enabled = false; check(exporting::validateExport(playback::compileSequence(unknown), settings).isEmpty(), "Unknown version bypass permits export");
    auto excessive = p;
    for (int n = 0; n < 8; ++n) { auto t = excessive.sequences[0].tracks[0]; t.id = project::newId(); for (auto& cl : t.clips) cl.id = project::newId(); excessive.sequences[0].tracks.append(t); }
    check(!exporting::validateExport(playback::compileSequence(excessive), settings).isEmpty(), "More than eight overlapping video decoders fail with useful bounded-graph error");
    check(!playback::visualGraphError(graph).size(), "Supported graph has no unknown effect warning");
    const QJsonObject report{{"passed", failures.isEmpty()}, {"checks", checks}, {"failures", failures}, {"exports", exports}, {"comparisons", comparisons}, {"worstExportMeanRgbError", worst},
        {"evidence", "Native deterministic model, raster pixels, production Qt offscreen dialog/playback, independently decoded H.264 exports. No computer use or human visual acceptance."}};
    write(out.filePath("effects-result.json"), QJsonDocument(report).toJson());
    std::printf("Effects: %d checks, %lld failures\n", checks, static_cast<long long>(failures.size())); return failures.isEmpty() ? 0 : 1;
}

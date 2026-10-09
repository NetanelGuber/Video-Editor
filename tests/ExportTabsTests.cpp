#include "export/ExportDialog.h"
#include "export/EncoderConfig.h"
#include "project/ProjectStore.h"
#include "timeline/Timeline.h"
#include "media/Inspection.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QGroupBox>
#include <QLineEdit>
#include <QLabel>
#include <QProgressBar>
#include <QStandardItemModel>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QProcess>
#include <cstdio>
extern "C" {
#include <libavcodec/avcodec.h>
}
using namespace editor;
namespace {
bool waitFor(const std::function<bool()>& done) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < 60000) { if (done()) return true; QApplication::processEvents(); QThread::msleep(1); }
    return false;
}
QByteArray read(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
QByteArray process(const QString& exe, const QStringList& args, bool& ok) {
    QProcess p; p.start(exe, args); ok = p.waitForStarted() && p.waitForFinished(45000) && p.exitCode() == 0;
    if (!ok) { std::fprintf(stderr, "%s\n", p.readAllStandardError().constData()); p.kill(); p.waitForFinished(1000); }
    return p.readAllStandardOutput();
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false); if (app.arguments().size() != 3) return 2;
    QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/segoeui.ttf"));
    app.setFont(QFont("Segoe UI", 9));
    const QDir root(app.arguments()[1]), out(app.arguments()[2]); QDir().mkpath(out.absolutePath());
    int count = 0; QJsonArray failures, outputs;
    auto check = [&](bool ok, const QString& label) { ++count; if (!ok) { failures.append(label); std::fprintf(stderr, "FAIL: %s\n", qPrintable(label)); } };
    auto p = project::newProject("Simple and Advanced export reference"); auto& seq = p.sequences[0]; seq.width = 320; seq.height = 180; seq.durationFrames = 12;
    const auto source = media::inspect(root.filePath("fixtures/generated/session-5/flash-beep.mp4")); check(source.error.isEmpty(), "Load synthetic source"); if (!source.error.isEmpty()) return 1;
    p.media = {source.media};
    for (const auto& stream : source.media.streams) {
        project::Clip clip; clip.id = project::newId(); clip.name = stream.kind; clip.kind = stream.kind; clip.mediaId = source.media.id; clip.streamIndex = stream.index; clip.durationFrames = seq.durationFrames;
        clip.sourceDurationTicks = *project::framesToTicks(clip.durationFrames, seq.frameRate, stream.timeBase, project::Rounding::Nearest);
        seq.tracks[stream.kind == "video" ? 0 : 1].clips = {clip};
    }
    auto settings = p.exportSettings; settings.width = 320; settings.height = 180;
    auto graph = playback::compileSequence(p);
    exporting::ExportDialog dialog(graph, settings); dialog.show(); QApplication::processEvents();
    auto* tabs = dialog.findChild<QTabWidget*>("exportTabs"); auto* start = dialog.findChild<QPushButton*>("exportStart");
    auto* path = dialog.findChild<QLineEdit*>("exportPath"); auto* automatic = dialog.findChild<QCheckBox*>("exportAutomaticRate");
    auto* quality = dialog.findChild<QDoubleSpinBox*>("exportQuality"); auto* rateN = dialog.findChild<QSpinBox*>("exportRateNumerator"); auto* rateD = dialog.findChild<QSpinBox*>("exportRateDenominator");
    auto* simpleQuality = dialog.findChild<QComboBox*>("exportSimpleQuality"); auto* encoder = dialog.findChild<QComboBox*>("exportEncoder");
    auto* bitrate = dialog.findChild<QComboBox*>("exportAudioBitrate"); auto* audioEncoder = dialog.findChild<QComboBox*>("exportAudioEncoder");
    auto* reset = dialog.findChild<QPushButton*>("exportCompatibleDefaults");
    check(tabs && tabs->count() == 2 && tabs->tabText(0) == "Simple" && tabs->tabText(1) == "Advanced" && tabs->currentIndex() == 0, "Ordinary settings open Simple and exactly two named tabs exist");
    check(tabs->widget(0)->findChildren<QComboBox*>().size() == 3 && tabs->widget(0)->findChildren<QCheckBox*>().size() == 1 && tabs->widget(0)->findChildren<QSpinBox*>().isEmpty() && tabs->widget(0)->findChildren<QTableWidget*>().isEmpty(), "Simple contains only target, size, quality and audio controls");
    for (const auto* name : {"exportVideoGroup", "exportSizeGroup", "exportQualityGroup", "exportPixelGroup", "exportAudioGroup", "exportNativeGroup"})
        check(tabs->widget(1)->findChild<QGroupBox*>(name), QString("Advanced contains group ") + name);
    check(automatic->isChecked() && !rateN->isEnabled() && !rateD->isEnabled(), "Automatic FPS is explicit and locks the exact fraction");
    check(dialog.findChild<QLabel*>("exportMessage")->text().contains("Choose an output file"), "Empty destination offers the next action without an extension-conflict error");
    simpleQuality->setCurrentIndex(simpleQuality->findData(18)); check(quality->value() == 18, "Simple quality changes the shared native value");
    tabs->setCurrentIndex(1); check(quality->value() == 18, "Switching to Advanced preserves Simple quality"); quality->setValue(24); tabs->setCurrentIndex(0);
    check(simpleQuality->currentData() == 24, "Advanced quality synchronizes back to Simple");
    automatic->setChecked(false); check(rateN->isEnabled() && !simpleQuality->isEnabled(), "Explicit manual intent disables Simple even when its rate equals automatic");
    automatic->setChecked(true); check(simpleQuality->isEnabled(), "Returning to automatic rate restores Simple choices");
    auto* simpleAudio = dialog.findChild<QCheckBox*>("exportSimpleAudio"); simpleAudio->setChecked(false);
    check(!dialog.findChild<QCheckBox*>("exportAudio")->isChecked() && !bitrate->isEnabled(), "Simple audio off updates Advanced dependencies");
    simpleAudio->setChecked(true);
    project::ExportSettings chosen; int chosenCount = 0;
    QObject::connect(&dialog, &exporting::ExportDialog::settingsChosen, [&](const auto& s) { chosen = s; ++chosenCount; });
    const auto lock = QJsonDocument::fromJson(read(root.filePath("dependencies.lock.json"))).object();
    const auto bin = root.filePath(lock["packages"].toArray()[1].toObject()["prefix"].toString() + "/bin/");
    auto exportAndInspect = [&](const QString& label, const QString& expectedVideo, const QString& expectedAudio, const QString& expectedPixel, const QString& fps) {
        const auto filename = out.filePath(label + "." + exporting::containerExtension(dialog.findChild<QComboBox*>("exportContainer")->currentData().toString()));
        QFile::remove(filename); path->setText(filename); const int prior = chosenCount;
        check(waitFor([&] { return start->isEnabled(); }), "Exact configuration validates: " + label);
        start->click(); check(!start->isEnabled() && !tabs->isEnabled(), "Export locks both tabs: " + label);
        check(waitFor([&] { return chosenCount == prior + 1 && start->isEnabled(); }) && dialog.findChild<QLabel*>("exportMessage")->text().contains("Export complete"), "Production dialog finishes: " + label);
        bool ok = false; const auto metadata = QJsonDocument::fromJson(process(bin + "ffprobe.exe", {"-v", "error", "-show_streams", "-show_format", "-of", "json", filename}, ok)).object();
        check(ok, "Independent FFprobe inspection: " + label); QJsonObject video, audio;
        for (const auto& row : metadata["streams"].toArray()) { const auto stream = row.toObject(); if (stream["codec_type"] == "video") video = stream; else if (stream["codec_type"] == "audio") audio = stream; }
        check(video["codec_name"] == expectedVideo && video["width"] == chosen.width && video["height"] == chosen.height && video["pix_fmt"] == expectedPixel && video["r_frame_rate"] == fps, "Actual video matches selected codec, size, pixels and exact FPS: " + label);
        check(audio["codec_name"] == expectedAudio && audio["sample_rate"] == "48000" && audio["channels"] == 2, "Actual audio matches selected codec/rate/channels: " + label);
        if (expectedAudio == "mp3") check(audio["bit_rate"].toString().toInt() == chosen.audioBitrate, "MP3 bitstream reports the selected audio bitrate");
        if (expectedPixel == "yuv420p10le") check(video["profile"] == "High 10", "Native profile override reaches the bitstream");
        process(bin + "ffmpeg.exe", {"-v", "error", "-nostdin", "-i", filename, "-f", "null", "NUL"}, ok); check(ok, "Complete independent decode: " + label);
        outputs.append(QJsonObject{{"label", label}, {"metadata", metadata}});
    };
    dialog.grab().save(out.filePath("simple-tab.png")); exportAndInspect("simple", "h264", "aac", "yuv420p", "30/1");
    check(!chosen.frameRateCustomized && chosen.quality == 24 && chosen.videoEncoder == "software", "Simple exports the selected shared settings");
    auto project = p; project.exportSettings = chosen;
    auto roundTrip = project::deserialize(project::serialize(project)); check(roundTrip && *roundTrip.project == project, "Simple settings round-trip exactly");
    automatic->setChecked(false); exportAndInspect("manual-same-rate", "h264", "aac", "yuv420p", "30/1");
    check(chosen.frameRateCustomized && chosen.frameRate == graph.frameRate, "Manual intent survives export even when equal to the automatic rate");
    tabs->setCurrentIndex(1); encoder->setCurrentIndex(encoder->findData("libx264"));
    check(dialog.findChild<QComboBox*>("exportFormat")->currentData() == "custom", "Named encoder selects custom Advanced mode");
    dialog.findChild<QComboBox*>("exportContainer")->setCurrentIndex(dialog.findChild<QComboBox*>("exportContainer")->findData("matroska"));
    dialog.findChild<QComboBox*>("exportPixelFormat")->setCurrentIndex(dialog.findChild<QComboBox*>("exportPixelFormat")->findData("yuv420p10le"));
    dialog.findChild<QComboBox*>("exportSpeed")->setCurrentIndex(dialog.findChild<QComboBox*>("exportSpeed")->findData("fast")); quality->setValue(21.5);
    rateN->setValue(30000); rateD->setValue(1001); check(!automatic->isChecked(), "Editing fraction establishes manual FPS intent");
    audioEncoder->setCurrentIndex(audioEncoder->findData("libmp3lame")); bitrate->setCurrentIndex(bitrate->findData(128000));
    check(bitrate->findData(384000) < 0, "MP3 excludes unsupported discrete audio rates");
    auto* native = dialog.findChild<QTableWidget*>("exportNativeOptions");
    auto setNative = [&](const QString& key, const QString& value) { for (int row = 0; row < native->rowCount(); ++row) if (native->item(row, 0)->text() == key) { qobject_cast<QComboBox*>(native->cellWidget(row, 1))->setCurrentText(value); return true; } return false; };
    check(setNative("private:profile", "high10") && setNative("private:tune", "film"), "Profile and tune use actual bundled native options");
    dialog.findChild<QGroupBox*>("exportNativeGroup")->setChecked(true); QApplication::processEvents(); dialog.grab().save(out.filePath("advanced-tab.png"));
    tabs->setCurrentIndex(0); check(!simpleQuality->isEnabled() && dialog.findChild<QLabel*>("exportSimpleNotice")->text().contains("preserved"), "Simple explains retained custom settings without resetting them");
    tabs->setCurrentIndex(1); exportAndInspect("advanced", "h264", "mp3", "yuv420p10le", "30000/1001");
    check(chosen.frameRateCustomized && chosen.quality == 21.5 && chosen.encoderOptions.value("private:tune") == "film" && chosen.audioBitrate == 128000, "Advanced exports exact native/quality/audio intent");
    project.exportSettings = chosen; const auto projectPath = out.filePath("advanced-reference.veproject");
    check(project::ProjectStore::save(project, projectPath).isEmpty(), "Save representative Advanced settings atomically");
    const auto loaded = project::ProjectStore::load(projectPath); check(loaded && *loaded.project == project, "Reopen Advanced settings without losing fractional values or overrides");
    QDir().mkpath(root.filePath("fixtures/generated/session-15.2"));
    auto review = p; review.exportSettings = settings;
    check(project::ProjectStore::save(review, root.filePath("fixtures/generated/session-15.2/export-tabs.veproject")).isEmpty(), "Prepare a small reference project for the focused Windows appearance review");
    exporting::ExportDialog reopened(graph, chosen);
    check(reopened.findChild<QTabWidget*>("exportTabs")->currentIndex() == 1 && reopened.findChild<QComboBox*>("exportAudioBitrate")->currentData() == 128000 && reopened.findChild<QGroupBox*>("exportNativeGroup")->isChecked(), "Saved custom settings open Advanced with native options revealed"); reopened.reject();
    timeline::TimelineEditor editor(p); check(editor.execute(timeline::SetExportSettings{chosen}).isEmpty() && editor.undo() && editor.state().project == p && editor.redo() && editor.state().project == project, "Advanced settings preserve timeline and exact undo/redo");
    auto lowRate = chosen; lowRate.audioBitrate = 192000;
    check(exporting::probeSettings(lowRate, "libx264") != exporting::probeSettings(chosen, "libx264") && exporting::encoderCatalogCacheKey(lowRate) != exporting::encoderCatalogCacheKey(chosen), "Exact and catalog cache identities include audio bitrate");
    const auto* codec = avcodec_find_encoder_by_name("libmp3lame"); auto* context = avcodec_alloc_context3(codec); exporting::configureAudioEncoder(context, chosen);
    check(context->bit_rate == 128000, "Shared probe/export audio configuration applies bitrate in bits/s"); avcodec_free_context(&context);
    bitrate->setCurrentIndex(bitrate->findData(192000)); check(!start->isEnabled(), "Changed audio bitrate invalidates prior exact success immediately");
    audioEncoder->setCurrentIndex(audioEncoder->findData("flac")); check(!bitrate->isEnabled(), "Lossless audio disables unused bitrate");
    const auto retainedPath = path->text(); reset->click(); check(tabs->currentIndex() == 0 && simpleQuality->isEnabled() && path->text().endsWith(".mp4") && QFileInfo(path->text()).completeBaseName() == QFileInfo(retainedPath).completeBaseName() && automatic->isChecked() && native->rowCount() > 0, "Compatible reset restores Simple and retains destination stem");
    check(waitFor([&] { return start->isEnabled(); }), "Reset still requires successful capability validation");
    dialog.resize(600, 650); QApplication::processEvents(); check(start->isVisible() && tabs->height() > 120 && dialog.width() <= 650, "Narrow dialog retains tabs and fixed Export feedback/actions"); dialog.grab().save(out.filePath("simple-narrow.png")); dialog.reject();
    auto schema10 = QJsonDocument::fromJson(project::serialize(p)).object(); schema10["schemaVersion"] = 10; auto old = schema10["exportSettings"].toObject(); old.remove("audioBitrate"); schema10["exportSettings"] = old;
    const auto migrated = project::deserialize(QJsonDocument(schema10).toJson()); check(migrated && migrated.migratedFrom == 10 && migrated.project->exportSettings.audioBitrate == 192000, "Schema 10 migrates former fixed audio bitrate");
    old["audioBitrate"] = 128000; schema10["exportSettings"] = old; check(!project::deserialize(QJsonDocument(schema10).toJson()), "Mislabeled older schema cannot discard newer nondefault audio intent");
    for (int kind = 0; kind < 5; ++kind) {
        auto stale = settings; stale.outputPath = out.filePath("stale.mp4");
        if (kind == 0) stale.pixelFormat = "yuv444p";
        if (kind == 1) stale.videoEncoder = "no_such_encoder";
        if (kind == 2) { stale = exporting::adaptToEncoder(stale, "libx264"); stale.encoderOptions["private:no_such_option"] = "1"; }
        if (kind == 3) stale.audioBitrate = 999999;
        if (kind == 4) { stale = exporting::adaptToEncoder(stale, "libx264"); stale.audioEncoder = "libmp3lame"; stale.audioCodec = "mp3"; stale.audioBitrate = 384000; }
        exporting::ExportDialog invalid(graph, stale);
        check(invalid.findChild<QTabWidget*>("exportTabs")->currentIndex() == 1 && !invalid.findChild<QPushButton*>("exportStart")->isEnabled() && invalid.findChild<QLabel*>("exportMessage")->text().contains(kind == 2 || kind == 1 ? "unavailable" : "conflict", Qt::CaseInsensitive), QString("Stale settings show specific immediate warning and block Export: %1").arg(kind));
        invalid.findChild<QPushButton*>("exportCompatibleDefaults")->click(); check(invalid.findChild<QTabWidget*>("exportTabs")->currentIndex() == 0 && invalid.findChild<QComboBox*>("exportSimpleQuality")->isEnabled(), "Explicit reset repairs stale intent"); invalid.reject();
    }
    auto fastGraph = graph; fastGraph.frameRate = {144, 1}; fastGraph.sequence->frameRate = fastGraph.frameRate;
    exporting::ExportDialog fast(fastGraph, settings); auto* targets = fast.findChild<QComboBox*>("exportSimpleTarget"); auto* model = qobject_cast<QStandardItemModel*>(targets->model());
    check(!model->item(targets->findData("broad-mp4"))->isEnabled() && targets->itemData(targets->findData("broad-mp4"), Qt::ToolTipRole).toString().contains("144/1"), "Simple disables Broad MP4 for automatic 144 fps with an exact conflict reason"); fast.reject();
    for (const bool portrait : {false, true}) {
        auto large = graph; large.width = portrait ? 2160 : 3840; large.height = portrait ? 3840 : 2160;
        auto largeSettings = settings; largeSettings.width = large.width; largeSettings.height = large.height;
        exporting::ExportDialog sizes(large, largeSettings);
        auto* sizeChoice = sizes.findChild<QComboBox*>("exportSimpleSize"); auto* w = sizes.findChild<QSpinBox*>("exportWidth"); auto* h = sizes.findChild<QSpinBox*>("exportHeight");
        sizeChoice->setCurrentIndex(sizeChoice->findData("1080"));
        check(w->value() == (portrait ? 1080 : 1920) && h->value() == (portrait ? 1920 : 1080), "Simple fits 1080p with the correct landscape/portrait aspect");
        sizeChoice->setCurrentIndex(sizeChoice->findData("720"));
        check(w->value() == (portrait ? 720 : 1280) && h->value() == (portrait ? 1280 : 720), "Simple fits 720p with the correct landscape/portrait aspect");
        sizeChoice->setCurrentIndex(sizeChoice->findData("sequence"));
        check(w->value() == large.width && h->value() == large.height, "Sequence size restores source dimensions without modifying timeline timing");
        auto* simpleTarget = sizes.findChild<QComboBox*>("exportSimpleTarget");
        simpleTarget->setCurrentIndex(simpleTarget->findData("desktop-mkv"));
        check(sizes.findChild<QComboBox*>("exportContainer")->currentData() == "matroska" && sizes.findChild<QComboBox*>("exportFormat")->currentData() == "desktop-mkv", "Simple target changes the shared encoder/container controls"); sizes.reject();
    }
    const QJsonObject report{{"passed", failures.isEmpty()}, {"checkCount", count}, {"failures", failures}, {"exports", outputs}, {"limits", "Production Qt widgets exercised offscreen; exported video/audio independently inspected and decoded. Native Windows scaling, subjective appearance and physical playback devices remain unverified."}};
    QFile reportFile(out.filePath("export-tabs-result.json")); if (reportFile.open(QIODevice::WriteOnly)) reportFile.write(QJsonDocument(report).toJson());
    std::printf("Export tabs: %d checks, %lld failures\n", count, static_cast<long long>(failures.size())); return failures.isEmpty() ? 0 : 1;
}

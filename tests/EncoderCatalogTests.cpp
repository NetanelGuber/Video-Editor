#include "export/Capabilities.h"
#include "export/ExportDialog.h"
#include "export/ExportWorker.h"
#include "project/ProjectStore.h"
#include "media/Inspection.h"
#include "timeline/Timeline.h"
#include <QApplication>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QProcess>
#include <cstdio>
using namespace editor;
namespace {
QByteArray read(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
void write(const QString& path, const QByteArray& bytes) { QFile f(path); if (f.open(QIODevice::WriteOnly)) f.write(bytes); }
bool waitFor(const std::function<bool()>& done) { QElapsedTimer t; t.start(); while (t.elapsed() < 45000) { if (done()) return true; QCoreApplication::processEvents(); QThread::msleep(1); } return false; }
QByteArray process(const QString& exe, const QStringList& args, bool& ok) { QProcess p; p.start(exe, args); ok = p.waitForStarted() && p.waitForFinished(45000) && p.exitCode() == 0; if (!ok) { std::fprintf(stderr,"%s\n",p.readAllStandardError().constData()); p.kill(); p.waitForFinished(1000); } return p.readAllStandardOutput(); }
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false); if (app.arguments().size() != 3) return 2;
    const QDir root(app.arguments()[1]), out(app.arguments()[2]); QDir().mkpath(out.absolutePath());
    int checks = 0; QJsonArray failures, catalog, outputs;
    auto check = [&](bool ok, const QString& label) { ++checks; if (!ok) { failures.append(label); std::fprintf(stderr,"FAIL: %s\n",qPrintable(label)); } };
    auto p = project::newProject("All encoder reference"); p.sequences[0].durationFrames = 6;
    auto media = media::inspect(root.filePath("fixtures/generated/session-5/flash-beep.mp4")); check(media.error.isEmpty(), "Reference media loaded"); if (!media.error.isEmpty()) return 1;
    p.media = {media.media};
    for (const auto& stream : media.media.streams) if (stream.kind == "video" || stream.kind == "audio") {
        project::Clip c; c.id = project::newId(); c.name = stream.kind; c.kind = stream.kind; c.mediaId = media.media.id; c.streamIndex = stream.index; c.durationFrames = 6; c.sourceDurationTicks = *project::framesToTicks(6, {30,1}, stream.timeBase, project::Rounding::Nearest);
        p.sequences[0].tracks[stream.kind == "video" ? 0 : 1].clips = {c};
    }
    const auto graph = playback::compileSequence(p); auto base = p.exportSettings; base.width = 320; base.height = 180; base.audioEnabled = false;
    const auto names = exporting::videoEncoderNames(); check(names.size() > 100, "Every compiled video encoder inventoried");
    exporting::ExportDialog dialog(graph, base); auto* enc = dialog.findChild<QComboBox*>("exportEncoder");
    for (const auto& name : names) {
        const auto controls = exporting::encoderControls(name); auto s = exporting::adaptToEncoder(base, name); s.width = 640; s.height = 360;
        check(enc->findData(name) >= 0 && controls["name"] == name, "Native production selection/descriptor: " + name);
        check(exporting::nativeSettingsError(s, name).isEmpty(), "Default native values validate: " + name);
        check(controls["qualityMinimum"].toInt() <= controls["qualityDefault"].toInt() && controls["qualityMaximum"].toInt() >= controls["qualityDefault"].toInt(), "Native quality default is in range: " + name);
        // Every encoder gets an isolated exact-settings attempt. Generic failures are retained, never treated as global unavailability.
        auto result = exporting::runCapabilityProbe({"--encoder"}, exporting::probeSettings(s, name), {}, 6000);
        result["encoder"] = name; result["configuration"] = QJsonDocument::fromJson(exporting::probeSettings(s, name)).object(); result["controls"] = controls;
        check(result["usable"].toBool() || !result["reason"].toString().isEmpty(), "Every compiled encoder receives an explicit runtime outcome: " + name); catalog.append(result);
    }
    const auto amf = exporting::encoderControls("hevc_amf"), av1 = exporting::encoderControls("av1_amf"), vp9 = exporting::encoderControls("libvpx-vp9");
    check(amf["speeds"].toArray().contains("balanced") && amf["speeds"].toArray().contains("speed") && !amf["speeds"].toArray().contains("medium"), "AMF exposes actual quality constants instead of CPU labels");
    check(amf["qualityMaximum"] == 51 && av1["qualityMaximum"] == 255 && vp9["qualityMaximum"] == 63, "HEVC AMF / AV1 AMF / VP9 use distinct native scales");
    auto invalid = exporting::adaptToEncoder(base, "hevc_amf"); invalid.preset = "medium"; check(exporting::exportCombinationError(invalid).contains("speed"), "CPU preset is rejected for AMF");
    invalid.preset = "balanced"; invalid.quality = 100; check(exporting::exportCombinationError(invalid).contains("0–51"), "Native out-of-range HEVC quality rejected");
    invalid = exporting::adaptToEncoder(base, "libx264"); invalid.encoderOptions["private:preset"] = "slow"; check(exporting::exportCombinationError(invalid).contains("conflicts"), "Duplicate speed override blocked");
    invalid.preset = "default"; check(exporting::exportCombinationError(invalid).isEmpty(), "Native override allowed when primary speed uses default");
    invalid.encoderOptions["private:no_such_option"] = "1"; check(exporting::exportCombinationError(invalid).contains("unavailable"), "Unknown native override explained"); invalid.encoderOptions.remove("private:no_such_option"); invalid.encoderOptions["context:bf"] = "abc"; check(!exporting::exportCombinationError(invalid).isEmpty(), "Invalid typed native option blocked");
    auto stored = p; stored.exportSettings = exporting::adaptToEncoder(base, "libx264"); stored.exportSettings.quality = 20.25; stored.exportSettings.encoderOptions["private:tune"] = "film";
    const auto loaded = project::deserialize(project::serialize(stored)); check(loaded && *loaded.project == stored, "Schema 8 native codec controls round-trip exactly");
    check(project::ProjectStore::save(stored, out.filePath("native-controls.veproject")).isEmpty(), "Save native controls to disk atomically");
    const auto reopened = project::ProjectStore::load(out.filePath("native-controls.veproject")); check(reopened && *reopened.project == stored, "Reopen fractional quality and native overrides exactly");
    exporting::ExportDialog reopenedDialog(graph, stored.exportSettings); check(reopenedDialog.findChild<QDoubleSpinBox*>("exportQuality")->value() == 20.25 && reopenedDialog.findChild<QComboBox*>("exportEncoder")->currentData() == "libx264", "Production dialog restores the reopened native settings"); reopenedDialog.reject();
    timeline::TimelineEditor editor(p); check(editor.execute(timeline::SetExportSettings{stored.exportSettings}).isEmpty() && editor.undo() && editor.state().project == p && editor.redo() && editor.state().project == stored, "Native codec controls persist through exact undo/redo");
    auto old = QJsonDocument::fromJson(project::serialize(p)).object(); old["schemaVersion"] = 7;
    auto oldSequences = old["sequences"].toArray();
    for (qsizetype i = 0; i < oldSequences.size(); ++i) { auto sequence = oldSequences[i].toObject(); sequence.remove("primaryVideoMediaId"); sequence.remove("primaryVideoStreamIndex"); oldSequences[i] = sequence; }
    old["sequences"] = oldSequences; auto e = old["exportSettings"].toObject(); e.remove("qualityMode"); e.remove("audioEncoder"); e.remove("encoderOptions"); e["videoEncoder"] = "hevc_amf"; e["videoCodec"] = "hevc"; e["compatibilityProfile"] = "hevc-mp4"; e["preset"] = "medium"; old["exportSettings"] = e; const auto migrated = project::deserialize(QJsonDocument(old).toJson()); check(migrated && migrated.migratedFrom == 7 && migrated.project->exportSettings.preset == "balanced", "Legacy AMF speed migrates to its previous native behavior");
    enc->setCurrentIndex(enc->findData("hevc_amf")); auto* speed = dialog.findChild<QComboBox*>("exportSpeed"); auto* quality = dialog.findChild<QDoubleSpinBox*>("exportQuality");
    check(dialog.findChild<QComboBox*>("exportFormat")->currentData() == "custom" && speed->findData("balanced") >= 0 && speed->findData("medium") < 0 && quality->maximum() == 51, "Production selection adapts HEVC AMF speed and scale");
    enc->setCurrentIndex(enc->findData("av1_amf")); check(quality->maximum() == 255, "Production AV1 AMF scale adapts");
    enc->setCurrentIndex(enc->findData("libvpx-vp9")); check(quality->maximum() == 63 && speed->findData("8") >= 0, "Production VP9 speed/scale adapts");
    enc->setCurrentIndex(enc->findData("libjxl")); quality->setValue(0.75); check(quality->decimals() >= 2 && quality->value() == 0.75 && speed->findData("9") >= 0 && speed->findData("10") < 0, "JPEG XL exposes fractional distance and its actual effort range");
    enc->setCurrentIndex(enc->findData("libwebp")); check(quality->maximum() == 100 && speed->findData("6") >= 0 && speed->findData("photo") < 0, "WebP separates compression speed from image-type presets");
    check(dialog.findChild<QLabel*>("exportNativeHint")->text().contains("Applied libwebp defaults"), "Changing an encoder explicitly reports its native defaults/reset");
    enc->setCurrentIndex(enc->findData("ffv1")); check(!quality->isEnabled() && !speed->isEnabled() && dialog.findChild<QTableWidget*>("exportNativeOptions")->rowCount() > 0, "Lossless codec disables irrelevant fields and retains native options");
    dialog.reject();
    auto fixed = base; fixed.encoderOptions["private:x264-params"] = "level=5.1"; check(exporting::exportCombinationError(fixed).contains("fixed playback target"), "Native overrides cannot invalidate fixed target guarantees");
    auto lossless = exporting::adaptToEncoder(base, "ffv1"); lossless.qualityMode = "bitrate"; lossless.quality = 5000; check(exporting::exportCombinationError(lossless).contains("no primary target bitrate"), "Lossless codec rejects an irrelevant target bitrate");
    auto integerQP = exporting::adaptToEncoder(base, "hevc_amf"); integerQP.quality = 20.25; check(exporting::exportCombinationError(integerQP).contains("whole-number"), "Integer native quantizers reject fractional quality");
    auto x264 = exporting::adaptToEncoder(base, "libx264"), rgb = exporting::adaptToEncoder(base, "libx264rgb");
    check(exporting::probeSettings(x264, "all-supported") != exporting::probeSettings(rgb, "all-supported"), "Capability configuration identity includes the requested encoder even when its native defaults match");
    x264.outputPath = out.filePath("ui-native-validation.mkv"); exporting::ExportDialog switchDialog(graph, x264); auto* switchStart = switchDialog.findChild<QPushButton*>("exportStart");
    check(waitFor([&] { return switchStart->isEnabled(); }), "Initial custom encoder validates in production dialog");
    switchDialog.findChild<QComboBox*>("exportEncoder")->setCurrentIndex(switchDialog.findChild<QComboBox*>("exportEncoder")->findData("libx264rgb"));
    check(!switchStart->isEnabled() && waitFor([&] { return switchStart->isEnabled() && switchDialog.findChild<QLabel*>("exportCapabilities")->text().contains("libx264rgb"); }), "Selecting another encoder with identical controls invalidates the previous probe and completes a new exact check"); switchDialog.reject();
    const auto lock = QJsonDocument::fromJson(read(root.filePath("dependencies.lock.json"))).object(); const auto bin = root.filePath(lock["packages"].toArray()[1].toObject()["prefix"].toString() + "/bin/");
    auto exportSample = [&](project::ExportSettings s, const QString& label) {
        s.outputPath = out.filePath(label + "." + exporting::containerExtension(s.container)); exporting::ExportWorker worker(graph, s); worker.start(); const bool complete = waitFor([&] { return worker.isFinished(); }); worker.cancel(); worker.wait(); auto result = worker.result();
        check(complete && result["passed"].toBool(), "Production export " + label + ": " + result["error"].toString());
        if (result["passed"].toBool()) {
            bool ok = false; const auto metadata = QJsonDocument::fromJson(process(bin + "ffprobe.exe", {"-v","error","-count_frames","-show_streams","-show_format","-of","json",s.outputPath},ok)).object(); check(ok,"Independent inspection " + label);
            const auto streams = metadata["streams"].toArray(); check(streams.size() == (s.audioEnabled ? 2 : 1),"Audio inclusion " + label);
            if (!streams.isEmpty()) { const auto video = streams[0].toObject(); check(video["codec_name"] == s.videoCodec && video["width"] == s.width && video["height"] == s.height && video["nb_read_frames"] == "6", "Actual codec/dimensions/frame count " + label); }
            if (!streams.isEmpty() && s.pixelFormat != "auto") check(streams[0].toObject()["pix_fmt"] == s.pixelFormat, "Explicit native pixel format " + label);
            if (!streams.isEmpty() && label == "prores-proxy") check(streams[0].toObject()["profile"] == "Proxy", "Native ProRes profile override affects the actual bitstream");
            if (s.audioEnabled && streams.size() > 1) check(streams[1].toObject()["codec_name"] == s.audioCodec && streams[1].toObject()["channels"] == 2,"Selected audio codec " + label);
            process(bin + "ffmpeg.exe", {"-v","error","-nostdin","-i",s.outputPath,"-f","null","NUL"},ok); check(ok,"Independent complete decode " + label); result["metadata"] = metadata;
        }
        outputs.append(result);
    };
    for (const auto& name : QStringList{"libx264", "libx264rgb", "libx265", "libvpx-vp9", "libaom-av1", "libsvtav1", "prores_ks", "ffv1", "huffyuv", "mpeg4", "mjpeg", "png", "gif", "rawvideo", "bmp", "libtheora"}) {
        auto s = exporting::adaptToEncoder(base, name);
        if (name == "libaom-av1") s.preset = "8";
        if (name == "libsvtav1") s.preset = "10";
        if (name == "libvpx-vp9") s.preset = "8";
        exportSample(s, name);
    }
    auto tenBit = exporting::adaptToEncoder(base, "ffv1"); tenBit.pixelFormat = "yuv420p10le"; tenBit.encoderOptions["context:level"] = "3"; tenBit.encoderOptions["private:coder"] = "range_tab"; exportSample(tenBit, "ffv1-10bit");
    tenBit = exporting::adaptToEncoder(base, "libx265"); tenBit.pixelFormat = "yuv420p10le"; exportSample(tenBit, "hevc-main10");
    auto prores = exporting::adaptToEncoder(base, "prores_ks"); prores.encoderOptions["private:profile"] = "proxy"; exportSample(prores, "prores-proxy");
    auto fractional = stored.exportSettings; exportSample(fractional, "x264-fractional-crf");
    for (const auto& name : QStringList{"h264_amf", "hevc_amf", "av1_amf", "h264_nvenc", "hevc_nvenc", "av1_nvenc", "h264_qsv", "hevc_qsv", "av1_qsv", "h264_d3d12va", "hevc_d3d12va", "av1_d3d12va", "h264_mf", "hevc_mf", "av1_mf"}) {
        auto s = exporting::adaptToEncoder(base, name); s.width = 1920; s.height = 1080;
        const auto probe = exporting::probeEncoder(s, name); auto entry = probe; entry["reference1080p"] = true; catalog.append(entry); if (probe["usable"].toBool()) exportSample(s, name);
    }
    for (const auto& a : QStringList{"aac", "libopus", "flac", "libmp3lame", "ac3", "pcm_s16le"}) {
        auto s = exporting::adaptToEncoder(base, "ffv1"); s.container = "matroska"; s.audioEnabled = true; s.audioEncoder = a; s.audioCodec = a == "libopus" ? "opus" : a == "libmp3lame" ? "mp3" : a; exportSample(s,"audio-" + a);
    }
    QJsonObject report{{"passed",failures.isEmpty()},{"checkCount",checks},{"failures",failures},{"videoEncoderCount",names.size()},{"catalog",catalog},{"exports",outputs},{"limits","All compiled video encoders are selectable and classified at a representative exact configuration. Failed probes do not prove global unavailability. Physical playback, visual scaling, perceived quality and long hardware runs remain unverified."}};
    write(out.filePath("encoder-catalog-result.json"),QJsonDocument(report).toJson()); std::printf("Encoder catalog: %d encoders, %d checks, %lld failures\n", static_cast<int>(names.size()),checks,static_cast<long long>(failures.size())); return failures.isEmpty() ? 0 : 1;
}

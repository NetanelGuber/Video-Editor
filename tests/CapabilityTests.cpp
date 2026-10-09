#include "export/Capabilities.h"
#include "export/ExportDialog.h"
#include "export/ExportWorker.h"
#include "media/Inspection.h"
#include "project/ProjectStore.h"
#include "timeline/Timeline.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTimer>
#include <cstdio>
using namespace editor;
namespace {
QByteArray read(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
bool write(const QString& path, const QByteArray& bytes) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(); }
bool waitFor(const std::function<bool()>& done, int timeout = 120000) {
    QElapsedTimer clock; clock.start();
    while (clock.elapsed() < timeout) { if (done()) return true; QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1); }
    return false;
}
QJsonObject subprocess(const QString& executable, const QStringList& args, bool& ok) {
    QProcess p; p.start(executable, args); ok = p.waitForStarted() && p.waitForFinished(60000) && p.exitCode() == 0;
    if (!ok) { std::fprintf(stderr, "%s\n", p.readAllStandardError().constData()); p.kill(); p.waitForFinished(1000); }
    return QJsonDocument::fromJson(p.readAllStandardOutput()).object();
}
project::Clip clip(const project::Media& m, const QString& kind) {
    project::Clip c; c.id = project::newId(); c.name = "Compatibility reference"; c.mediaId = m.id; c.kind = kind; c.durationFrames = 12;
    for (const auto& s : m.streams) if (s.kind == kind) { c.streamIndex = s.index; c.sourceDurationTicks = *project::framesToTicks(12, {30,1}, s.timeBase, project::Rounding::Nearest); break; }
    return c;
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    if (app.arguments().contains("--stall-helper")) { QThread::msleep(10000); return 0; }
    if (app.arguments().contains("--failed-helper")) return 17;
    if (app.arguments().size() != 3) return 2;
    const QDir root(app.arguments()[1]), out(app.arguments()[2]); QDir().mkpath(out.absolutePath());
    QJsonArray failures, exports, probes; int checks = 0;
    auto check = [&](bool ok, const QString& label) { ++checks; if (!ok) { failures.append(label); std::fprintf(stderr, "FAIL: %s\n", qPrintable(label)); } };
    const auto build = exporting::compiledCapabilities();
    check(build["version"].toString().startsWith("9.0.2") && build["license"].toString().contains("GPL"), "Loaded pinned version and license are inventoried");
    check(build["encoders"].toArray().size() > 50 && build["muxers"].toArray().size() > 50 && build["pixelFormats"].toArray().size() > 50, "Runtime inventory enumerates loaded encoders, muxers and pixel formats");
    auto media = media::inspect(root.filePath("fixtures/generated/session-5/flash-beep.mp4"));
    check(media.error.isEmpty(), "Reference footage inspected"); if (!media.error.isEmpty()) return 1;
    auto p = project::newProject("Session 15 compatibility"); p.media = {media.media}; p.sequences[0].durationFrames = 12;
    p.sequences[0].tracks[0].clips = {clip(media.media, "video")}; p.sequences[0].tracks[1].clips = {clip(media.media, "audio")};
    const auto graph = playback::compileSequence(p);
    check(project::validate(p).isEmpty(), "Reference project validates: " + project::validate(p));
    if (!project::validate(p).isEmpty()) return 1;
    auto settings = p.exportSettings; settings.width = 320; settings.height = 180; settings.preset = "veryfast"; settings.outputPath = out.filePath("desktop.mp4");
    const auto discovery = exporting::discoverCapabilities(settings, true);
    check(discovery["error"].toString().isEmpty() && discovery["encoder"] == "libx264", "Dependable software fallback is actually usable");
    check(discovery["deviceProbes"].toArray().size() == build["hardwareDeviceTypes"].toArray().size(), "All compiled device types have separate creation outcomes");
    for (const auto& probe : discovery["encoderProbes"].toArray()) probes.append(probe);
    bool unavailableHardware = false;
    for (const auto& v : discovery["encoderProbes"].toArray()) {
        const auto q = v.toObject(); if (!q["encoder"].toString().startsWith("libx") && !q["usable"].toBool()) unavailableHardware = true;
    }
    check(unavailableHardware, "Compiled encoders are distinguished from unavailable target-PC hardware");
    QJsonArray simulated{QJsonObject{{"encoder", "h264_amf"}, {"usable", false}, {"reason", "No AMD device"}},
        QJsonObject{{"encoder", "h264_nvenc"}, {"usable", false}, {"reason", "No NVIDIA device"}},
        QJsonObject{{"encoder", "h264_qsv"}, {"usable", false}, {"reason", "No Intel device"}},
        QJsonObject{{"encoder", "libx264"}, {"usable", true}}};
    auto automatic = settings; automatic.videoEncoder = "auto";
    const auto fallback = exporting::resolveEncoder(automatic, simulated);
    check(fallback.error.isEmpty() && fallback.encoder == "libx264" && fallback.warning.contains("No AMD device"), "Software-only computer uses explicit reported fallback");
    auto explicitHardware = settings; explicitHardware.videoEncoder = "h264_nvenc"; explicitHardware.preset = "p4";
    const auto rejected = exporting::resolveEncoder(explicitHardware, simulated);
    check(rejected.encoder.isEmpty() && rejected.error.contains("NVIDIA") && rejected.error.contains("Software"), "Explicit absent hardware never silently falls back");
    simulated.removeLast(); check(!exporting::resolveEncoder(automatic, simulated).error.isEmpty(), "Missing hardware and software blocks export");
    auto timeout = exporting::runCapabilityProbe({"--stall-helper"}, {}, {}, 100, QCoreApplication::applicationFilePath());
    check(!timeout["usable"].toBool() && timeout["reason"].toString().contains("timed out"), "Hung child process is killed at deadline");
    auto cancelled = exporting::runCapabilityProbe({"--stall-helper"}, {}, [] { return true; }, 10000, QCoreApplication::applicationFilePath());
    check(cancelled["reason"].toString().contains("cancelled"), "Child process cancellation is bounded");
    auto failed = exporting::runCapabilityProbe({"--failed-helper"}, {}, {}, 2000, QCoreApplication::applicationFilePath());
    check(failed["reason"].toString().contains("failed/crashed"), "Failed helper cannot validate an encoder");
    auto missing = exporting::runCapabilityProbe({}, {}, {}, 1000, out.filePath("no-helper.exe"));
    check(missing["reason"].toString().contains("could not start"), "Missing deployment helper reports a fix");
    // Immediate conflicts must name the choices and a resolution.
    auto conflict = settings; conflict.videoEncoder = "hevc_amf";
    check(exporting::exportCombinationError(conflict).contains("hevc_amf") && exporting::exportCombinationError(conflict).contains("h264"), "Codec/encoder conflict is specific");
    conflict = settings; conflict.container = "webm";
    check(exporting::exportCombinationError(conflict).contains("webm") && exporting::exportCombinationError(conflict).contains("Reselect"), "Target/container conflict names its fix");
    conflict = settings; conflict.pixelFormat = "p010le";
    check(exporting::exportCombinationError(conflict).contains("p010le") && exporting::exportCombinationError(conflict).contains("8-bit"), "Pixel depth conflict is immediate");
    conflict = settings; conflict.videoEncoder = "h264_qsv"; conflict.preset = "default"; conflict.pixelFormat = "yuv420p";
    const auto pixelConflict = exporting::probeEncoder(conflict, "h264_qsv");
    check(!pixelConflict["usable"].toBool() && pixelConflict["reason"].toString().contains("pixel format"), "Advertised pixel format compatibility is checked");
    conflict = settings; conflict.compatibilityProfile = "broad-mp4"; conflict.frameRate = {60,1};
    check(exporting::exportCombinationError(conflict).contains("30 fps"), "Broad target frame rate cap is enforced");
    conflict.frameRate = {30,1}; conflict.width = 3840; conflict.height = 2160;
    check(exporting::exportCombinationError(conflict).contains("1920"), "Broad target size cap is enforced");
    conflict.width = 1080; conflict.height = 1920;
    check(exporting::exportCombinationError(conflict).isEmpty(), "Portrait broad target is supported");
    conflict.videoEncoder = "h264_amf";
    check(exporting::exportCombinationError(conflict).contains("Level 4.1"), "Broad target cannot use unvalidated hardware level constraints");
    conflict = settings; conflict.compatibilityProfile = "removed-profile";
    check(exporting::exportCombinationError(conflict).contains("removed-profile"), "Stale profile is preserved and explained");
    // File persistence on a computer with absent drivers must not prevent timeline editing.
    auto stored = p; stored.exportSettings = settings; stored.exportSettings.videoEncoder = "h264_nvenc"; stored.exportSettings.pixelFormat = "nv12";
    check(project::ProjectStore::save(stored, out.filePath("hardware-choice.veproject")).isEmpty(), "Save explicit hardware intent");
    auto loaded = project::ProjectStore::load(out.filePath("hardware-choice.veproject"));
    check(loaded && *loaded.project == stored, "Unavailable driver assumptions are not serialized; choices round-trip exactly");
    timeline::TimelineEditor editor(p); auto original = editor.state();
    check(editor.execute(timeline::SetExportSettings{stored.exportSettings}).isEmpty() && editor.undo() && editor.state() == original && editor.redo(), "Device export choices follow exact undo/redo");
    auto legacy = QJsonDocument::fromJson(project::serialize(p)).object(); legacy["schemaVersion"] = 6;
    auto legacySequences = legacy["sequences"].toArray();
    for (qsizetype i = 0; i < legacySequences.size(); ++i) { auto sequence = legacySequences[i].toObject(); sequence.remove("primaryVideoMediaId"); sequence.remove("primaryVideoStreamIndex"); legacySequences[i] = sequence; }
    legacy["sequences"] = legacySequences; auto legacySettings = legacy["exportSettings"].toObject();
    legacySettings.remove("compatibilityProfile"); legacySettings.remove("videoEncoder"); legacySettings.remove("pixelFormat"); legacy["exportSettings"] = legacySettings;
    auto migration = project::deserialize(QJsonDocument(legacy).toJson());
    check(migration && migration.migratedFrom == 6 && *migration.project == p, "Schema 6 migrates legacy choices without loss");
    for (int schema : {1,2,3,4}) {
        const auto name = schema == 4 ? "effects-v4.veproject" : QString("multitrack-v%1.veproject").arg(schema);
        const auto old = project::ProjectStore::load(root.filePath("fixtures/projects/" + name));
        check(old && old.migratedFrom == schema && old.project->exportSettings.videoEncoder == "software", QString("Schema %1 migrates to reproducible software intent").arg(schema));
    }
    auto old5 = QJsonDocument::fromJson(project::serialize(p)).object(); old5["schemaVersion"] = 5;
    auto old5Sequences = old5["sequences"].toArray();
    for (qsizetype i = 0; i < old5Sequences.size(); ++i) { auto sequence = old5Sequences[i].toObject(); sequence.remove("primaryVideoMediaId"); sequence.remove("primaryVideoStreamIndex"); old5Sequences[i] = sequence; }
    old5["sequences"] = old5Sequences;
    const auto migrated5 = project::deserialize(QJsonDocument(old5).toJson());
    check(migrated5 && migrated5.migratedFrom == 5, "Schema 5 migration remains supported");
    const auto lock = QJsonDocument::fromJson(read(root.filePath("dependencies.lock.json"))).object();
    const auto bin = root.filePath(lock["packages"].toArray()[1].toObject()["prefix"].toString() + "/bin/");
    auto exportAndInspect = [&](project::ExportSettings s, const QString& name) {
        s.outputPath = out.filePath(name + (s.container == "matroska" ? ".mkv" : ".mp4"));
        exporting::ExportWorker worker(graph, s); worker.start(); const bool complete = waitFor([&] { return worker.isFinished(); }); worker.cancel(); worker.wait();
        const auto result = worker.result(); check(complete && result["passed"].toBool(), "Production export " + name + ": " + result["error"].toString());
        if (!result["passed"].toBool()) { exports.append(result); return; }
        bool ok = false; const auto metadata = subprocess(bin + "ffprobe.exe", {"-v", "error", "-count_frames", "-show_streams", "-show_format", "-of", "json", s.outputPath}, ok);
        check(ok, "Independent container inspection " + name); auto streams = metadata["streams"].toArray();
        check(streams.size() == (s.audioEnabled ? 2 : 1), "Selected audio inclusion " + name);
        if (!streams.isEmpty()) {
            const auto v = streams[0].toObject();
            check(v["codec_name"] == s.videoCodec && v["pix_fmt"] == "yuv420p" && v["width"] == s.width && v["height"] == s.height, "Independent codec/pixel/dimensions " + name);
            check(v["avg_frame_rate"].toString() == QString("%1/%2").arg(s.frameRate.numerator).arg(s.frameRate.denominator) && v["nb_read_frames"] == "12", "Exact selected rational frame rate and frame count " + name);
            if (s.videoCodec == "hevc") check(v["profile"] == "Main" && v["codec_tag_string"] == "hvc1", "HEVC Main hvc1 compatibility tag " + name);
            if (s.compatibilityProfile == "broad-mp4") check(v["profile"] == "High" && v["level"] == 41, "Broad H.264 High Level 4.1 " + name);
        }
        const auto format = metadata["format"].toObject()["format_name"].toString();
        check(format.contains(s.container == "matroska" ? "matroska" : "mp4"), "Independent selected container " + name);
        auto saved = p; saved.exportSettings = s;
        check(project::deserialize(project::serialize(saved)).project == std::optional<project::Project>(saved), "Profile/encoder/output settings persist exactly " + name);
        if (s.audioEnabled && streams.size() == 2) { const auto a = streams[1].toObject(); check(a["codec_name"] == "aac" && a["profile"] == "LC" && a["sample_rate"] == "48000" && a["channels"] == 2, "AAC-LC stereo 48 kHz " + name); }
        subprocess(bin + "ffmpeg.exe", {"-v", "error", "-nostdin", "-i", s.outputPath, "-f", "null", "NUL"}, ok); check(ok, "Independent complete audio/video decode " + name);
        write(out.filePath(name + "-ffprobe.json"), QJsonDocument(metadata).toJson());
        auto entry = result; entry["metadata"] = metadata; exports.append(entry);
    };
    for (const auto& target : exporting::compatibilityProfiles()) {
        auto s = settings; s.compatibilityProfile = target.id; s.container = target.container; s.videoCodec = target.codec;
        if (target.id == "broad-mp4") { s.width = 1920; s.height = 1080; s.preset = "medium"; }
        if (target.id == "desktop-mkv") s.frameRate = {30000,1001};
        exportAndInspect(s, target.id + "-software");
    }
    auto hevc = settings; hevc.compatibilityProfile = "hevc-mp4"; hevc.videoCodec = "hevc"; hevc.videoEncoder = "auto"; hevc.preset = "default"; hevc.qualityMode = "default";
    auto smallHevc = hevc; smallHevc.videoEncoder = "hevc_amf"; smallHevc.preset = "balanced";
    const auto smallHevcProbe = exporting::probeEncoder(smallHevc, "hevc_amf");
    // The AMD HEVC driver can reject small dimensions despite accepting 1080p.
    hevc.width = 1920; hevc.height = 1080;
    const auto hevcDiscovery = exporting::discoverCapabilities(hevc, false);
    for (const auto& probe : hevcDiscovery["encoderProbes"].toArray()) probes.append(probe);
    int usableHardwareCount = 0;
    for (const auto& probe : probes) {
        const auto q = probe.toObject(); const auto name = q["encoder"].toString(); if (!q["usable"].toBool() || name.startsWith("libx")) continue;
        ++usableHardwareCount;
        auto s = name.startsWith("hevc") ? hevc : settings; s.videoEncoder = name; exportAndInspect(s, name);
    }
    auto fourK = hevc; fourK.videoEncoder = "software"; fourK.width = 3840; fourK.height = 2160; fourK.audioEnabled = false;
    exportAndInspect(fourK, "hevc-4k-video-only");
    auto autoExport = settings; autoExport.videoEncoder = "auto"; exportAndInspect(autoExport, "automatic-desktop");
    check(exports.size() == 6 + usableHardwareCount, "All profiles, every usable target-PC hardware codec, automatic and 4K samples are inspected");
    // Explicit unusable encoder must leave an existing destination intact.
    auto absent = settings; absent.videoEncoder = "h264_nvenc"; absent.outputPath = out.filePath("unavailable.mp4");
    const QByteArray sentinel("existing destination"); write(absent.outputPath, sentinel);
    for (const auto& probe : discovery["encoderProbes"].toArray()) if (probe.toObject()["encoder"] == absent.videoEncoder && !probe.toObject()["usable"].toBool()) {
        exporting::ExportWorker worker(graph, absent); worker.start(); check(waitFor([&] { return worker.isFinished(); }), "Absent hardware export ends"); worker.wait();
        check(!worker.result()["passed"].toBool() && worker.result()["error"].toString().contains("h264_nvenc") && read(absent.outputPath) == sentinel, "Hardware failure preserves destination and names encoder");
    }
    exporting::ExportDialog dialog(graph, settings); dialog.show();
    auto* start = dialog.findChild<QPushButton*>("exportStart"); auto* message = dialog.findChild<QLabel*>("exportMessage");
    auto* width = dialog.findChild<QSpinBox*>("exportWidth"); auto* target = dialog.findChild<QComboBox*>("exportFormat");
    auto* encoder = dialog.findChild<QComboBox*>("exportEncoder"); auto* pixel = dialog.findChild<QComboBox*>("exportPixelFormat");
    check(!start->isEnabled(), "Export is blocked during initial discovery");
    int heartbeats = 0; QTimer heartbeat; heartbeat.setInterval(5); QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeats; }); heartbeat.start();
    check(waitFor([&] { return start->isEnabled(); }) && heartbeats > 10, "Production UI stays responsive through real driver probes");
    auto* model = qobject_cast<QStandardItemModel*>(encoder->model());
    check(model && model->item(encoder->findData("h264_nvenc"))->isEnabled() && !encoder->itemData(encoder->findData("h264_nvenc"), Qt::ToolTipRole).toString().isEmpty(), "Every compiled encoder remains selectable with validation information");
    encoder->setCurrentIndex(encoder->findData("h264_nvenc"));
    check(!start->isEnabled() && waitFor([&] { return message->text().contains("h264_nvenc") && message->text().contains("Software"); }), "Unavailable exact hardware choice is blocked and explained after isolated probe");
    encoder->setCurrentIndex(encoder->findData("software"));
    check(waitFor([&] { return start->isEnabled(); }), "Returning to Software resets native controls and validates its configuration");
    width->setValue(321); check(!start->isEnabled() && message->text().contains("even"), "Odd dimensions immediately block export"); width->setValue(320);
    pixel->addItem("Unsupported saved 10-bit", "p010le"); pixel->setCurrentIndex(pixel->findData("p010le"));
    check(message->text().contains("Pixel format"), "Saved/selected unsupported pixel combination is reported immediately");
    check(!start->isEnabled(), "Pixel/encoder conflict blocks export"); pixel->setCurrentIndex(pixel->findData("auto"));
    check(waitFor([&] { return start->isEnabled(); }), "Resolving pixel conflict revalidates configuration");
    width->setValue(640); QCoreApplication::processEvents(); width->setValue(642); width->setValue(643);
    check(!start->isEnabled() && message->text().contains("even"), "Edit during queued discovery cannot inherit stale validity");
    check(waitFor([&] { return !start->isEnabled() && message->text().contains("even"); }, 1000), "Invalid current settings remain blocked"); width->setValue(320);
    width->setValue(640);
    // Let actual driver discovery start, then refresh while it is running.
    QTimer::singleShot(220, &dialog, [&] { dialog.findChild<QPushButton*>("exportRefreshCapabilities")->click(); });
    check(waitFor([&] { return start->isEnabled(); }), "Refreshing during discovery cancels old work and completes a new current check");
    width->setValue(320);
    target->setCurrentIndex(target->findData("broad-mp4"));
    dialog.findChild<QSpinBox*>("exportRateNumerator")->setValue(60);
    check(!start->isEnabled() && message->text().contains("30 fps"), "Profile/rate conflict immediately names limit");
    dialog.findChild<QSpinBox*>("exportRateNumerator")->setValue(30);
    check(waitFor([&] { return start->isEnabled(); }), "Broad profile recovers with supported rate");
    check(target->currentData() == "broad-mp4" && encoder->currentData() == "software", "Fixed target resets encoder to its guaranteed software configuration");
    target->setCurrentIndex(target->findData("desktop-mkv"));
    check(dialog.findChild<QLineEdit*>("exportPath")->text().endsWith(".mkv"), "Target change explicitly supplies matching extension");
    dialog.findChild<QLineEdit*>("exportPath")->setText(out.filePath("wrong.mp4"));
    check(!start->isEnabled() && message->text().contains("matroska") && message->text().contains(".mkv"), "Extension conflict is immediate and specific");
    dialog.reject();
    auto stale = settings; stale.container = "webm";
    exporting::ExportDialog staleDialog(graph, stale);
    check(staleDialog.findChild<QLabel*>("exportMessage")->text().contains("webm") && !staleDialog.findChild<QPushButton*>("exportStart")->isEnabled(), "Opening conflicting saved tuple never silently repairs it");
    auto* staleTarget = staleDialog.findChild<QComboBox*>("exportFormat");
    QMetaObject::invokeMethod(staleTarget, "activated", Q_ARG(int, staleTarget->currentIndex()));
    check(!staleDialog.findChild<QLabel*>("exportMessage")->text().contains("webm"), "Reselecting the current target repairs stale codec/container settings");
    staleDialog.reject(); stale = settings; stale.videoEncoder = "obsolete_gpu";
    exporting::ExportDialog unknown(graph, stale);
    check(unknown.findChild<QComboBox*>("exportEncoder")->currentData() == "obsolete_gpu" && !unknown.findChild<QPushButton*>("exportStart")->isEnabled() && unknown.findChild<QLabel*>("exportMessage")->text().contains("obsolete_gpu"), "Stale computer choice remains selected, explained and blocked");
    unknown.reject();
    stale = settings; stale.outputPath.clear(); stale.width = 5000; stale.frameRate = {1000001, 1}; stale.frameRateCustomized = true;
    exporting::ExportDialog staleDimensions(graph, stale);
    check(staleDimensions.findChild<QSpinBox*>("exportWidth")->value() == 5000 && staleDimensions.findChild<QSpinBox*>("exportRateNumerator")->value() == 1000001 &&
        !staleDimensions.findChild<QPushButton*>("exportStart")->isEnabled(), "Out-of-range saved dimensions/rate remain visible even without an output path");
    staleDimensions.reject();
    stale = settings; stale.sampleRate = 44100; stale.channels = 6;
    exporting::ExportDialog staleAudio(graph, stale);
    check(staleAudio.findChild<QLabel*>("exportMessage")->text().contains("off and on") && !staleAudio.findChild<QPushButton*>("exportStart")->isEnabled(), "Stale audio configuration has an available explicit fix");
    auto* audioChoice = staleAudio.findChild<QCheckBox*>("exportAudio"); audioChoice->setChecked(false); audioChoice->setChecked(true);
    check(waitFor([&] { return staleAudio.findChild<QPushButton*>("exportStart")->isEnabled(); }), "Re-enabling the advertised AAC audio choice repairs sample rate/channels");
    staleAudio.reject();
    const QJsonObject report{{"passed", failures.isEmpty()}, {"checkCount", checks}, {"failures", failures}, {"build", build}, {"deviceProbes", discovery["deviceProbes"]},
        {"encoderProbes", probes}, {"smallHevcHardwareProbe", smallHevcProbe}, {"exports", exports}, {"limits", "Physical playback devices, Windows visual scaling and subjective quality/AV sync are unverified. Probes test short exact configurations; long hardware sessions are not certified."}};
    write(out.filePath("capabilities-result.json"), QJsonDocument(report).toJson());
    std::printf("Session 15 compatibility: %d checks, %lld failures\n", checks, static_cast<long long>(failures.size()));
    return failures.isEmpty() ? 0 : 1;
}

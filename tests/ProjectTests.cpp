#include "project/ProjectStore.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <limits>
#include <stdexcept>

using namespace editor::project;
namespace {
QByteArray read(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(("Cannot read " + path).toStdString());
    return file.readAll();
}
void write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) throw std::runtime_error(("Cannot write " + path).toStdString());
}
void changeClip(QJsonObject& root, const std::function<void(QJsonObject&)>& change) {
    auto sequences = root["sequences"].toArray(); auto s = sequences[0].toObject();
    auto tracks = s["tracks"].toArray(); auto t = tracks[0].toObject();
    auto clips = t["clips"].toArray(); auto c = clips[0].toObject();
    change(c); clips[0] = c; t["clips"] = clips; tracks[0] = t; s["tracks"] = tracks; sequences[0] = s; root["sequences"] = sequences;
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 3 && args[1] == "--interrupt-save") {
        auto loaded = ProjectStore::load(args[2]);
        if (!loaded) return 2;
        loaded.project->name = "Interrupted new version";
        const auto error = ProjectStore::save(*loaded.project, args[2], [] {
            std::fputs("PARTIAL_TEMP_READY\n", stdout); std::fflush(stdout);
            for (;;) QThread::msleep(1000); // Parent kills the process while the real QSaveFile is half written.
        });
        return error.isEmpty() ? 0 : 3;
    }
    if (args.size() != 3) return 2;
    QJsonArray failures, checks;
    auto check = [&](bool condition, const QString& name) {
        checks.append(name);
        if (!condition) { failures.append(name); std::fprintf(stderr, "FAIL: %s\n", qPrintable(name)); }
    };
    try {
        const auto fixture = read(QDir(args[1]).filePath("multitrack-v2.veproject"));
        auto loaded = deserialize(fixture);
        check(bool(loaded), "Load multitrack v2 fixture: " + loaded.error);
        if (!loaded) throw std::runtime_error("Fixture failed");
        const auto p = *loaded.project;
        check(p.sequences.size() == 2 && p.sequences[0].tracks.size() == 4 && p.media.size() == 2 && p.titles.size() == 1,
            "Multiple sequences/tracks/media/title records present");
        check(p.media[0].sizeBytes == 9007199254740993LL && p.sequences[1].durationFrames == 9007199254740993LL,
            "Int64 values above 2^53 retain exact precision");
        auto again = deserialize(serialize(p));
        check(again && *again.project == p, "All typed fields and opaque effect parameters round-trip exactly");
        const auto automaticProject = newProject("Automatic end", true);
        const auto automaticRoundTrip = deserialize(serialize(automaticProject));
        check(automaticRoundTrip && *automaticRoundTrip.project == automaticProject,
            "Automatic sequence end mode persists in schema 12");
        auto schema11 = QJsonDocument::fromJson(serialize(p)).object(); schema11["schemaVersion"] = 11;
        auto manualSequences = schema11["sequences"].toArray();
        for (qsizetype i = 0; i < manualSequences.size(); ++i) { auto s = manualSequences[i].toObject(); s.remove("automaticEnd"); manualSequences[i] = s; }
        schema11["sequences"] = manualSequences;
        const auto migrated11 = deserialize(QJsonDocument(schema11).toJson());
        check(migrated11 && migrated11.migratedFrom == 11 && *migrated11.project == p,
            "Older projects migrate to manual mode while preserving every existing duration");
        auto badAutomatic = automaticProject; badAutomatic.sequences[0].durationFrames = 90;
        check(validate(badAutomatic).contains("automatic duration"), "Automatic mode rejects a duration beyond the last clip");
        auto withPrimary = p; bool foundPrimary = false;
        for (const auto& media : withPrimary.media) for (const auto& stream : media.streams) if (stream.kind == "video") {
            withPrimary.sequences[0].primaryVideoMediaId = media.id; withPrimary.sequences[0].primaryVideoStreamIndex = stream.index; foundPrimary = true; break;
        }
        const auto primaryRoundTrip = deserialize(serialize(withPrimary));
        check(foundPrimary && primaryRoundTrip && *primaryRoundTrip.project == withPrimary,
            "Primary video media and stream identity round-trip exactly");
        auto schema9 = QJsonDocument::fromJson(serialize(p)).object(); schema9["schemaVersion"] = 9;
        auto oldSequences = schema9["sequences"].toArray();
        for (qsizetype i = 0; i < oldSequences.size(); ++i) { auto sequence = oldSequences[i].toObject(); sequence.remove("primaryVideoMediaId"); sequence.remove("primaryVideoStreamIndex"); oldSequences[i] = sequence; }
        schema9["sequences"] = oldSequences; const auto migrated9 = deserialize(QJsonDocument(schema9).toJson());
        check(migrated9 && migrated9.migratedFrom == 9 && migrated9.project->sequences[0].primaryVideoMediaId.isEmpty() &&
            migrated9.project->sequences[0].primaryVideoStreamIndex == -1,
            "Schema 9 migration leaves Primary Video unset for the existing sequence-FPS fallback");
        auto invalidPrimary = withPrimary; invalidPrimary.sequences[0].primaryVideoMediaId = newId();
        check(validate(invalidPrimary).contains("missing media reference"), "Missing primary media reference is rejected");
        invalidPrimary = withPrimary; invalidPrimary.sequences[0].primaryVideoStreamIndex = 999999;
        check(!validate(invalidPrimary).isEmpty(), "Primary selection cannot reference a non-video or nonexistent stream");
        auto repeated = p;
        for (int i = 0; i < 100; ++i) repeated = *deserialize(serialize(repeated)).project;
        check(repeated == p, "100 serialization cycles preserve IDs, metadata, timing, title/audio/export/effects");
        auto v1 = deserialize(read(QDir(args[1]).filePath("multitrack-v1.veproject")));
        auto expected = p; expected.sequences[1].durationFrames = 9007199254740991LL;
        check(v1 && v1.migratedFrom == 1 && *v1.project == expected, "v1 fixture migrates explicitly to v2 without loss");
        check(v1 && deserialize(serialize(*v1.project)).migratedFrom == 0, "Migrated project writes current schema");
        auto reject = [&](const QString& name, const QString& field, const std::function<void(QJsonObject&)>& mutate) {
            auto root = QJsonDocument::fromJson(fixture).object(); mutate(root);
            const auto result = deserialize(QJsonDocument(root).toJson());
            check(!result && result.error.contains(field), name + " has useful field error: " + result.error);
        };
        reject("Future schema", "schemaVersion", [](auto& o) { o["schemaVersion"] = 99; });
        reject("Unknown old schema", "schemaVersion", [](auto& o) { o["schemaVersion"] = 0; });
        reject("Wrong format", "format", [](auto& o) { o["format"] = "other"; });
        reject("Missing field", "name", [](auto& o) { o.remove("name"); });
        reject("Unknown field", "newProperty", [](auto& o) { o["newProperty"] = true; });
        reject("Wrong scalar type", "name", [](auto& o) { o["name"] = 1; });
        reject("Duplicate ID", "duplicate", [](auto& o) { o["id"] = o["activeSequenceId"]; });
        reject("Missing active sequence", "activeSequenceId", [](auto& o) { o["activeSequenceId"] = newId(); });
        reject("Broken media reference", "mediaId", [](auto& o) { changeClip(o, [](auto& c) { c["mediaId"] = newId(); }); });
        reject("Wrong stream index", "streamIndex", [](auto& o) { changeClip(o, [](auto& c) { c["streamIndex"] = 99; }); });
        reject("Negative clip position", "startFrame", [](auto& o) { changeClip(o, [](auto& c) { c["startFrame"] = "-1"; }); });
        reject("Int64 overflow", "durationFrames", [](auto& o) { changeClip(o, [](auto& c) { c["durationFrames"] = "9223372036854775808"; }); });
        reject("Numeric frame time in v2", "startFrame", [](auto& o) { changeClip(o, [](auto& c) { c["startFrame"] = 10; }); });
        reject("Noncanonical integer", "startFrame", [](auto& o) { changeClip(o, [](auto& c) { c["startFrame"] = "0010"; }); });
        reject("Clip outside sequence", "sequence duration", [](auto& o) { changeClip(o, [](auto& c) { c["durationFrames"] = "9223372036854775807"; }); });
        reject("Source out of bounds", "source range", [](auto& o) { changeClip(o, [](auto& c) { c["sourceInTicks"] = "9000001"; }); });
        reject("Wrong clip/track kind", "track kind", [](auto& o) { changeClip(o, [](auto& c) { c["kind"] = "audio"; }); });
        reject("Invalid audio gain", "gain", [](auto& o) { changeClip(o, [](auto& c) { c["gain"] = -1; }); });
        reject("Missing title reference", "title reference", [](auto& o) { o["titles"] = QJsonArray{}; });
        reject("Duplicate effect ID", "duplicate", [](auto& o) { const auto id = o["id"]; changeClip(o, [id](auto& c) {
            auto effects = c["effects"].toArray(); auto e = effects[0].toObject(); e["id"] = id; effects[0] = e; c["effects"] = effects;
        }); });
        reject("Invalid rational", "denominator", [](auto& o) { auto e = o["exportSettings"].toObject(); auto r = e["frameRate"].toObject(); r["denominator"] = "0"; e["frameRate"] = r; o["exportSettings"] = e; });
        reject("Nonintegral v1 timing", "v1 time", [](auto& o) { o["schemaVersion"] = 1; });
        check(!deserialize("{broken").error.isEmpty() && !deserialize("[]"), "Malformed/truncated JSON and nonobject root fail");
        check(!deserialize(QByteArray(MaxDocumentBytes + 1, ' ')), "Oversized project rejected before parsing");
        auto invalid = p; invalid.sequences[0].frameRate = {60, 2};
        check(!validate(invalid).isEmpty(), "Unreduced rate rejected");
        invalid = p; invalid.sequences[0].tracks[0].gain = std::numeric_limits<double>::infinity();
        check(!validate(invalid).isEmpty(), "Nonfinite model values rejected before saving");
        const Rational ntsc{30000, 1001};
        check(framesToSamples(1, ntsc, 48000, Rounding::Floor) == 1601 &&
            framesToSamples(1, ntsc, 48000, Rounding::Ceil) == 1602 &&
            framesToSamples(1, ntsc, 48000, Rounding::Nearest) == 1602, "29.97 frame/sample rounding policies");
        check(framesToSamples(30000, ntsc, 48000, Rounding::Floor) == 48048000 &&
            samplesToFrames(48048000, ntsc, 48000, Rounding::Nearest) == 30000, "Exact rational conversion over 1001 seconds");
        check(framesToSamples(1, {768, 1}, 44100, Rounding::Floor) == 57 &&
            framesToSamples(1, {768, 1}, 44100, Rounding::Ceil) == 58, "44.1 kHz fractional sample conversion");
        check(samplesToFrames(800, {30, 1}, 48000, Rounding::Nearest) == 1 &&
            samplesToFrames(800, {30, 1}, 48000, Rounding::Floor) == 0, "Nearest half-sample/frame ties round upward");
        check(!framesToSamples(-1, ntsc, 48000, Rounding::Nearest) &&
            !framesToSamples(1, {0, 1}, 48000, Rounding::Nearest) &&
            !framesToSamples(1, ntsc, 0, Rounding::Nearest), "Negative time/invalid rate/sample rate rejected");
        check(!framesToSamples(std::numeric_limits<qint64>::max(), {1, 1}, 48000, Rounding::Floor), "Conversion overflow is reported");
        check(samplesToFrames(std::numeric_limits<qint64>::max(), {48000, 1}, 48000, Rounding::Nearest) == std::numeric_limits<qint64>::max(),
            "128-bit intermediate preserves a representable large result");

        QDir().mkpath(args[2]);
        QTemporaryDir temp(QDir(args[2]).filePath("store-XXXXXX"));
        if (!temp.isValid()) throw std::runtime_error("Temporary directory unavailable");
        const auto base = temp.path(); QDir(base).mkpath("media"); QDir(base).mkpath("save-as");
        write(QDir(base).filePath("original.veproject"), fixture);
        write(QDir(base).filePath("media/example.mp4"), "synthetic existence fixture, not a decoded video");
        auto online = ProjectStore::load(QDir(base).filePath("original.veproject"));
        check(online && online.offlineMediaIds == QStringList{p.media[1].id}, "Available/missing media resolved relative to project, with IDs preserved");
        const auto runtime = *online.project;
        const auto target = QDir(base).filePath("roundtrip.veproject");
        check(!ProjectStore::save(p, target).isEmpty() && !QFile::exists(target), "Relative runtime paths rejected rather than guessed during Save As");
        check(ProjectStore::save(runtime, target).isEmpty(), "First atomic save succeeds");
        check(!QFile::exists(target + ".bak"), "First save does not fabricate a backup");
        check(ProjectStore::load(target).project == online.project, "Disk round-trip preserves resolved model values");
        const auto originalBytes = read(target);
        auto edited = runtime; edited.name = "Second save";
        check(ProjectStore::save(edited, target).isEmpty(), "Second atomic save succeeds");
        check(read(target + ".bak") == originalBytes && ProjectStore::load(target).project == std::optional<Project>(edited), "Backup is byte-exact previous good primary");
        const auto currentBytes = read(target);
        check(ProjectStore::autosave(runtime, target + ".autosave").isEmpty() &&
            read(target) == currentBytes && read(target + ".bak") == originalBytes, "Autosave writes independent snapshot without modifying primary/backup");
        check(ProjectStore::load(target + ".autosave").project == online.project, "Autosave is a validated recoverable project");
        const auto saveAs = QDir(base).filePath("save-as/rebased.veproject");
        check(ProjectStore::save(runtime, saveAs).isEmpty() && ProjectStore::load(saveAs).project == online.project,
            "Save As rebases relative media/output paths without changing resolved references");
        const auto sourceBytes = read(runtime.media[0].path);
        check(!ProjectStore::save(runtime, runtime.media[0].path).isEmpty() && read(runtime.media[0].path) == sourceBytes,
            "Saving over source media rejected without modifying source");
        check(!ProjectStore::autosave(runtime, runtime.media[0].path).isEmpty() && read(runtime.media[0].path) == sourceBytes,
            "Autosaving over source media rejected without modifying source");
        auto protectedBackup = runtime; protectedBackup.media[0].path = target + ".bak";
        check(!ProjectStore::save(protectedBackup, target).isEmpty() && read(target) == currentBytes && read(target + ".bak") == originalBytes,
            "Backup destination cannot overwrite a source media reference");
        check(!ProjectStore::save(invalid, target).isEmpty() && read(target) == currentBytes && read(target + ".bak") == originalBytes,
            "Invalid model fails before touching primary or backup");
        {
            QLockFile lock(target + ".lock"); check(lock.tryLock(0), "Test lock acquired");
            check(!ProjectStore::save(runtime, target).isEmpty() && read(target) == currentBytes, "Concurrent writer rejected");
        }
        const auto broken = QDir(base).filePath("corrupt.veproject");
        write(broken, "{truncated"); write(broken + ".bak", originalBytes);
        check(!ProjectStore::save(runtime, broken).isEmpty() && read(broken) == "{truncated" && read(broken + ".bak") == originalBytes,
            "Invalid primary cannot replace a good backup");
        const auto blocked = QDir(base).filePath("blocked.veproject");
        write(blocked, originalBytes); QDir().mkpath(blocked + ".bak");
        check(!ProjectStore::save(edited, blocked).isEmpty() && read(blocked) == originalBytes, "Backup failure leaves primary unchanged");
        check(!ProjectStore::save(runtime, QDir(base).filePath("absent/sub/project.veproject")).isEmpty(), "Missing/unwritable destination fails usefully");
        // Use the production save path in another process, then kill it mid-write.
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--interrupt-save", target});
        QByteArray marker;
        bool started = child.waitForStarted(5000);
        for (int i = 0; started && i < 10 && !marker.contains("PARTIAL_TEMP_READY"); ++i) {
            child.waitForReadyRead(500); marker += child.readAllStandardOutput();
        }
        const bool interrupted = marker.contains("PARTIAL_TEMP_READY");
        child.kill(); const bool stopped = child.waitForFinished(5000);
        check(interrupted && stopped, "Process killed after partial project temporary write before atomic commit");
        check(read(target) == currentBytes && read(target + ".bak") == currentBytes && ProjectStore::load(target),
            "Interrupted save keeps primary and previous-good backup valid and byte-exact");
        check(ProjectStore::save(runtime, target).isEmpty(), "Next save recovers stale lock after process termination");
        QFile::remove(runtime.media[0].path);
        auto moved = ProjectStore::load(target);
        check(moved && moved.offlineMediaIds.size() == 2 && moved.project == online.project,
            "Moved/missing sources do not change edit records or source paths");
    } catch (const std::exception& error) { failures.append(QString::fromUtf8(error.what())); }
    const auto bytes = QJsonDocument(QJsonObject{{"passed", failures.isEmpty()}, {"checks", checks}, {"failures", failures},
        {"evidenceLevel", "Automated native model/filesystem/process-termination tests; no human UI or media decode acceptance"}}).toJson();
    try { QDir().mkpath(args[2]); write(QDir(args[2]).filePath("project-result.json"), bytes); }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
    std::printf("Project tests: %lld checks, %lld failures\n", static_cast<long long>(checks.size()), static_cast<long long>(failures.size()));
    return failures.isEmpty() ? 0 : 1;
}

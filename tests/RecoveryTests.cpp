#include "MainWindow.h"
#include "Diagnostics.h"
#include "project/Recovery.h"
#include "media/Inspection.h"
#include "timeline/TimelineWidget.h"
#include "timeline/TimelineCanvas.h"
#include <QApplication>
#include <QAction>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <cstdio>
#include <stdexcept>

using namespace editor;
namespace {
QByteArray read(const QString& path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
void write(const QString& path, const QByteArray& data) { QFile f(path); if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) throw std::runtime_error(qPrintable("Cannot write " + path)); }
bool waitFor(const std::function<bool()>& predicate, int ms = 15000) {
    QElapsedTimer clock; clock.start();
    while (clock.elapsed() < ms) { if (predicate()) return true; QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1); }
    return false;
}
void answer(const QString& objectName, int result) {
    QTimer::singleShot(0, [objectName, result] {
        for (auto* w : QApplication::topLevelWidgets()) if (auto* b = qobject_cast<QMessageBox*>(w); b && b->objectName() == objectName) b->done(result);
    });
}
void dismissError() { answer("errorDialog", QMessageBox::Ok); }
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/segoeui.ttf"));
    const auto args = app.arguments(); if (args.size() != 3) return 2;
    if (args[1] == "--crash-session" || args[1] == "--restart-session") {
        QDir data(args[2]); QDir().mkpath(data.filePath("logs"));
        QSettings settings(data.filePath("settings.ini"), QSettings::IniFormat);
        Diagnostics diagnostics(data.filePath("logs/test.log")); MainWindow window(settings, diagnostics); window.show();
        auto* timeline = window.findChild<timeline::TimelineWidget*>("timelineArea");
        if (args[1] == "--crash-session") {
            if (!window.openProjectPath(data.filePath("primary.veproject"))) return 3;
            if (!timeline->execute(timeline::RenameProject{"Earlier snapshot"}).isEmpty()) return 4;
            window.autosaveNow();
            if (!timeline->execute(timeline::RenameProject{"Latest recovered edit"}).isEmpty()) return 5;
            auto* timer = window.findChild<QTimer*>("autosaveTimer");
            if (!timer || timer->interval() != 120000) return 6;
            timer->setInterval(20); // Exercise the production timeout wiring without waiting two minutes.
            QTimer::singleShot(60, &window, [] { std::fputs("RECOVERY_READY\n", stdout); std::fflush(stdout); });
            return app.exec();
        }
        bool offered = false;
        QTimer automation;
        automation.setInterval(10);
        QObject::connect(&automation, &QTimer::timeout, [&] {
            for (auto* w : QApplication::topLevelWidgets()) if (w->objectName() == "recoveryDialog") {
                offered = true; automation.stop();
                w->grab().save(data.filePath("recovery-dialog.png"));
                w->findChild<QPushButton*>("recoverSelectedButton")->click(); return;
            }
        }); automation.start();
        window.offerRecovery();
        const bool ok = offered && window.project().name == "Latest recovered edit" && window.isProjectDirty() && window.projectPath().isEmpty();
        write(data.filePath("restart-result.json"), QJsonDocument(QJsonObject{{"passed", ok}, {"offered", offered}, {"name", window.project().name}}).toJson());
        return ok ? 0 : 7;
    }
    QDir root(args[1]), output(args[2]); QDir().mkpath(output.absolutePath());
    QJsonArray checks, failures;
    auto check = [&](bool ok, const QString& name) { checks.append(name); if (!ok) { failures.append(name); std::fprintf(stderr, "FAIL: %s\n", qPrintable(name)); } };
    try {
        QTemporaryDir temporary(output.filePath("run-XXXXXX")); if (!temporary.isValid()) throw std::runtime_error("No temporary directory");
        QDir data(temporary.path()); QDir().mkpath(data.filePath("logs"));
        auto primary = *project::ProjectStore::load(root.filePath("fixtures/projects/multitrack-v3.veproject")).project;
        primary.name = "Last good save"; const auto primaryPath = data.filePath("primary.veproject");
        check(project::ProjectStore::save(primary, primaryPath).isEmpty(), "Create primary");
        primary.name = "Previous good save"; check(project::ProjectStore::save(primary, primaryPath).isEmpty(), "Create backup");
        const auto original = read(primaryPath), backup = read(primaryPath + ".bak");
        QProcess crash; crash.start(app.applicationFilePath(), {"--crash-session", data.path()});
        check(crash.waitForStarted(5000), "Start real MainWindow in a child process");
        QByteArray marker;
        QElapsedTimer timeout; timeout.start();
        while (timeout.elapsed() < 10000 && !marker.contains("RECOVERY_READY") && crash.state() != QProcess::NotRunning) {
            crash.waitForReadyRead(100); marker += crash.readAllStandardOutput();
        }
        check(marker.contains("RECOVERY_READY"), "Production autosave timer fires after the latest edit");
        check(project::recoveryCandidates(data.filePath("recovery")).isEmpty(), "Live editor's snapshots excluded from recovery");
        crash.kill(); check(crash.waitForFinished(5000), "Force termination with unsaved work");
        auto candidates = project::recoveryCandidates(data.filePath("recovery"));
        check(candidates.size() == 1 && candidates[0].name == "Latest recovered edit", "Dead-process lease releases; discover latest atomic snapshot");
        check(read(primaryPath) == original && read(primaryPath + ".bak") == backup, "Crash/autosaves leave last good primary and backup byte-exact");
        QProcess restart; restart.start(app.applicationFilePath(), {"--restart-session", data.path()});
        check(restart.waitForStarted(5000) && restart.waitForFinished(15000) && restart.exitCode() == 0,
            "Restart offers production recovery dialog and recovers latest work as an unsaved document");
        check(QJsonDocument::fromJson(read(data.filePath("restart-result.json"))).object()["passed"].toBool(), "Restart result confirms offer and latest edit");
        QFile::copy(data.filePath("recovery-dialog.png"), output.filePath("recovery-dialog.png"));
        auto expectedRecovered = primary; expectedRecovered.name = "Latest recovered edit";
        check(!candidates.isEmpty() && project::ProjectStore::load(candidates[0].path).project == std::optional<project::Project>(expectedRecovered), "Crashed/recovered multitrack snapshot retains all timing, titles, audio fades and effects");
        check(read(primaryPath) == original && read(primaryPath + ".bak") == backup, "Recovery leaves primary/backup intact");

        QSettings settings(data.filePath("ui.ini"), QSettings::IniFormat);
        Diagnostics diagnostics(data.filePath("logs/test.log")); MainWindow window(settings, diagnostics); window.show();
        auto* timeline = window.findChild<timeline::TimelineWidget*>("timelineArea");
        const auto snapshot = candidates.isEmpty() ? QString{} : candidates[0].path;
        check(window.recoverProjectPath(snapshot), "Recover abandoned snapshot using production load");
        check(project::recoveryCandidates(window.recoveryDirectory()).isEmpty(), "Recovered snapshot held exclusively while being edited");
        check(window.saveProjectPath(data.filePath("recovered-copy.veproject")) && !QFileInfo::exists(snapshot), "Save As clears only the owned resolved snapshot");
        check(read(primaryPath) == original, "Save recovered copy preserves original save");

        const auto corrupt = data.filePath("recovery/broken.veproject.autosave"); write(corrupt, "{truncated");
        const auto before = window.project(); const auto beforePath = window.projectPath();
        dismissError(); check(!window.recoverProjectPath(corrupt) && window.project() == before && window.projectPath() == beforePath && !window.isProjectDirty(), "Invalid recovery preserves current model/path/dirty state");
        check(read(corrupt) == "{truncated" && read(primaryPath) == original && read(primaryPath + ".bak") == backup, "Failed recovery retains corrupt evidence and last good save");
        auto invalidCandidates = project::recoveryCandidates(window.recoveryDirectory());
        check(invalidCandidates.size() == 1 && !invalidCandidates[0].error.isEmpty(), "Invalid snapshot listed with diagnostic, never silently deleted");
        QTimer::singleShot(0, [&] { for (auto* w : QApplication::topLevelWidgets()) if (w->objectName() == "recoveryDialog") w->findChild<QPushButton*>("recoveryLaterButton")->click(); });
        window.offerRecovery(); check(QFileInfo::exists(corrupt), "Later keeps unresolved snapshots");
        QTimer driver; driver.setInterval(5); int discardDialogs = 0;
        QObject::connect(&driver, &QTimer::timeout, [&] {
            for (auto* w : QApplication::topLevelWidgets()) if (w->objectName() == "recoveryDialog") {
                ++discardDialogs;
                answer("discardRecoveryDialog", discardDialogs == 1 ? QMessageBox::Cancel : QMessageBox::Discard);
                w->findChild<QPushButton*>("discardRecoveryButton")->click(); return;
            }
        }); driver.start(); window.offerRecovery(); driver.stop();
        check(discardDialogs == 2 && !QFileInfo::exists(corrupt), "Recovery discard requires explicit confirmation; Cancel preserves snapshot");

        check(timeline->execute(timeline::RenameProject{"Unsaved again"}).isEmpty(), "Create unsaved edit"); window.autosaveNow();
        const auto ownedEntries = QDir(window.recoveryDirectory()).entryList({"*.autosave"}, QDir::Files);
        check(ownedEntries.size() == 1, "Named projects autosave into startup-discoverable app data");
        if (!ownedEntries.isEmpty()) {
            const auto autosavePath = QDir(window.recoveryDirectory()).filePath(ownedEntries[0]);
            const auto goodSnapshot = read(autosavePath);
            QLockFile blocking(autosavePath + ".lock"); check(blocking.tryLock(0), "Acquire autosave writer lock for failure fixture");
            timeline->execute(timeline::RenameProject{"Newer unsaved work"}); window.autosaveNow();
            check(read(autosavePath) == goodSnapshot && window.isProjectDirty(), "Failed autosave keeps last recoverable snapshot byte-exact and newer edits dirty");
            timeline->undo();
        }
        answer("unsavedProjectDialog", QMessageBox::Cancel); window.close();
        check(window.isVisible() && window.isProjectDirty() && QDir(window.recoveryDirectory()).entryList({"*.autosave"}, QDir::Files) == ownedEntries, "Cancel Close preserves dirty work and snapshot");
        answer("unsavedProjectDialog", QMessageBox::Cancel); window.newDocument();
        check(window.project().name == "Unsaved again", "Cancel New preserves current work");
        answer("unsavedProjectDialog", QMessageBox::Cancel); check(!window.openProjectPath(primaryPath) && window.project().name == "Unsaved again", "Cancel Open preserves current work");
        dismissError(); check(!window.saveProjectPath(data.filePath("absent/project.veproject")) && window.isProjectDirty(), "Failed explicit save retains dirty state");
        check(QDir(window.recoveryDirectory()).entryList({"*.autosave"}, QDir::Files) == ownedEntries, "Failed save preserves recovery snapshot");
        answer("unsavedProjectDialog", QMessageBox::Discard); window.newDocument();
        check(window.project().name == "Untitled" && window.isProjectDirty() && QDir(window.recoveryDirectory()).entryList({"*.autosave"}, QDir::Files).isEmpty(), "Discard on New removes abandoned owned snapshot");
        window.autosaveNow();
        answer("unsavedProjectDialog", QMessageBox::Discard); check(window.openProjectPath(primaryPath), "Discard on Open switches safely");
        check(QDir(window.recoveryDirectory()).entryList({"*.autosave"}, QDir::Files).isEmpty(), "Discard on Open cleans the previous snapshot");
        timeline->execute(timeline::RenameProject{"Undo autosaved edit"}); window.autosaveNow();
        window.findChild<QAction*>("timelineUndo")->trigger();
        window.autosaveNow();
        check(!window.isProjectDirty() && QDir(window.recoveryDirectory()).entryList({"*.autosave"}, QDir::Files).isEmpty(), "Undo to last saved state removes obsolete snapshot");

        const auto retained = data.filePath("retained.veproject"); auto versions = project::newProject();
        for (int i = 0; i < 6; ++i) { versions.name = QString("Revision %1").arg(i); check(project::ProjectStore::save(versions, retained).isEmpty(), "Save retained revision " + QString::number(i)); }
        check(project::ProjectStore::load(retained).project->name == "Revision 5" && project::ProjectStore::load(retained + ".bak").project->name == "Revision 4" && project::ProjectStore::load(retained + ".bak.2").project->name == "Revision 3", "Two backups retain exact two preceding good saves");
        check(!QFileInfo::exists(retained + ".bak.3"), "Retention bounded to two backups");
        auto bytesBefore = read(retained), secondBefore = read(retained + ".bak.2");
        auto colliding = versions; project::Media protectedMedia; protectedMedia.id = project::newId(); protectedMedia.name = "Protected"; protectedMedia.path = retained + ".bak.2"; colliding.media.append(protectedMedia);
        check(!project::ProjectStore::save(colliding, retained).isEmpty() && read(retained) == bytesBefore && read(retained + ".bak.2") == secondBefore, "Retained backup cannot overwrite a source reference");
        write(retained + ".bak", "{broken");
        check(!project::ProjectStore::save(versions, retained).isEmpty() && read(retained) == bytesBefore && read(retained + ".bak.2") == secondBefore, "Corrupt backup cannot destroy the second good backup");
        check(window.openProjectPath(retained + ".bak.2") && window.projectPath().isEmpty() && window.isProjectDirty(), "Second retained backup opens safely as unsaved recovery");
        answer("unsavedProjectDialog", QMessageBox::Discard); window.newDocument();
        answer("unsavedProjectDialog", QMessageBox::Discard); window.openProjectPath(primaryPath);

        for (int schema : {1, 2, 3}) {
            const auto fixture = root.filePath(QString("fixtures/projects/multitrack-v%1.veproject").arg(schema));
            auto loaded = project::ProjectStore::load(fixture);
            check(loaded && loaded.migratedFrom == schema, QString("Migration fixture schema %1").arg(schema));
            const auto copy = data.filePath(QString("migration-%1.veproject").arg(schema)); write(copy, read(fixture));
            auto local = project::ProjectStore::load(copy); const auto oldBytes = read(copy);
            check(local && project::ProjectStore::save(*local.project, copy).isEmpty() && read(copy + ".bak") == oldBytes && project::ProjectStore::load(copy).project == local.project,
                QString("Schema %1 migrates without losing values; predecessor remains byte-exact backup").arg(schema));
        }

        const auto inspected = media::inspect(root.filePath("fixtures/generated/session-5/flash-beep.mp4"));
        const auto sound = media::inspect(root.filePath("fixtures/generated/session-7/soundtrack.wav"));
        check(inspected.error.isEmpty() && sound.error.isEmpty(), "Probe representative video/audio originals");
        auto relink = project::newProject("Session 9 recovery and moved originals");
        auto movie = inspected.media, audio = sound.media;
        movie.path = data.filePath("old/flash-beep.mp4"); audio.path = data.filePath("old/soundtrack.wav"); relink.media = {movie, audio};
        auto& seq = relink.sequences[0]; seq.durationFrames = 120;
        project::Title title; title.id = project::newId(); title.text = "Recovered title"; title.fontSize = 36; title.x = 0.3; title.shadow = true;
        relink.titles.append(title);
        project::Track titleTrack; titleTrack.id = project::newId(); titleTrack.name = "Titles"; titleTrack.kind = "title";
        project::Clip titleClip; titleClip.id = project::newId(); titleClip.name = title.text; titleClip.kind = "title"; titleClip.titleId = title.id; titleClip.startFrame = 4; titleClip.durationFrames = 60;
        titleTrack.clips.append(titleClip); seq.tracks.append(titleTrack);
        for (const auto& mediaRecord : relink.media) for (const auto& stream : mediaRecord.streams) {
            project::Clip clip; clip.id = project::newId(); clip.name = "Preserve trimmed edit"; clip.mediaId = mediaRecord.id; clip.kind = stream.kind; clip.streamIndex = stream.index;
            clip.startFrame = 8; clip.durationFrames = 30; clip.sourceInTicks = project::framesToTicks(5, seq.frameRate, stream.timeBase, project::Rounding::Nearest).value_or(0);
            clip.sourceDurationTicks = project::framesToTicks(30, seq.frameRate, stream.timeBase, project::Rounding::Nearest).value_or(0);
            if (stream.kind == "audio") { clip.gain = 0.5; clip.fadeInFrames = 4; clip.fadeOutFrames = 7; }
            seq.tracks[stream.kind == "video" ? 0 : 1].clips.append(clip);
        }
        const auto offline = data.filePath("offline.veproject"); check(project::ProjectStore::save(relink, offline).isEmpty(), "Create recovery fixture with edited offline media");
        check(window.openProjectPath(offline) && !window.mediaBusy(), "Offline project opens without probing missing sources");
        auto* bin = window.findChild<QTreeWidget*>("mediaBin");
        check(bin->topLevelItem(0)->text(1) == "Offline" && timeline->canvas()->clipCaption(seq.tracks[0].clips[0]).startsWith("OFFLINE"), "Bin and production timeline captions identify offline sources");
        QDir().mkpath(data.filePath("old")); QFile::copy(inspected.media.path, movie.path);
        QMetaObject::invokeMethod(window.findChild<QTimer*>("mediaAvailabilityTimer"), "timeout");
        check(bin->topLevelItem(0)->text(1) == "Online" && !timeline->canvas()->clipCaption(seq.tracks[0].clips[0]).startsWith("OFFLINE") && window.project() == relink, "Availability refresh detects a returned source without changing project edits");
        QFile::remove(movie.path);
        QMetaObject::invokeMethod(window.findChild<QTimer*>("mediaAvailabilityTimer"), "timeout");
        check(bin->topLevelItem(0)->text(1) == "Offline" && timeline->canvas()->clipCaption(seq.tracks[0].clips[0]).startsWith("OFFLINE") && window.project() == relink, "Availability refresh detects source disappearance without changing edits");
        auto offlineImage = timeline->canvas()->viewport()->grab().toImage();
        check(!offlineImage.isNull() && offlineImage.save(output.filePath("offline-timeline.png")), "Render offline timeline with production painter");
        QDir().mkpath(data.filePath("moved/subfolder"));
        check(QFile::copy(inspected.media.path, data.filePath("moved/flash-beep.mp4")) && QFile::copy(sound.media.path, data.filePath("moved/subfolder/soundtrack.wav")), "Prepare disposable moved originals in nested folder");
        QElapsedTimer submit; submit.start(); check(window.relinkOfflineFolder(data.filePath("moved")) && submit.elapsed() < 100, "Folder matching/probing submitted asynchronously");
        check(waitFor([&] { return !window.mediaBusy(); }), "Folder relink finishes");
        check(window.project().sequences == relink.sequences && window.project().titles == relink.titles && window.project().exportSettings == relink.exportSettings && window.project().media[0].id == movie.id && window.project().media[1].id == audio.id, "Bulk relink preserves every edit/time/fade/title/setting and stable media IDs");
        check(window.project().media[0].path == data.filePath("moved/flash-beep.mp4") && window.project().media[1].path == data.filePath("moved/subfolder/soundtrack.wav"), "Unique originals found recursively");
        check(bin->topLevelItem(0)->text(1) != "Offline" && !timeline->canvas()->clipCaption(seq.tracks[0].clips[0]).startsWith("OFFLINE"), "Relink clears offline bin/timeline states");
        const auto linked = window.project();
        window.findChild<QAction*>("timelineUndo")->trigger(); window.findChild<QAction*>("timelineUndo")->trigger();
        check(window.project() == relink, "Undo both relinks restores original paths and all edits");
        window.findChild<QAction*>("timelineRedo")->trigger(); window.findChild<QAction*>("timelineRedo")->trigger();
        check(window.project() == linked, "Redo restores exact relinked model");
        check(window.saveProjectPath(data.filePath("linked.veproject")) && window.openProjectPath(data.filePath("linked.veproject")) && window.project() == linked, "Relinked edits survive atomic Save/reopen");
        check(waitFor([&] { return !window.mediaBusy(); }), "Reopened aids finish");
        window.openProjectPath(offline);
        QDir().mkpath(data.filePath("moved/duplicate")); QFile::copy(inspected.media.path, data.filePath("moved/duplicate/flash-beep.mp4"));
        check(window.relinkOfflineFolder(data.filePath("moved")) && waitFor([&] { return !window.mediaBusy(); }), "Ambiguous folder relink completes with safe partial success");
        check(window.project().media[0] == movie && window.project().media[1].path == linked.media[1].path && window.project().sequences == relink.sequences, "Ambiguous name skipped while unambiguous source succeeds; edits unchanged");
        check(window.relinkMediaPath(movie.id, data.filePath("moved/flash-beep.mp4")) && waitFor([&] { return !window.mediaBusy(); }) && window.project().media[0].path == linked.media[0].path, "Explicit selected-file relink resolves ambiguity");
        const auto successful = window.project();
        check(window.relinkMediaPath(movie.id, sound.media.path) && waitFor([&] { return !window.mediaBusy(); }) && window.project() == successful, "Incompatible replacement rejects without changing edits");
        window.saveProjectPath(data.filePath("linked.veproject")); window.openProjectPath(offline);
        check(window.relinkOfflineFolder(data.filePath("moved")), "Start cancellable folder search"); window.cancelImport();
        check(waitFor([&] { return !window.mediaBusy(); }) && window.project() == relink, "Cancellation suppresses stale folder/probe results");

        // Keep a prepared user fixture outside temporary test storage.
        QDir generated(root.filePath("fixtures/generated/session-9")); QDir().mkpath(generated.absolutePath());
        auto fixture = relink; fixture.media[0].path = generated.filePath("old/flash-beep.mp4"); fixture.media[1].path = generated.filePath("old/soundtrack.wav");
        check(project::ProjectStore::save(fixture, generated.filePath("recovery-offline.veproject")).isEmpty(), "Write durable recovery fixture");
        QDir().mkpath(generated.filePath("moved/subfolder"));
        if (!QFileInfo::exists(generated.filePath("moved/flash-beep.mp4"))) QFile::copy(inspected.media.path, generated.filePath("moved/flash-beep.mp4"));
        if (!QFileInfo::exists(generated.filePath("moved/subfolder/soundtrack.wav"))) QFile::copy(sound.media.path, generated.filePath("moved/subfolder/soundtrack.wav"));
        window.autosaveNow(); answer("unsavedProjectDialog", QMessageBox::Discard); window.close();
        check(!window.isVisible() && project::recoveryCandidates(window.recoveryDirectory()).isEmpty(), "Discard Close clears owned snapshot");
    } catch (const std::exception& e) { failures.append(QString::fromUtf8(e.what())); }
    write(output.filePath("recovery-result.json"), QJsonDocument(QJsonObject{{"passed", failures.isEmpty()}, {"checkCount", checks.size()}, {"checks", checks}, {"failures", failures},
        {"evidenceLevel", "Native atomic persistence, killed/restarted production MainWindow processes, offscreen recovery dialogs and background folder relinking; no computer use or human visual acceptance"}}).toJson());
    std::printf("Recovery tests: %lld checks, %lld failures\n", static_cast<long long>(checks.size()), static_cast<long long>(failures.size()));
    return failures.isEmpty() ? 0 : 1;
}

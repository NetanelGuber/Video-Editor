#include "MainWindow.h"
#include "Diagnostics.h"
#include "BuildInfo.h"
#include "timeline/TimelineWidget.h"
#include "export/ExportDialog.h"
#include "project/Recovery.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>

using namespace editor;
namespace {
bool waitFor(const std::function<bool()>& ready, int timeout = 90000) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < timeout) {
        if (ready()) return true;
        QApplication::processEvents(); QThread::msleep(2);
    }
    return false;
}
QByteArray read(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
bool write(const QString& path, const QByteArray& bytes) {
    QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
QJsonArray modules() {
    QJsonArray result;
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) return result;
    MODULEENTRY32W row{}; row.dwSize = sizeof(row);
    if (Module32FirstW(snapshot, &row)) do { result.append(QString::fromWCharArray(row.szExePath)); } while (Module32NextW(snapshot, &row));
    CloseHandle(snapshot); return result;
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    if (app.arguments().size() != 3) return 2;
    const auto mode = app.arguments()[1]; const QDir requested(app.arguments()[2]);
    const bool child = mode == "--crash" || mode == "--recover";
    const QDir out(child ? requested.path() : requested.filePath("run-" + project::newId()));
    QDir().mkpath(out.filePath("logs"));
    qputenv("VIDEO_EDITOR_CACHE_DIR", out.filePath("cache").toUtf8());
    QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/segoeui.ttf"));
    QSettings settings(out.filePath("settings.ini"), QSettings::IniFormat);
    Diagnostics diagnostics(out.filePath("logs/workflow.log"));
    MainWindow window(settings, diagnostics); window.show();
    auto* ui = window.findChild<timeline::TimelineWidget*>("timelineArea");
    if (mode == "--crash") {
        if (!window.openProjectPath(out.filePath("first-cut.veproject")) ||
            !ui->execute(timeline::RenameProject{"Recovered packaged edit"}).isEmpty()) return 3;
        window.autosaveNow();
        std::puts("RECOVERY_READY"); std::fflush(stdout); return app.exec();
    }
    if (mode == "--recover") {
        bool offered = false; QTimer click; click.setInterval(10);
        QObject::connect(&click, &QTimer::timeout, [&] {
            for (auto* w : app.topLevelWidgets()) if (w->objectName() == "recoveryDialog") {
                offered = true; click.stop(); w->findChild<QPushButton*>("recoverSelectedButton")->click(); return;
            }
        }); click.start(); window.offerRecovery();
        return offered && window.project().name == "Recovered packaged edit" && window.isProjectDirty() &&
            window.saveProjectPath(out.filePath("recovered.veproject")) ? 0 : 4;
    }
    QJsonArray checks, failures;
    auto check = [&](bool ok, const QString& label) { checks.append(label); if (!ok) { failures.append(label); std::fprintf(stderr, "FAIL: %s\n", qPrintable(label)); } };
    check(!QFile::exists(settings.fileName()) && !QFile::exists(out.filePath("cache/export-capabilities.json")), "Start with empty isolated settings and capabilities");
    window.findChild<QAction*>("newProjectAction")->trigger();
    check(window.isProjectDirty(), "Create project through production action");
    check(window.importPaths({mode}) && waitFor([&] { return !window.mediaBusy(); }), "Background import completes");
    check(window.project().media.size() == 1 && !window.project().media[0].streams.isEmpty() && QFile::exists(window.project().media[0].path), "Imported media is online");
    if (window.project().media.size() != 1) return 1;
    const auto media = window.project().media[0]; const auto sid = ui->sequence()->id;
    for (const auto& stream : media.streams) {
        if (stream.kind != "video" && stream.kind != "audio") continue;
        project::Clip clip; clip.id = project::newId(); clip.name = stream.kind; clip.kind = stream.kind;
        clip.mediaId = media.id; clip.streamIndex = stream.index; clip.durationFrames = 30;
        clip.sourceDurationTicks = project::framesToTicks(30, ui->sequence()->frameRate, stream.timeBase, project::Rounding::Nearest).value_or(0);
        const auto tid = ui->sequence()->tracks[stream.kind == "video" ? 0 : 1].id;
        check(ui->execute(timeline::InsertClip{sid, tid, clip}).isEmpty(), "Place " + stream.kind + " through production timeline");
    }
    const auto videoTrack = ui->sequence()->tracks[0].id, clipId = ui->sequence()->tracks[0].clips[0].id;
    const auto placed = window.project();
    ui->execute(timeline::SetSelection{{sid, {videoTrack}, {clipId}}}); ui->setPlayhead(15, true);
    ui->findChild<QAction*>("timelineSplit")->trigger();
    check(ui->sequence()->tracks[0].clips.size() == 2, "Split action edits the selected video");
    ui->undo(); check(window.project() == placed, "Undo restores placed content exactly"); ui->redo();
    const auto projectPath = out.filePath("first-cut.veproject");
    check(window.saveProjectPath(projectPath), "Atomic first save succeeds");
    const auto saved = window.project();
    check(window.openProjectPath(projectPath) && window.project() == saved, "Reopen preserves saved edits and normalized media paths");
    auto exportSettings = saved.exportSettings; exportSettings.width = 320; exportSettings.height = 180;
    exporting::ExportDialog dialog(playback::compileSequence(saved), exportSettings); dialog.show();
    auto* tabs = dialog.findChild<QTabWidget*>("exportTabs"); auto* start = dialog.findChild<QPushButton*>("exportStart");
    auto* path = dialog.findChild<QLineEdit*>("exportPath"); auto* message = dialog.findChild<QLabel*>("exportMessage");
    int completed = 0; QObject::connect(&dialog, &exporting::ExportDialog::settingsChosen, [&](auto) { ++completed; });
    check(tabs && tabs->count() == 2 && tabs->tabText(0) == "Simple" && tabs->tabText(1) == "Advanced", "Packaged dialog provides both export tabs");
    path->setText(out.filePath("simple.mp4"));
    check(waitFor([&] { return start->isEnabled(); }), "Packaged capability helper validates Simple settings from an empty cache");
    start->click(); check(waitFor([&] { return completed == 1 && start->isEnabled(); }) && message->text().contains("Export complete"), "Simple produces H.264/AAC export");
    tabs->setCurrentIndex(1); auto* encoder = dialog.findChild<QComboBox*>("exportEncoder");
    encoder->setCurrentIndex(encoder->findData("libx264"));
    auto* container = dialog.findChild<QComboBox*>("exportContainer"); container->setCurrentIndex(container->findData("matroska"));
    path->setText(out.filePath("advanced.mkv"));
    const bool advancedReady = waitFor([&] { return start->isEnabled(); });
    if (!advancedReady) std::fprintf(stderr, "Advanced validation: %s; encoder=%s container=%s\n", qPrintable(message->text()), qPrintable(encoder->currentData().toString()), qPrintable(container->currentData().toString()));
    check(advancedReady, "Advanced validates the packaged custom encoder and container");
    if (!advancedReady) return 1;
    start->click(); check(waitFor([&] { return completed == 2 && start->isEnabled(); }) && message->text().contains("Export complete"), "Advanced export completes");
    const auto cancelled = out.filePath("cancelled.mkv"); const QByteArray sentinel("previous destination remains intact");
    check(write(cancelled, sentinel), "Prepare an existing cancellation destination"); path->setText(cancelled);
    check(waitFor([&] { return start->isEnabled(); }), "Cancellation configuration validates");
    QTimer::singleShot(0, [] { for (auto* w : QApplication::topLevelWidgets()) if (auto* box = qobject_cast<QMessageBox*>(w)) box->done(QMessageBox::Yes); });
    start->click();
    dialog.findChild<QPushButton*>("exportCancel")->click();
    check(waitFor([&] { return tabs->isEnabled(); }) && read(cancelled) == sentinel && completed == 3 && message->text().contains("Export cancelled"), "Cancellation preserves existing destination atomically");
    dialog.reject(); window.close();
    const auto lastGood = read(projectPath);
    QProcess crash; crash.start(app.applicationFilePath(), {"--crash", out.path()});
    check(crash.waitForStarted(5000), "Launch packaged crash child"); QByteArray marker; QElapsedTimer timer; timer.start();
    while (timer.elapsed() < 15000 && !marker.contains("RECOVERY_READY") && crash.state() != QProcess::NotRunning) { crash.waitForReadyRead(100); marker += crash.readAllStandardOutput(); }
    check(marker.contains("RECOVERY_READY"), "Production autosave writes crash snapshot"); crash.kill(); crash.waitForFinished(5000);
    check(read(projectPath) == lastGood, "Terminated process preserves last good save");
    QProcess restart; restart.start(app.applicationFilePath(), {"--recover", out.path()});
    check(restart.waitForStarted(5000) && restart.waitForFinished(15000) && restart.exitCode() == 0, "Restart offers production recovery and saves recovered edit");
    const auto loadedModules = modules(); check(!loadedModules.isEmpty(), "Enumerate actual loaded DLL paths");
    check(write(requested.filePath("workflow-result.json"), QJsonDocument(QJsonObject{{"passed", failures.isEmpty()}, {"version", EDITOR_VERSION}, {"runDirectory", out.absolutePath()},
        {"checks", checks}, {"failures", failures}, {"modules", loadedModules},
        {"evidenceLevel", "Automated production Qt widgets offscreen, isolated profile data, packaged DLLs; no new Windows account or human visual validation"}}).toJson()), "Write workflow report");
    return failures.isEmpty() ? 0 : 1;
}

#include "BuildInfo.h"
#include "Diagnostics.h"
#include "MainWindow.h"
#include "media/RuntimeInfo.h"
#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProgressBar>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QTreeWidget>
#include "project/ProjectStore.h"
#include "timeline/TimelineWidget.h"
#include <memory>

namespace {
int smokeTest(MainWindow& window, QSettings& settings, Diagnostics& diagnostics, const QString& directory, const QString& fixtures) {
    QJsonArray failures;
    auto require = [&failures](bool condition, const QString& description) {
        if (!condition) failures.append(description);
    };
    require(window.isVisible(), QStringLiteral("Main window did not start"));
    require(window.findChild<QWidget*>(QStringLiteral("previewArea")) != nullptr, QStringLiteral("Missing preview"));
    require(window.findChild<QWidget*>(QStringLiteral("timelineArea")) != nullptr, QStringLiteral("Missing timeline"));
    require(window.findChild<QProgressBar*>(QStringLiteral("taskProgress")) != nullptr, QStringLiteral("Missing progress"));
    auto* dock = window.findChild<QDockWidget*>(QStringLiteral("mediaBinDock"));
    require(dock && dock->isVisible(), QStringLiteral("Missing visible media dock"));
    if (dock) {
        dock->toggleViewAction()->trigger();
        require(!dock->isVisible(), QStringLiteral("Media dock visibility toggle failed"));
        window.resetWorkspace();
        require(dock->isVisible() && window.dockWidgetArea(dock) == Qt::LeftDockWidgetArea,
                QStringLiteral("Workspace reset failed"));
    }

    const auto savedPath = QDir(directory).filePath(QStringLiteral("ui-project.veproject"));
    auto* create = window.findChild<QAction*>(QStringLiteral("newProjectAction"));
    require(create && window.findChild<QAction*>(QStringLiteral("openProjectAction")) &&
        window.findChild<QAction*>(QStringLiteral("saveProjectAction")) && window.findChild<QAction*>(QStringLiteral("saveProjectAsAction")),
        QStringLiteral("Project file actions missing"));
    if (create) create->trigger();
    require(window.isProjectDirty() && window.project().sequences.size() == 1 && window.project().sequences[0].tracks.size() == 2,
        QStringLiteral("New project action did not create default model"));
    const auto unsavedId = window.project().id;
    window.autosaveNow();
    const auto snapshotPath = QDir(directory).filePath(QStringLiteral("recovery/") + unsavedId + QStringLiteral(".veproject.autosave"));
    auto snapshot = editor::project::ProjectStore::load(snapshotPath);
    require(snapshot && snapshot.project->id == unsavedId && window.isProjectDirty(), QStringLiteral("Untitled autosave failed or marked document saved"));
    QTimer::singleShot(0, &window, [] {
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* box = qobject_cast<QMessageBox*>(widget); box && box->objectName() == QStringLiteral("unsavedProjectDialog"))
                box->done(QMessageBox::Cancel);
        }
    });
    window.close();
    require(window.isVisible() && window.project().id == unsavedId, QStringLiteral("Cancel did not preserve unsaved project/window"));
    require(window.saveProjectPath(savedPath) && !window.isProjectDirty() && !QFile::exists(snapshotPath),
        QStringLiteral("Explicit save failed to clear dirty flag/owned autosave"));
    require(window.openProjectPath(QDir(fixtures).filePath(QStringLiteral("multitrack-v2.veproject"))), QStringLiteral("UI could not open multitrack fixture"));
    auto* tracks = window.findChild<editor::timeline::TimelineWidget*>(QStringLiteral("timelineArea"));
    auto* media = window.findChild<QTreeWidget*>(QStringLiteral("mediaBin"));
    require(tracks && tracks->sequence() && tracks->sequence()->tracks.size() == 4 && media && media->topLevelItemCount() == 2 &&
        media->topLevelItem(0)->text(1) == QStringLiteral("Offline"), QStringLiteral("Loaded track/media/offline summaries incorrect"));
    const auto fixtureProject = window.project();
    require(window.saveProjectPath(savedPath) && window.openProjectPath(savedPath) && window.project() == fixtureProject,
        QStringLiteral("UI multitrack save/open changed project values"));
    require(window.openProjectPath(QDir(fixtures).filePath(QStringLiteral("multitrack-v1.veproject"))) && window.isProjectDirty(),
        QStringLiteral("UI migration did not mark project dirty"));
    require(window.saveProjectPath(savedPath), QStringLiteral("UI could not save migrated project"));
    const auto beforeError = window.project();
    const auto invalidPath = QDir(directory).filePath(QStringLiteral("invalid.veproject"));
    QFile invalidFile(invalidPath);
    require(invalidFile.open(QIODevice::WriteOnly | QIODevice::Truncate), QStringLiteral("Could not create malformed UI test file"));
    invalidFile.write("{truncated"); invalidFile.close();
    auto dismissError = [&window] {
        QTimer::singleShot(0, &window, [] {
            for (auto* widget : QApplication::topLevelWidgets())
                if (auto* box = qobject_cast<QMessageBox*>(widget); box && box->objectName() == QStringLiteral("errorDialog")) box->accept();
        });
    };
    dismissError();
    require(!window.openProjectPath(invalidPath) && window.project() == beforeError, QStringLiteral("Failed load changed current project"));
    dismissError();
    require(!window.saveProjectPath(QDir(directory).filePath(QStringLiteral("nonexistent/project.veproject"))) &&
        window.project() == beforeError && window.projectPath() == savedPath && !window.isProjectDirty(),
        QStringLiteral("Failed save changed current project/path/dirty state"));

    int errorSignals = 0;
    bool dialogFound = false;
    auto connection = QObject::connect(&diagnostics, &Diagnostics::errorReported, &window,
        [&errorSignals](const QString&, const QString&) { ++errorSignals; });
    // Exercise the modal error path directly; the production Help menu contains no test command.
    QTimer::singleShot(0, &window, [&dialogFound] {
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* box = qobject_cast<QMessageBox*>(widget); box && box->objectName() == QStringLiteral("errorDialog")) {
                dialogFound = true;
                box->accept();
            }
        }
    });
    auto* action = window.findChild<QAction*>(QStringLiteral("diagnosticErrorAction"));
    require(!action, QStringLiteral("Test error action leaked into production UI"));
    diagnostics.reportError(&window, QStringLiteral("Intentional diagnostic error"),
        QStringLiteral("This error was requested by the offscreen smoke test. No project or media was changed."));
    require(dialogFound && errorSignals == 1, QStringLiteral("Intentional error dialog/signal failed"));
    require(window.statusBar()->currentMessage() == QStringLiteral("Intentional diagnostic error"),
            QStringLiteral("Error did not reach status area"));
    QObject::disconnect(connection);

    if (dock) window.addDockWidget(Qt::RightDockWidgetArea, dock);
    // Fit the offscreen plugin's default 800x800 screen; Qt clamps oversized saved windows.
    window.resize(800, 600);
    window.close();
    require(settings.status() == QSettings::NoError && QFile::exists(settings.fileName()), QStringLiteral("Settings write failed"));
    {
        // A fresh QSettings and MainWindow must recover persisted state from disk.
        QSettings reopenedSettings(settings.fileName(), QSettings::IniFormat);
        MainWindow reopened(reopenedSettings, diagnostics);
        reopened.show();
        auto* reopenedDock = reopened.findChild<QDockWidget*>(QStringLiteral("mediaBinDock"));
        require(reopenedDock && reopened.dockWidgetArea(reopenedDock) == Qt::RightDockWidgetArea,
                QStringLiteral("Dock placement did not survive reopen"));
        require(reopened.size() == QSize(800, 600),
                QStringLiteral("Window size did not survive reopen: %1x%2").arg(reopened.width()).arg(reopened.height()));
        reopened.close();
    }
    QFile log(diagnostics.logPath());
    require(log.open(QIODevice::ReadOnly), QStringLiteral("Log is unreadable"));
    const auto contents = log.readAll();
    require(contents.contains("Startup Video Editor") && contents.contains("Settings:") && contents.contains("Qt runtime:"),
            QStringLiteral("Startup context missing from log"));
    require(contents.contains("[ERROR]") && contents.contains("Intentional diagnostic error"),
            QStringLiteral("Intentional error missing from log"));
    QJsonObject result{
        {QStringLiteral("passed"), failures.isEmpty()}, {QStringLiteral("failures"), failures},
        {QStringLiteral("version"), EDITOR_VERSION}, {QStringLiteral("qt"), qVersion()},
        {QStringLiteral("ffmpeg"), QString::fromStdString(editor::media::runtimeVersion())},
        {QStringLiteral("platform"), QApplication::platformName()},
        {QStringLiteral("settings"), settings.fileName()}, {QStringLiteral("log"), diagnostics.logPath()},
        {QStringLiteral("errorDialogVerified"), dialogFound},
        {QStringLiteral("projectLifecycleVerified"), failures.isEmpty()},
        {QStringLiteral("evidenceLevel"), QStringLiteral("Automated offscreen Qt Widgets startup/error/layout/project lifecycle; no visual or human acceptance")}
    };
    QFile report(QDir(directory).filePath(QStringLiteral("smoke-result.json")));
    const auto bytes = QJsonDocument(result).toJson();
    if (!report.open(QIODevice::WriteOnly | QIODevice::Truncate) || report.write(bytes) != bytes.size()) {
        qCritical() << "Could not write smoke-result.json";
        return 1;
    }
    qInfo() << "Application shell smoke test" << (failures.isEmpty() ? "PASSED" : "FAILED") << failures;
    return failures.isEmpty() ? 0 : 1;
}
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LocalVideoTools"));
    QCoreApplication::setApplicationName(QStringLiteral("VideoEditor"));
    QCoreApplication::setApplicationVersion(QStringLiteral(EDITOR_VERSION));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Native Windows video editor — Project Media, Sequence and Inspector"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("smoke-test"), QStringLiteral("Run offscreen shell verification, then exit.")});
    parser.addOption({QStringLiteral("data-dir"), QStringLiteral("Isolated settings/log output (required for --smoke-test only)."), QStringLiteral("directory")});
    parser.addOption({QStringLiteral("project-fixtures"), QStringLiteral("Project fixture directory (required for --smoke-test only)."), QStringLiteral("directory")});
    parser.process(app);
    const bool smoke = parser.isSet(QStringLiteral("smoke-test"));
    if (smoke != parser.isSet(QStringLiteral("data-dir")) || smoke != parser.isSet(QStringLiteral("project-fixtures")) ||
        (smoke && (parser.value(QStringLiteral("data-dir")).isEmpty() || QApplication::platformName() != QStringLiteral("offscreen")))) {
        qCritical() << "Use --smoke-test with QT_QPA_PLATFORM=offscreen, --data-dir and --project-fixtures; these directory options are test-only.";
        return 2;
    }
    const auto directory = smoke ? QDir(parser.value(QStringLiteral("data-dir"))).absolutePath()
                                 : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QString logPath, logFailure;
    if (!editor::logging::start(QDir(directory).filePath(QStringLiteral("logs")), logPath, logFailure)) {
        qCritical().noquote() << logFailure;
        if (!smoke) QMessageBox::critical(nullptr, QStringLiteral("Video Editor startup failed"), logFailure);
        return 1;
    }
    int result = 0;
    {
        std::unique_ptr<QSettings> settings;
        if (smoke) {
            settings = std::make_unique<QSettings>(QDir(directory).filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
            settings->clear();
            settings->sync();
        } else {
            settings = std::make_unique<QSettings>(QSettings::IniFormat, QSettings::UserScope,
                QCoreApplication::organizationName(), QCoreApplication::applicationName());
            settings->setFallbacksEnabled(false);
        }
        Diagnostics diagnostics(logPath);
        qInfo().noquote() << "Startup Video Editor" << EDITOR_VERSION << "pid" << QCoreApplication::applicationPid();
        qInfo().noquote() << "Qt runtime:" << qVersion() << "FFmpeg runtime:" << QString::fromStdString(editor::media::runtimeVersion());
        qInfo().noquote() << "Build: Windows x64; C++20; MSVC" << EDITOR_COMPILER_VERSION << "; SDK" << EDITOR_SDK_VERSION;
        qInfo().noquote() << "Platform:" << QApplication::platformName() << "Settings:" << settings->fileName();
        qInfo().noquote() << "Log:" << logPath;
        if (QString::fromLatin1(qVersion()) != QStringLiteral(EDITOR_QT_VERSION) || !editor::media::matchesPinnedVersion()) {
            qCritical() << "Runtime dependency version mismatch; refusing startup";
            if (!smoke) diagnostics.reportError(nullptr, QStringLiteral("Startup dependency error"),
                QStringLiteral("The Qt or FFmpeg runtime differs from dependencies.lock.json. Rebuild with scripts/Build.ps1."));
            result = 1;
        } else {
            MainWindow window(*settings, diagnostics);
            window.show();
            if (!smoke) QTimer::singleShot(0, &window, &MainWindow::offerRecovery);
            if (smoke) {
                app.setQuitOnLastWindowClosed(false);
                QTimer::singleShot(0, &window, [&] { app.exit(smokeTest(window, *settings, diagnostics, directory, parser.value(QStringLiteral("project-fixtures")))); });
            }
            result = app.exec();
        }
        qInfo() << "Shutdown exit code" << result;
    }
    editor::logging::stop();
    return result;
}

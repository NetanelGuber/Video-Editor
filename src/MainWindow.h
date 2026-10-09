#pragma once
#include <QMainWindow>
#include "project/Project.h"
#include "media/Inspection.h"
#include <QHash>
#include <memory>

class Diagnostics;
class QSettings;
class QCloseEvent;
class QSplitter;
class QLockFile;
namespace editor::playback { class MonitorWidget; }
namespace editor::timeline { class TimelineWidget; }
namespace editor::ui { class InspectorWidget; }

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QSettings& settings, Diagnostics& diagnostics);
    void resetWorkspace();
    void newDocument();
    bool openProjectPath(const QString& path);
    bool saveProjectPath(const QString& path);
    const editor::project::Project& project() const { return project_; }
    const QString& projectPath() const { return projectPath_; }
    bool isProjectDirty() const { return dirty_; }
    void autosaveNow();
    void offerRecovery();
    bool recoverProjectPath(const QString& path);
    QString recoveryDirectory() const;
    bool relinkOfflineFolder(const QString& folder);
    bool importPaths(const QStringList& paths);
    bool relinkMediaPath(const QString& id, const QString& path);
    bool mediaBusy() const { return importWorker_ != nullptr; }
    void cancelImport();
    ~MainWindow() override;
protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private:
    QSettings& settings_;
    Diagnostics& diagnostics_;
    QSplitter* workspace_ = nullptr;
    editor::playback::MonitorWidget* sourceMonitor_ = nullptr;
    editor::playback::MonitorWidget* sequenceMonitor_ = nullptr;
    editor::timeline::TimelineWidget* timeline_ = nullptr;
    editor::ui::InspectorWidget* inspector_ = nullptr;
    void updateWorkspaceWidth();
    bool inspectorAutoHidden_ = false;
    void refreshEditedProject();
    void refreshSequenceSummary();
    void showSequence();
    void previewSelected();
    editor::project::Project project_ = editor::project::newProject(QStringLiteral("Untitled"), true);
    editor::project::Project savedProject_ = project_;
    QString projectPath_, autosavePath_;
    std::unique_ptr<QLockFile> snapshotLease_;
    void clearSnapshot();
    bool dirty_ = false;
    bool forceDirty_ = false;
    bool canReplaceDocument();
    bool saveDocument(bool saveAs = false);
    void refreshProject();
    void refreshMedia();
    void showMediaDetails();
    void refreshMediaActions();
    bool startInspection(QVector<editor::media::ImportJob> jobs);
    void acceptInspection(editor::media::Inspection result);
    editor::media::ImportWorker* importWorker_ = nullptr;
    quint64 mediaGeneration_ = 0;
    int importedCount_ = 0, failedCount_ = 0;
    bool relinkingBatch_ = false;
    QHash<QString, editor::media::Inspection> inspections_;
    QStringList inspectionOrder_;
    QVector<editor::media::Inspection> importFailures_;
    QVector<editor::media::ImportJob> pendingInspection_;
};

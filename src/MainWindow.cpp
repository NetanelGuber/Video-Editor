#include "MainWindow.h"
#include "BuildInfo.h"
#include "Diagnostics.h"
#include "media/RuntimeInfo.h"
#include "project/ProjectStore.h"
#include "project/Recovery.h"
#include <QLockFile>
#include <QDialogButtonBox>
#include <QDialog>
#include "media/WaveformWidget.h"
#include "playback/MonitorWidget.h"
#include "timeline/TimelineWidget.h"
#include "timeline/TimelineCanvas.h"
#include "ui/InspectorWidget.h"
#include "export/ExportDialog.h"
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QPixmap>
#include <QIcon>
#include <QScrollBar>
#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDebug>
#include <QDockWidget>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeWidget>
#include <QTimer>
#include <QToolButton>
#include <QLineEdit>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
QLabel* label(const QString& text, QWidget* parent = nullptr) {
    auto* result = new QLabel(text, parent);
    result->setWordWrap(true);
    result->setTextFormat(Qt::PlainText);
    return result;
}
}

MainWindow::MainWindow(QSettings& settings, Diagnostics& diagnostics)
    : settings_(settings), diagnostics_(diagnostics) {
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("Video Editor %1 — Untitled workspace").arg(EDITOR_VERSION));
    setDockNestingEnabled(true);
    resize(1280, 800);
    setMinimumSize(800, 560);

    auto* mediaDock = new QDockWidget(tr("Project Media"), this);
    mediaDock->setObjectName(QStringLiteral("mediaBinDock"));
    mediaDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* mediaContent = new QWidget(mediaDock);
    auto* mediaLayout = new QVBoxLayout(mediaContent);
    mediaContent->setMaximumWidth(420);
    auto* search = new QLineEdit(mediaContent); search->setObjectName("mediaSearch");
    search->setPlaceholderText(tr("Search Project Media")); search->setClearButtonEnabled(true); mediaLayout->addWidget(search);
    auto* mediaTree = new editor::timeline::MediaBinTree(mediaContent);
    mediaTree->setObjectName(QStringLiteral("mediaBin"));
    mediaTree->setHeaderLabels({tr("Name"), tr("State")});
    mediaTree->setMinimumWidth(220);
    mediaTree->setRootIsDecorated(false);
    mediaTree->setIconSize(QSize(48, 27)); mediaTree->setMinimumHeight(90);
    mediaTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    mediaTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    mediaLayout->addWidget(mediaTree);
    connect(search, &QLineEdit::textChanged, this, [mediaTree](const QString& query) {
        for (int i = 0; i < mediaTree->topLevelItemCount(); ++i) {
            auto* row = mediaTree->topLevelItem(i); row->setHidden(!row->text(0).contains(query, Qt::CaseInsensitive));
        }
    });
    connect(mediaTree, &QTreeWidget::itemSelectionChanged, this, &MainWindow::showMediaDetails);
    connect(mediaTree, &QTreeWidget::itemSelectionChanged, this, [this, mediaTree] {
        if (inspector_) inspector_->showMedia(mediaTree->currentItem() ? mediaTree->currentItem()->data(0, Qt::UserRole).toString() : QString{});
    });
    auto* mediaHint = label(tr("Import media to begin"));
    mediaHint->setObjectName(QStringLiteral("mediaHint"));
    mediaLayout->addWidget(mediaHint);
    auto* mediaTask = label({}); mediaTask->setObjectName("mediaTaskState"); mediaTask->hide(); mediaLayout->addWidget(mediaTask);
    auto* thumbnail = new QLabel(mediaContent);
    thumbnail->setObjectName(QStringLiteral("mediaThumbnail"));
    thumbnail->setAlignment(Qt::AlignCenter);
    thumbnail->setMaximumHeight(105);
    mediaLayout->addWidget(thumbnail);
    auto* details = new QPlainTextEdit(mediaContent);
    details->setObjectName(QStringLiteral("mediaDetails"));
    details->setReadOnly(true); details->setMinimumHeight(60); details->setMaximumHeight(150);
    mediaLayout->addWidget(details);
    mediaLayout->addWidget(new WaveformWidget(mediaContent));
    auto* waveformCaption = label({});
    waveformCaption->setObjectName(QStringLiteral("waveformCaption"));
    mediaLayout->addWidget(waveformCaption);
    mediaDock->setWidget(mediaContent);
    addDockWidget(Qt::LeftDockWidgetArea, mediaDock);

    workspace_ = new QSplitter(Qt::Vertical, this);
    workspace_->setObjectName(QStringLiteral("workspaceSplitter"));
    auto* preview = new QWidget(workspace_);
    preview->setObjectName(QStringLiteral("previewArea"));
    auto* previewLayout = new QVBoxLayout(preview);
    auto* monitors = new QTabWidget(preview);
    monitors->setObjectName("monitorTabs");
    sourceMonitor_ = new editor::playback::MonitorWidget(tr("Select Project Media and choose Preview source"), monitors);
    sourceMonitor_->setObjectName("sourceMonitor");
    sequenceMonitor_ = new editor::playback::MonitorWidget(tr("Sequence viewer"), monitors);
    sequenceMonitor_->setObjectName("sequenceMonitor");
    monitors->addTab(sequenceMonitor_, tr("Sequence")); monitors->addTab(sourceMonitor_, tr("Source"));
    connect(sourceMonitor_, &editor::playback::MonitorWidget::playRequested, this, [this] { sequenceMonitor_->controller().pause(); });
    connect(sequenceMonitor_, &editor::playback::MonitorWidget::playRequested, this, [this] { sourceMonitor_->controller().pause(); });
    connect(monitors, &QTabWidget::currentChanged, this, [this](int index) {
        (index == 0 ? sourceMonitor_ : sequenceMonitor_)->controller().pause();
    });
    previewLayout->addWidget(monitors, 1);
    auto* previewButtons = new QHBoxLayout;
    auto* openSource = new QPushButton(tr("Preview source"), preview);
    openSource->setObjectName("openSourceButton");
    auto* projectSequence = new QPushButton(tr("Return to Sequence"), preview);
    projectSequence->setObjectName("projectSequenceButton");
    previewButtons->addWidget(openSource); previewButtons->addWidget(projectSequence);
    previewLayout->addLayout(previewButtons);
    connect(openSource, &QPushButton::clicked, this, &MainWindow::previewSelected);
    connect(projectSequence, &QPushButton::clicked, this, [this, monitors] {
        showSequence(); monitors->setCurrentIndex(0);
    });
    connect(mediaTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int) { previewSelected(); });
    auto* timecode = label(QStringLiteral("00:00:00:00  |  No sequence"));
    timecode->setObjectName(QStringLiteral("sequenceSummary"));
    timecode->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(timecode);
    auto* notice = label({}); notice->setObjectName("workspaceNotice"); notice->hide(); previewLayout->addWidget(notice);

    timeline_ = new editor::timeline::TimelineWidget(workspace_);
    auto* inspectorDock = new QDockWidget(tr("Inspector"), this); inspectorDock->setObjectName("inspectorDock");
    inspectorDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    inspector_ = new editor::ui::InspectorWidget(*timeline_, inspectorDock); inspectorDock->setWidget(inspector_);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);
    connect(timeline_, &editor::timeline::TimelineWidget::propertiesRequested, this, [this, inspectorDock](const QString& section) {
        if (inspectorDock->isVisible()) { inspector_->showSection(section); inspectorDock->raise(); }
        else if (section == "effects") timeline_->editClipEffects();
        else if (section == "audio") timeline_->editTrackAudio();
        else timeline_->editClipProperties();
    });
    connect(timeline_, &editor::timeline::TimelineWidget::stateChanged, this, [this] { refreshSequenceSummary(); });
    connect(timeline_, &editor::timeline::TimelineWidget::cameraOverviewChanged, this, [this](bool enabled) { sequenceMonitor_->controller().setCameraOverview(enabled); });
    connect(timeline_, &editor::timeline::TimelineWidget::projectChanged, this, [this] {
        project_ = timeline_->editor().state().project;
        dirty_ = forceDirty_ || project_ != savedProject_;
        refreshEditedProject(); refreshMedia();
    });
    connect(timeline_, &editor::timeline::TimelineWidget::editError, this, [this](const QString& error) {
        statusBar()->showMessage(tr("Edit rejected: %1").arg(error), 10000);
        qWarning().noquote() << "Timeline edit rejected:" << error;
    });
    connect(timeline_, &editor::timeline::TimelineWidget::activated, this, [this, monitors] {
        monitors->setCurrentIndex(0); sourceMonitor_->controller().pause();
        inspector_->showTimeline();
    });
    connect(timeline_, &editor::timeline::TimelineWidget::seekRequested, this, [this](qint64 frame) {
        const auto* s = timeline_->sequence(); if (!s) return;
        sequenceMonitor_->controller().pause();
        sequenceMonitor_->controller().seek(editor::project::framesToTicks(frame, s->frameRate, {1, 1000000}, editor::project::Rounding::Nearest).value_or(0));
    });
    connect(timeline_, &editor::timeline::TimelineWidget::playRequested, this, [this] {
        auto& controller = sequenceMonitor_->controller();
        if (controller.isPlaying()) controller.pause(); else controller.play();
    });
    connect(&sequenceMonitor_->controller(), &editor::playback::PlaybackController::positionChanged, this, [this](qint64 us) {
        if (const auto* s = timeline_->sequence()) timeline_->setPlayhead(editor::project::ticksToFrames(us, {1, 1000000}, s->frameRate, editor::project::Rounding::Nearest).value_or(0));
        if (sequenceMonitor_->controller().isPlaying()) timeline_->canvas()->ensurePlayheadVisible();
        refreshSequenceSummary();
    });
    preview->setMinimumHeight(270);
    workspace_->setChildrenCollapsible(false);
    workspace_->setStretchFactor(0, 3);
    workspace_->setStretchFactor(1, 2);
    workspace_->setSizes({450, 250});
    setCentralWidget(workspace_);
    workspace_->setMinimumWidth(380);

    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->setObjectName("fileMenu");
    auto* create = fileMenu->addAction(tr("&New project"), this, &MainWindow::newDocument);
    create->setObjectName(QStringLiteral("newProjectAction"));
    create->setShortcut(QKeySequence::New);
    auto* open = fileMenu->addAction(tr("&Open project…"), this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Open project"), {},
            tr("Video Editor project (*.veproject *.veproject.bak *.veproject.bak.2 *.veproject.autosave);;All files (*)"));
        if (!path.isEmpty()) openProjectPath(path);
    });
    open->setObjectName(QStringLiteral("openProjectAction"));
    open->setShortcut(QKeySequence::Open);
    auto* save = fileMenu->addAction(tr("&Save project"), this, [this] { saveDocument(); });
    save->setObjectName(QStringLiteral("saveProjectAction"));
    save->setShortcut(QKeySequence::Save);
    auto* saveAs = fileMenu->addAction(tr("Save project &as…"), this, [this] { saveDocument(true); });
    saveAs->setObjectName(QStringLiteral("saveProjectAsAction"));
    saveAs->setShortcut(QKeySequence::SaveAs);
    auto* exportAction = fileMenu->addAction(tr("Export video…"), this, [this] {
        sequenceMonitor_->controller().pause(); sourceMonitor_->controller().pause();
        editor::exporting::ExportDialog dialog(editor::playback::compileSequence(project_), project_.exportSettings, this);
        connect(&dialog, &editor::exporting::ExportDialog::settingsChosen, this, [this](const editor::project::ExportSettings& settings) {
            timeline_->execute(editor::timeline::SetExportSettings{settings});
        });
        dialog.exec();
    });
    exportAction->setObjectName("exportSequenceAction");
    exportAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    fileMenu->addSeparator();
    auto* editMenu = menuBar()->addMenu(tr("&Edit")); editMenu->setObjectName("editMenu");
    for (const auto* name : {"timelineUndo", "timelineRedo", "timelineSplit", "timelineTrimIn", "timelineTrimOut", "timelineDelete", "timelineCloseGap"}) editMenu->addAction(timeline_->findChild<QAction*>(name));
    editMenu->addSeparator();
    for (const auto* name : {"timelineClipProperties", "timelineClipEffects", "timelineTrackAudio", "timelineAudioBuses"}) editMenu->addAction(timeline_->findChild<QAction*>(name));
    auto* mediaMenu = menuBar()->addMenu(tr("&Media")); mediaMenu->setObjectName("mediaMenu");
    auto* import = mediaMenu->addAction(tr("Import files…"), this, [this] {
        const auto paths = QFileDialog::getOpenFileNames(this, tr("Import media files"), {},
            tr("Media (*.mp4 *.mkv *.mov *.avi *.webm *.m4v *.mxf *.mts *.m2ts *.ts *.mpg *.mpeg *.wmv *.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.aiff *.aif *.wma);;All files (*)"));
        if (!paths.isEmpty()) importPaths(paths);
    });
    import->setObjectName(QStringLiteral("importFilesAction"));
    import->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    auto* folder = mediaMenu->addAction(tr("Import folder…"), this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, tr("Import media folder (includes subfolders)"));
        if (!path.isEmpty()) importPaths({path});
    });
    folder->setObjectName(QStringLiteral("importFolderAction"));
    auto* relink = mediaMenu->addAction(tr("Locate file…"), this, [this, mediaTree] {
        if (!mediaTree->currentItem()) return;
        const auto id = mediaTree->currentItem()->data(0, Qt::UserRole).toString();
        const auto path = QFileDialog::getOpenFileName(this, tr("Locate original media file"), {}, tr("All files (*)"));
        if (!path.isEmpty()) relinkMediaPath(id, path);
    });
    relink->setObjectName(QStringLiteral("relinkMediaAction"));
    auto* relinkFolder = mediaMenu->addAction(tr("Relink offline media from folder…"), this, [this] {
        const auto folder = QFileDialog::getExistingDirectory(this, tr("Find moved originals (includes subfolders)"));
        if (!folder.isEmpty()) relinkOfflineFolder(folder);
    });
    relinkFolder->setObjectName("relinkFolderAction");
    auto* recover = fileMenu->addAction(tr("Recover project…"), this, &MainWindow::offerRecovery);
    recover->setObjectName("recoverProjectAction");
    auto* inspectAction = mediaMenu->addAction(tr("Refresh selected media"), this, [this, mediaTree] {
        if (!mediaTree->currentItem()) return;
        const auto id = mediaTree->currentItem()->data(0, Qt::UserRole).toString();
        for (const auto& m : project_.media) if (m.id == id) { startInspection({{m.path, id}}); break; }
    });
    inspectAction->setObjectName(QStringLiteral("refreshMediaAction"));
    auto* cancel = mediaMenu->addAction(tr("Cancel media work"), this, &MainWindow::cancelImport);
    cancel->setObjectName(QStringLiteral("cancelImportAction"));
    auto* importMenu = new QMenu(this);
    auto* filesChoice = importMenu->addAction(tr("Files…"), import, &QAction::trigger);
    auto* folderChoice = importMenu->addAction(tr("Folder…"), folder, &QAction::trigger);
    connect(import, &QAction::changed, filesChoice, [import, filesChoice] { filesChoice->setEnabled(import->isEnabled()); });
    connect(folder, &QAction::changed, folderChoice, [folder, folderChoice] { folderChoice->setEnabled(folder->isEnabled()); });
    auto* importButton = new QToolButton(mediaContent); importButton->setObjectName("importMediaButton"); importButton->setText(tr("Import media"));
    importButton->setPopupMode(QToolButton::InstantPopup); importButton->setMenu(importMenu); importButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    importButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); mediaLayout->insertWidget(0, importButton);
    auto* emptyActions = new QWidget(mediaContent); emptyActions->setObjectName("emptyProjectActions"); auto* emptyRow = new QHBoxLayout(emptyActions); emptyRow->setContentsMargins(0, 0, 0, 0);
    for (auto* a : {create, open}) { auto* b = new QPushButton(a->text()); emptyRow->addWidget(b); connect(b, &QPushButton::clicked, a, &QAction::trigger); }
    mediaLayout->insertWidget(2, emptyActions);
    auto* mediaContext = new QHBoxLayout;
    auto* previewMedia = new QPushButton(tr("Preview source")); previewMedia->setObjectName("mediaPreviewButton"); mediaContext->addWidget(previewMedia); connect(previewMedia, &QPushButton::clicked, this, &MainWindow::previewSelected);
    for (auto* a : {relink, cancel}) {
        auto* b = new QPushButton(a->text()); b->setObjectName(a->objectName() + "Button"); mediaContext->addWidget(b);
        connect(b, &QPushButton::clicked, a, &QAction::trigger);
        connect(a, &QAction::changed, b, [a, b] { b->setEnabled(a->isEnabled()); b->setVisible(a->isEnabled()); }); b->hide();
    }
    mediaLayout->insertLayout(3, mediaContext);
    mediaTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(mediaTree, &QWidget::customContextMenuRequested, this, [this, mediaTree, relink, inspectAction](const QPoint& point) {
        if (auto* row = mediaTree->itemAt(point)) mediaTree->setCurrentItem(row);
        QMenu context(this); context.addAction(tr("Preview source"), this, &MainWindow::previewSelected); context.addAction(relink); context.addAction(inspectAction); context.exec(mediaTree->viewport()->mapToGlobal(point));
    });
    fileMenu->addSeparator();
    auto* exit = fileMenu->addAction(tr("E&xit"), this, &QWidget::close);
    exit->setShortcut(QKeySequence::Quit);
    auto* sequenceMenu = menuBar()->addMenu(tr("&Sequence")); sequenceMenu->setObjectName("sequenceMenu");
    for (const auto* name : {"newSequenceAction", "nestSequenceAction", "openChildAction", "newMulticamAction", "ungroupCamerasAction", "cameraOverviewAction", "camera1Action", "camera2Action", "camera3Action", "camera4Action"}) sequenceMenu->addAction(timeline_->findChild<QAction*>(name));
    auto* viewMenu = menuBar()->addMenu(tr("&View")); viewMenu->setObjectName("viewMenu");
    auto* timeMenu = viewMenu->addMenu(tr("Time display"));
    auto* timeGroup = new QActionGroup(this);
    const bool seconds = settings_.value(QStringLiteral("display/timeFormat")).toString() == "seconds";
    timeline_->setTimeDisplay(seconds ? editor::timeline::TimeDisplay::Seconds : editor::timeline::TimeDisplay::Frames);
    sourceMonitor_->setTimeDisplay(timeline_->timeDisplay()); sequenceMonitor_->setTimeDisplay(timeline_->timeDisplay());
    for (const bool useSeconds : {false, true}) {
        auto* action = timeMenu->addAction(useSeconds ? tr("Seconds") : tr("Frames"));
        action->setObjectName(useSeconds ? "timelineSecondsAction" : "timelineFramesAction");
        action->setCheckable(true); timeGroup->addAction(action); action->setChecked(useSeconds == seconds);
        connect(action, &QAction::triggered, this, [this, useSeconds] {
            timeline_->setTimeDisplay(useSeconds ? editor::timeline::TimeDisplay::Seconds : editor::timeline::TimeDisplay::Frames);
            sourceMonitor_->setTimeDisplay(timeline_->timeDisplay()); sequenceMonitor_->setTimeDisplay(timeline_->timeDisplay());
            refreshSequenceSummary();
            inspector_->refresh();
            settings_.setValue(QStringLiteral("display/timeFormat"), useSeconds ? "seconds" : "frames");
        });
    }
    viewMenu->addSeparator();
    viewMenu->addAction(mediaDock->toggleViewAction());
    viewMenu->addAction(inspectorDock->toggleViewAction());
    auto* reset = viewMenu->addAction(tr("Reset workspace"), this, &MainWindow::resetWorkspace);
    reset->setObjectName(QStringLiteral("resetWorkspaceAction"));
    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->setObjectName("helpMenu");
    helpMenu->addAction(tr("Getting started"), this, [this] {
        QMessageBox box(QMessageBox::Information, tr("Getting started"), tr("1. Choose Import media → Files… or Folder….\n2. Select Project Media and choose Preview source.\n3. Choose Add selected media at playhead, or drag the media onto a track.\n4. Select a clip to use the Inspector; Split and Trim are above the timeline.\n5. Save project, then choose Export video….\n\nTimeline edits return to Sequence. Locate file… restores an offline source. View → Reset workspace restores the panels."), QMessageBox::Ok, this); box.setTextFormat(Qt::PlainText); box.exec();
    });
    helpMenu->addAction(tr("Keyboard shortcuts"), this, [this] {
        QMessageBox box(QMessageBox::Information, tr("Keyboard shortcuts"), tr("Projects: Ctrl+N New · Ctrl+O Open · Ctrl+S Save · Ctrl+Shift+S Save as\nMedia: Ctrl+I Import files · Ctrl+E Export video\nTimeline: Ctrl+Z Undo · Ctrl+Y / Ctrl+Shift+Z Redo · S Split · Delete Delete\n[ Trim in · ] Trim out · R Ripple on this track · N Snap\nSpace Play/Pause · Left/Right Step · Ctrl+Left/Right Nudge\nHome/End Start/End · Escape Cancel gesture/selection\nTrack header: click E to enable or L to lock\nCtrl+wheel Zoom · Shift+wheel Scroll horizontally"), QMessageBox::Ok, this); box.setTextFormat(Qt::PlainText); box.exec();
    });
    helpMenu->addAction(tr("Diagnostics locations"), this, [this] {
        QMessageBox box(QMessageBox::Information, tr("Diagnostics locations"),
            tr("Settings: %1\nLog: %2").arg(settings_.fileName(), diagnostics_.logPath()), QMessageBox::Ok, this);
        box.setTextFormat(Qt::PlainText);
        box.exec();
    });
    helpMenu->addAction(tr("About Video Editor"), this, [this] {
        QMessageBox box(QMessageBox::Information, tr("About Video Editor"),
            tr("Video Editor %1\nNative Windows application\nQt %2 · FFmpeg %3\nC++20 · MSVC %4 · Windows SDK %5\n\nProject Media · live Sequence viewer · Inspector · validated video export.")
                .arg(EDITOR_VERSION, qVersion(), QString::fromStdString(editor::media::runtimeVersion()),
                     EDITOR_COMPILER_VERSION, EDITOR_SDK_VERSION), QMessageBox::Ok, this);
        box.setTextFormat(Qt::PlainText);
        box.exec();
    });

    auto* toolbar = addToolBar(tr("Workspace"));
    toolbar->setObjectName(QStringLiteral("workspaceToolbar"));
    toolbar->addAction(create);
    toolbar->addAction(open);
    toolbar->addAction(save);
    auto* toolbarImport = new QToolButton(toolbar); toolbarImport->setObjectName("toolbarImportMedia"); toolbarImport->setText(tr("Import media")); toolbarImport->setMenu(importMenu); toolbarImport->setPopupMode(QToolButton::InstantPopup); toolbar->addWidget(toolbarImport);
    toolbar->addAction(exportAction);
    viewMenu->addAction(toolbar->toggleViewAction());
    auto* progress = new QProgressBar(this);
    progress->setObjectName(QStringLiteral("taskProgress"));
    progress->setRange(0, 100);
    progress->setValue(0);
    progress->setFormat(tr("Idle"));
    progress->setFixedWidth(160);
    auto* audioPeakMeter = new QProgressBar(this); audioPeakMeter->setObjectName("audioPeakMeter");
    audioPeakMeter->setRange(0, 60); audioPeakMeter->setValue(0); audioPeakMeter->setFixedWidth(110); audioPeakMeter->setTextVisible(false);
    auto* audioPeakLabel = new QLabel(tr("Audio peak —∞ dBFS"), this); audioPeakLabel->setObjectName("audioPeakLabel");
    audioPeakMeter->setToolTip(tr("Live master output peak; range -60 to 0 dBFS."));
    statusBar()->addPermanentWidget(audioPeakLabel); statusBar()->addPermanentWidget(audioPeakMeter);
    statusBar()->addPermanentWidget(progress);
    statusBar()->showMessage(tr("Ready — new project"));
    connect(&sequenceMonitor_->controller(), &editor::playback::PlaybackController::audioMetersChanged, this,
        [this, audioPeakMeter, audioPeakLabel](const QJsonObject& meters) {
            if (meters.isEmpty()) { audioPeakMeter->setValue(0); audioPeakLabel->setText(tr("Audio peak —∞ dBFS")); audioPeakMeter->setStyleSheet({}); audioPeakLabel->setStyleSheet({}); return; }
            const auto peak = meters.value("masterPeak").toDouble();
            const auto db = peak > 0 ? 20.0 * std::log10(peak) : -60.0;
            audioPeakMeter->setValue(static_cast<int>(std::clamp(db + 60.0, 0.0, 60.0)));
            const auto clips = meters.value("clippedSamples").toInteger();
            if (clips > 0) {
                audioPeakLabel->setText(tr("CLIP · %1 samples").arg(clips));
                audioPeakMeter->setStyleSheet("QProgressBar::chunk { background: #d4380d; }"); audioPeakLabel->setStyleSheet("color: #d4380d; font-weight: bold;");
            } else {
                audioPeakLabel->setText(peak > 0 ? tr("Audio peak %1 dBFS").arg(db, 0, 'f', 1) : tr("Audio peak —∞ dBFS"));
                audioPeakMeter->setStyleSheet({}); audioPeakLabel->setStyleSheet({});
            }
            const auto trackPeaks = meters.value("trackPeaks").toObject().toVariantMap();
            const auto busPeaks = meters.value("busPeaks").toObject().toVariantMap();
            audioPeakMeter->setToolTip(tr("Live master peak %1 dBFS; track peaks %2; bus peaks %3")
                .arg(db, 0, 'f', 1).arg(QString::fromUtf8(QJsonDocument::fromVariant(trackPeaks).toJson(QJsonDocument::Compact)))
                .arg(QString::fromUtf8(QJsonDocument::fromVariant(busPeaks).toJson(QJsonDocument::Compact))));
            const auto error = meters.value("error").toString();
            if (!error.isEmpty()) statusBar()->showMessage(tr("Audio decode error: %1").arg(error), 10000);
            const auto outputError = meters.value("outputError").toString();
            if (!outputError.isEmpty()) statusBar()->showMessage(tr("Audio output unavailable; playback uses the silent clock: %1").arg(outputError), 10000);
        });
    connect(&diagnostics_, &Diagnostics::errorReported, this, [this](const QString& operation, const QString&) {
        statusBar()->showMessage(operation);
    });

    if (settings_.contains(QStringLiteral("window/geometry"))) {
        if (!restoreGeometry(settings_.value(QStringLiteral("window/geometry")).toByteArray()))
            qWarning() << "Discarded invalid saved window geometry";
    }
    if (settings_.contains(QStringLiteral("window/state15_1"))) {
        if (!restoreState(settings_.value(QStringLiteral("window/state15_1")).toByteArray(), 2))
            qWarning() << "Discarded invalid saved dock/toolbar state";
    }
    if (settings_.contains(QStringLiteral("window/splitter"))) {
        if (!workspace_->restoreState(settings_.value(QStringLiteral("window/splitter")).toByteArray()))
            qWarning() << "Discarded invalid saved splitter state";
    }
    qInfo() << "Workspace created: media bin, sequence viewer, timeline, toolbar, status/progress";
    refreshProject();
    resizeDocks({mediaDock, inspectorDock}, {270, 275}, Qt::Horizontal);
    updateWorkspaceWidth();
    auto* autosave = new QTimer(this);
    autosave->setObjectName("autosaveTimer");
    autosave->setInterval(120000);
    connect(autosave, &QTimer::timeout, this, &MainWindow::autosaveNow);
    autosave->start();
    auto* availability = new QTimer(this);
    availability->setObjectName("mediaAvailabilityTimer");
    availability->setInterval(5000);
    connect(availability, &QTimer::timeout, this, &MainWindow::refreshMedia);
    availability->start();
}

bool MainWindow::canReplaceDocument() {
    if (!dirty_) return true;
    QMessageBox box(QMessageBox::Warning, tr("Unsaved project"), tr("Save changes to %1?").arg(project_.name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    box.setObjectName(QStringLiteral("unsavedProjectDialog"));
    box.setTextFormat(Qt::PlainText);
    box.setDefaultButton(QMessageBox::Save);
    const auto answer = box.exec();
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return saveDocument();
    clearSnapshot();
    return true;
}

void MainWindow::newDocument() {
    if (!canReplaceDocument()) return;
    clearSnapshot();
    cancelImport(); ++mediaGeneration_;
    inspections_.clear(); inspectionOrder_.clear(); importFailures_.clear();
    pendingInspection_.clear();
    project_ = editor::project::newProject();
    savedProject_ = project_; forceDirty_ = true;
    projectPath_.clear(); autosavePath_.clear(); dirty_ = true;
    refreshProject();
    statusBar()->showMessage(tr("New project created"));
    findChild<QLabel*>("workspaceNotice")->hide();
    qInfo() << "New project" << project_.id;
}

bool MainWindow::openProjectPath(const QString& path) {
    auto loaded = editor::project::ProjectStore::load(path);
    if (!loaded) {
        diagnostics_.reportError(this, tr("Could not open project"), tr("Your current project is unchanged. Choose a valid project or a recovery copy."), loaded.error);
        return false;
    }
    if (!canReplaceDocument()) return false;
    clearSnapshot();
    cancelImport(); ++mediaGeneration_;
    inspections_.clear(); inspectionOrder_.clear(); importFailures_.clear();
    pendingInspection_.clear();
    project_ = std::move(*loaded.project);
    savedProject_ = project_;
    const bool recovery = path.endsWith(QStringLiteral(".bak"), Qt::CaseInsensitive) || path.endsWith(QStringLiteral(".bak.2"), Qt::CaseInsensitive) || path.endsWith(QStringLiteral(".autosave"), Qt::CaseInsensitive);
    projectPath_ = recovery ? QString{} : QFileInfo(path).absoluteFilePath();
    dirty_ = recovery || loaded.migratedFrom != 0;
    forceDirty_ = dirty_;
    autosavePath_.clear();
    refreshProject();
    statusBar()->showMessage(tr("Opened %1 — %2 offline media reference(s)%3").arg(project_.name).arg(loaded.offlineMediaIds.size())
        .arg(loaded.migratedFrom ? tr("; migrated from schema %1, save to keep schema %2").arg(loaded.migratedFrom).arg(editor::project::SchemaVersion) : QString{}));
    auto* notice = findChild<QLabel*>("workspaceNotice"); notice->setVisible(recovery);
    if (recovery) notice->setText(tr("Project recovered. Review it, then use File → Save project as… to save a working copy."));
    qInfo() << "Project opened" << path << "id" << project_.id << "migratedFrom" << loaded.migratedFrom << "offline media" << loaded.offlineMediaIds.size();
    for (const auto& m : project_.media)
        if (!loaded.offlineMediaIds.contains(m.id) && pendingInspection_.size() < 1000) pendingInspection_.append({m.path, m.id});
    if (!importWorker_ && !pendingInspection_.isEmpty()) {
        auto jobs = std::move(pendingInspection_); pendingInspection_.clear(); startInspection(std::move(jobs));
    }
    return true;
}

bool MainWindow::saveDocument(bool saveAs) {
    auto path = projectPath_;
    if (saveAs || path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, tr("Save project"), path.isEmpty() ? project_.name + ".veproject" : path,
            tr("Video Editor project (*.veproject)"));
        if (path.isEmpty()) return false;
        if (!path.endsWith(QStringLiteral(".veproject"), Qt::CaseInsensitive)) path += QStringLiteral(".veproject");
    }
    return saveProjectPath(path);
}

bool MainWindow::saveProjectPath(const QString& path) {
    auto candidate = project_;
    if (candidate.name == QStringLiteral("Untitled")) candidate.name = QFileInfo(path).completeBaseName();
    const auto error = editor::project::ProjectStore::save(candidate, path);
    if (!error.isEmpty()) {
        diagnostics_.reportError(this, tr("Could not save project"), tr("Your edits remain open and the last saved project was preserved. Use Save project as… in a writable folder, then retry."), error);
        return false;
    }
    if (candidate.name != project_.name) timeline_->execute(editor::timeline::RenameProject{candidate.name});
    project_ = std::move(candidate); savedProject_ = project_; forceDirty_ = false;
    projectPath_ = QFileInfo(path).absoluteFilePath(); dirty_ = false;
    clearSnapshot();
    refreshEditedProject();
    statusBar()->showMessage(tr("Saved %1").arg(projectPath_));
    findChild<QLabel*>("workspaceNotice")->hide();
    qInfo() << "Project saved atomically" << projectPath_ << "id" << project_.id;
    return true;
}

void MainWindow::autosaveNow() {
    if (!dirty_) { clearSnapshot(); return; }
    if (autosavePath_.isEmpty()) {
        const QDir directory(recoveryDirectory());
        if (!QDir().mkpath(directory.absolutePath())) {
            qWarning() << "Could not create project recovery directory";
            statusBar()->showMessage(tr("Autosave failed: recovery directory is not writable")); return;
        }
        auto path = directory.filePath(project_.id + ".veproject.autosave");
        // Preserve an unresolved snapshot of this same project, including another editor's work.
        if (QFileInfo::exists(path) || QFileInfo::exists(path + ".session.lock"))
            path = directory.filePath(project_.id + "-" + editor::project::newId() + ".veproject.autosave");
        auto lease = std::make_unique<QLockFile>(path + ".session.lock");
        lease->setStaleLockTime(0);
        if (!lease->tryLock(0)) { statusBar()->showMessage(tr("Autosave failed: recovery snapshot is in use")); return; }
        snapshotLease_ = std::move(lease); autosavePath_ = path;
    }
    const auto error = editor::project::ProjectStore::autosave(project_, autosavePath_);
    if (!error.isEmpty()) { qWarning().noquote() << "Autosave failed:" << error; statusBar()->showMessage(tr("Autosave failed: %1").arg(error)); }
    else { qInfo() << "Autosaved" << autosavePath_; statusBar()->showMessage(tr("Recovery snapshot saved: %1").arg(autosavePath_)); }
}

QString MainWindow::recoveryDirectory() const {
    auto directory = QFileInfo(diagnostics_.logPath()).absoluteDir(); directory.cdUp();
    return directory.filePath("recovery");
}

void MainWindow::clearSnapshot() {
    if (!autosavePath_.isEmpty() && snapshotLease_) {
        const auto loaded = editor::project::ProjectStore::load(autosavePath_);
        if (loaded && loaded.project->id == project_.id && editor::project::isRecoveryPath(autosavePath_, recoveryDirectory())) {
            if (!QFile::remove(autosavePath_)) qWarning() << "Could not remove resolved recovery snapshot" << autosavePath_;
        }
    }
    autosavePath_.clear(); snapshotLease_.reset();
}

bool MainWindow::recoverProjectPath(const QString& path) {
    if (!editor::project::isRecoveryPath(path, recoveryDirectory())) return false;
    auto lease = std::make_unique<QLockFile>(path + ".session.lock"); lease->setStaleLockTime(0);
    if (!lease->tryLock(0)) {
        diagnostics_.reportError(this, tr("Could not recover project"), tr("This snapshot is being used by another editor.")); return false;
    }
    if (!openProjectPath(path)) return false;
    autosavePath_ = QFileInfo(path).absoluteFilePath(); snapshotLease_ = std::move(lease);
    statusBar()->showMessage(tr("Recovered %1 — review it, then use Save project as…").arg(project_.name));
    return true;
}

void MainWindow::offerRecovery() {
    auto candidates = editor::project::recoveryCandidates(recoveryDirectory());
    if (candidates.isEmpty()) { statusBar()->showMessage(tr("No recovery snapshots available")); return; }
    QDialog dialog(this); dialog.setObjectName("recoveryDialog"); dialog.setWindowTitle(tr("Recover project"));
    dialog.resize(720, 360);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(label(tr("Unsaved snapshots are listed newest first. Recover opens an unsaved project; your last saved project stays safe. Later keeps the snapshots for another time.")));
    auto* list = new QTreeWidget(&dialog); list->setObjectName("recoveryList");
    list->setRootIsDecorated(false); list->setHeaderLabels({tr("Project"), tr("Snapshot time"), tr("State")});
    list->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    for (const auto& candidate : candidates) {
        auto* row = new QTreeWidgetItem(list, {candidate.name, candidate.modified.toLocalTime().toString("yyyy-MM-dd HH:mm:ss"), candidate.error.isEmpty() ? tr("Recoverable") : tr("Invalid — kept for inspection")});
        row->setData(0, Qt::UserRole, candidate.path); row->setToolTip(0, candidate.path); row->setToolTip(2, candidate.error);
    }
    list->setCurrentItem(list->topLevelItem(0)); layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(&dialog);
    auto* recover = buttons->addButton(tr("Recover selected"), QDialogButtonBox::AcceptRole); recover->setObjectName("recoverSelectedButton");
    auto* discard = buttons->addButton(tr("Discard selected…"), QDialogButtonBox::ActionRole); discard->setObjectName("discardRecoveryButton");
    auto* later = buttons->addButton(tr("Later"), QDialogButtonBox::RejectRole); later->setObjectName("recoveryLaterButton");
    layout->addWidget(buttons);
    connect(later, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(recover, &QPushButton::clicked, &dialog, [this, &dialog, list] {
        if (list->currentItem() && recoverProjectPath(list->currentItem()->data(0, Qt::UserRole).toString())) dialog.accept();
    });
    connect(discard, &QPushButton::clicked, &dialog, [this, &dialog, list] {
        const auto* row = list->currentItem(); if (!row) return;
        const auto path = row->data(0, Qt::UserRole).toString();
        QMessageBox confirm(QMessageBox::Warning, tr("Discard recovery snapshot"), tr("Permanently discard this unsaved snapshot of %1?").arg(row->text(0)), QMessageBox::Discard | QMessageBox::Cancel, &dialog);
        confirm.setObjectName("discardRecoveryDialog"); confirm.setTextFormat(Qt::PlainText); confirm.setDefaultButton(QMessageBox::Cancel);
        if (confirm.exec() != QMessageBox::Discard) return;
        QLockFile lease(path + ".session.lock"); lease.setStaleLockTime(0);
        if (!editor::project::isRecoveryPath(path, recoveryDirectory()) || !lease.tryLock(0) || !QFile::remove(path)) {
            diagnostics_.reportError(&dialog, tr("Could not discard snapshot"), tr("The snapshot is in use or its directory is not writable.")); return;
        }
        delete list->takeTopLevelItem(list->indexOfTopLevelItem(list->currentItem()));
        if (!list->topLevelItemCount()) dialog.accept();
    });
    dialog.exec();
}

void MainWindow::refreshProject() {
    setWindowTitle(QStringLiteral("Video Editor %1 — %2%3").arg(EDITOR_VERSION, project_.name, dirty_ ? QStringLiteral(" *") : QString{}));
    sourceMonitor_->setDescription({}, tr("Select Project Media and choose Preview source"));
    timeline_->setProject(project_);
    inspector_->showTimeline();
    refreshMedia();
    refreshEditedProject();
}

void MainWindow::refreshEditedProject() {
    setWindowTitle(QStringLiteral("Video Editor %1 — %2%3").arg(EDITOR_VERSION, project_.name, dirty_ ? QStringLiteral(" *") : QString{}));
    showSequence();
    refreshSequenceSummary();
}
void MainWindow::refreshSequenceSummary() {
    for (const auto& s : project_.sequences) if (s.id == project_.activeSequenceId) {
        const editor::TimeFormat time{s.frameRate, timeline_->timeDisplay()};
        findChild<QLabel*>(QStringLiteral("sequenceSummary"))->setText(tr("%1 | %2/%3 fps | Duration %4 | Position %5")
            .arg(s.name).arg(s.frameRate.numerator).arg(s.frameRate.denominator).arg(time.text(s.durationFrames), time.text(timeline_->playhead())));
    }
}

void MainWindow::showSequence() {
    const auto frame = timeline_->playhead();
    auto description = editor::playback::compileSequence(project_);
    if (sequenceMonitor_->controller().updateCameraCuts(description)) return;
    const bool empty = description.durationUs == 0;
    sequenceMonitor_->setDescription(std::move(description), empty ? tr("Sequence is empty — add selected media at playhead") : tr("Sequence"));
    sequenceMonitor_->controller().setCameraOverview(timeline_->cameraOverview());
    if (const auto* s = timeline_->sequence()) sequenceMonitor_->controller().seek(
        editor::project::framesToTicks(frame, s->frameRate, {1, 1000000}, editor::project::Rounding::Nearest).value_or(0));
    findChild<QTabWidget*>("monitorTabs")->setCurrentIndex(0);
}

void MainWindow::previewSelected() {
    const auto* row = findChild<QTreeWidget*>(QStringLiteral("mediaBin"))->currentItem();
    if (!row) { statusBar()->showMessage(tr("Select a media item first")); return; }
    const auto id = row->data(0, Qt::UserRole).toString();
    for (const auto& media : project_.media) if (media.id == id) {
        sourceMonitor_->setDescription(editor::playback::compileSource(media), media.name);
        findChild<QTabWidget*>("monitorTabs")->setCurrentWidget(sourceMonitor_);
        return;
    }
}

void MainWindow::resetWorkspace() {
    auto* dock = findChild<QDockWidget*>(QStringLiteral("mediaBinDock"));
    dock->setFloating(false);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    dock->show();
    auto* inspectorDock = findChild<QDockWidget*>("inspectorDock"); inspectorDock->setFloating(false);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock); inspectorDock->show(); inspectorAutoHidden_ = false;
    auto* toolbar = findChild<QToolBar*>(QStringLiteral("workspaceToolbar"));
    addToolBar(Qt::TopToolBarArea, toolbar);
    toolbar->show();
    workspace_->setSizes({450, 250});
    resizeDocks({dock, inspectorDock}, {270, 275}, Qt::Horizontal); updateWorkspaceWidth();
    statusBar()->showMessage(tr("Workspace reset"));
    qInfo() << "Workspace reset to default layout";
}
void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event); updateWorkspaceWidth();
}
void MainWindow::updateWorkspaceWidth() {
    auto* dock = findChild<QDockWidget*>("inspectorDock"); if (!dock || dock->isFloating()) return;
    if (width() < 1100 && !dock->isHidden()) { inspectorAutoHidden_ = true; dock->hide(); }
    else if (width() >= 1100 && inspectorAutoHidden_) { inspectorAutoHidden_ = false; dock->show(); }
}

MainWindow::~MainWindow() {
    if (importWorker_) { importWorker_->requestInterruption(); importWorker_->wait(); }
}

bool MainWindow::importPaths(const QStringList& paths) {
    if (paths.isEmpty() || paths.size() > 1000 || mediaBusy()) return false;
    QVector<editor::media::ImportJob> jobs;
    for (const auto& path : paths) jobs.append({path, {}});
    return startInspection(std::move(jobs));
}

bool MainWindow::relinkMediaPath(const QString& id, const QString& path) {
    if (mediaBusy() || path.isEmpty()) return false;
    for (const auto& m : project_.media) if (m.id == id) return startInspection({{path, id}});
    return false;
}

bool MainWindow::relinkOfflineFolder(const QString& folder) {
    if (mediaBusy() || !QFileInfo(folder).isDir()) return false;
    QVector<editor::media::ImportJob> jobs;
    for (const auto& media : project_.media) if (!editor::project::mediaAvailable(media.path))
        jobs.append({QFileInfo(folder).absoluteFilePath(), media.id, QFileInfo(media.path).fileName()});
    return !jobs.isEmpty() && startInspection(std::move(jobs));
}

void MainWindow::cancelImport() {
    if (importWorker_) {
        importWorker_->requestInterruption(); ++mediaGeneration_;
        statusBar()->showMessage(tr("Cancelling media work; completed imports are kept"));
        findChild<QLabel*>("mediaTaskState")->setText(tr("Cancelling media work… Completed imports are kept."));
    }
    pendingInspection_.clear();
}

bool MainWindow::startInspection(QVector<editor::media::ImportJob> jobs) {
    if (mediaBusy() || jobs.isEmpty()) return false;
    QSet<QString> known;
    for (const auto& m : project_.media) known.insert(editor::media::pathIdentity(m.path));
    importedCount_ = failedCount_ = 0;
    relinkingBatch_ = std::any_of(jobs.cbegin(), jobs.cend(), [](const auto& job) { return !job.mediaId.isEmpty(); });
    auto* taskState = findChild<QLabel*>("mediaTaskState"); taskState->show();
    taskState->setText(relinkingBatch_ ? tr("Inspecting / relinking media… Use Cancel media work to stop.") : tr("Importing media… Use Cancel media work to stop."));
    const auto generation = mediaGeneration_;
    const auto documentId = project_.id;
    auto* worker = new editor::media::ImportWorker(std::move(jobs), std::move(known), this);
    importWorker_ = worker;
    connect(worker, &editor::media::ImportWorker::inspected, this,
        [this, worker, generation](editor::media::Inspection result) {
            if (generation == mediaGeneration_) acceptInspection(std::move(result));
            worker->acknowledge();
        });
    connect(worker, &editor::media::ImportWorker::batchComplete, this,
        [this, generation, documentId](int count, int duplicates, bool cancelled, const QString& note) {
            if (project_.id != documentId) return;
            if (generation != mediaGeneration_ && !cancelled) return;
            statusBar()->showMessage(tr("%1: %2 inspected, %3 imported/relinked, %4 skipped duplicate(s), %5 failure(s). %6")
                .arg(cancelled ? tr("Cancelled") : tr("Media work finished")).arg(count).arg(importedCount_).arg(duplicates).arg(failedCount_).arg(note));
            qInfo() << "Media batch" << count << "duplicates" << duplicates << "failures" << failedCount_ << "cancelled" << cancelled << note;
            int offline = 0; for (const auto& m : project_.media) if (!editor::project::mediaAvailable(m.path)) ++offline;
            findChild<QLabel*>("mediaTaskState")->setText(tr("%1: %2 inspected, %3 imported / restored, %4 skipped, %5 failed. %6 remain offline.%7")
                .arg(cancelled ? tr("Cancelled") : tr("Finished")).arg(count).arg(importedCount_).arg(duplicates).arg(failedCount_).arg(offline)
                .arg(failedCount_ ? tr(" Select a failed item for details, then retry Import media or Locate file…. Completed work is kept.") : offline ? tr(" Select an Offline item and choose Locate file….") : tr(" Preview a source or add media at the playhead.")));
        });
    connect(worker, &QThread::finished, this, [this, worker] {
        importWorker_ = nullptr; worker->deleteLater();
        auto* progress = findChild<QProgressBar*>(QStringLiteral("taskProgress"));
        progress->setRange(0, 100); progress->setValue(0); progress->setFormat(tr("Idle"));
        refreshMediaActions();
        if (!pendingInspection_.isEmpty()) {
            auto jobs = std::move(pendingInspection_); pendingInspection_.clear(); startInspection(std::move(jobs));
        }
    });
    auto* progress = findChild<QProgressBar*>(QStringLiteral("taskProgress"));
    progress->setRange(0, 0); progress->setFormat(tr("Inspecting media"));
    refreshMediaActions();
    statusBar()->showMessage(tr("Inspecting media in background…"));
    worker->start(); return true;
}

void MainWindow::acceptInspection(editor::media::Inspection result) {
    QString id = result.mediaId;
    if (!id.isEmpty()) {
        int index = -1;
        for (qsizetype n = 0; n < project_.media.size(); ++n) if (project_.media[n].id == id) { index = static_cast<int>(n); break; }
        if (index < 0) return;
        const auto& old = project_.media[index];
        const bool relinking = QDir::cleanPath(old.path).compare(QDir::cleanPath(result.media.path), Qt::CaseInsensitive) != 0;
        if (relinking && result.error.isEmpty()) {
            result.error = editor::media::relinkError(old, result.media);
            for (const auto& m : project_.media)
                if (m.id != id && editor::media::pathIdentity(m.path) == editor::media::pathIdentity(result.media.path))
                    result.error = tr("That file is already linked to another media record.");
            if (result.error.isEmpty()) {
                auto candidate = project_; result.media.id = id; result.media.name = old.name;
                candidate.media[index] = result.media;
                result.error = editor::project::validate(candidate);
                if (result.error.isEmpty()) {
                    result.error = timeline_->execute(editor::timeline::UpsertMedia{result.media});
                    if (result.error.isEmpty()) ++importedCount_;
                }
            }
        }
        if (!result.error.isEmpty()) {
            ++failedCount_; qWarning().noquote() << "Media inspection/relink failed:" << result.media.path << result.error;
            result.notes = result.error + "\n" + result.notes;
            result.media = project_.media[index];
            result.thumbnail = {}; result.waveform.clear(); result.durationUs = 0;
            result.state = QFileInfo(result.media.path).isFile() ? tr("Inspection failed") : tr("Offline");
        }
    } else if (!result.error.isEmpty()) {
        ++failedCount_;
        qWarning().noquote() << "Media import failed:" << result.media.path << result.error;
        if (importFailures_.size() >= 100) importFailures_.removeFirst();
        importFailures_.append(std::move(result));
        refreshMedia(); return;
    } else {
        for (const auto& m : project_.media)
            if (editor::media::pathIdentity(m.path) == editor::media::pathIdentity(result.media.path)) return;
        if (project_.media.size() >= 1000) {
            ++failedCount_; statusBar()->showMessage(tr("Project media limit is 1000 files.")); return;
        }
        id = result.media.id;
        const auto error = timeline_->execute(editor::timeline::UpsertMedia{result.media});
        if (!error.isEmpty()) { ++failedCount_; return; }
        ++importedCount_;
    }
    // Bound raster/PCM aid payloads as well as entry count; disk cache regenerates evictions on Refresh.
    inspectionOrder_.removeAll(id); inspectionOrder_.append(id); inspections_.insert(id, std::move(result));
    qint64 aidBytes = 0;
    for (const auto& inspection : inspections_) aidBytes += inspection.thumbnail.sizeInBytes() + inspection.waveform.size() * sizeof(float);
    while (inspectionOrder_.size() > 64 || aidBytes > 8 * 1024 * 1024) {
        const auto evicted = inspectionOrder_.takeFirst(); const auto& inspection = inspections_[evicted];
        aidBytes -= inspection.thumbnail.sizeInBytes() + inspection.waveform.size() * sizeof(float); inspections_.remove(evicted);
    }
    setWindowTitle(QStringLiteral("Video Editor %1 — %2%3").arg(EDITOR_VERSION, project_.name, dirty_ ? QStringLiteral(" *") : QString{}));
    refreshMedia();
    findChild<QLabel*>("mediaTaskState")->setText(tr("%1: %2 completed, %3 failed. Cancel media work stops the remaining files.")
        .arg(relinkingBatch_ ? tr("Inspecting / relinking") : tr("Importing")).arg(importedCount_).arg(failedCount_));
}

void MainWindow::refreshMediaActions() {
    const bool busy = mediaBusy();
    for (const auto* name : {"importFilesAction", "importFolderAction"})
        if (auto* action = findChild<QAction*>(name)) action->setEnabled(!busy);
    bool offline = false;
    for (const auto& media : project_.media) offline |= !editor::project::mediaAvailable(media.path);
    if (auto* action = findChild<QAction*>("relinkFolderAction")) action->setEnabled(!busy && offline);
    if (auto* action = findChild<QAction*>(QStringLiteral("cancelImportAction"))) action->setEnabled(busy);
    const auto* row = findChild<QTreeWidget*>(QStringLiteral("mediaBin"))->currentItem();
    const bool selected = row && !row->data(0, Qt::UserRole).toString().isEmpty();
    if (auto* action = findChild<QAction*>("relinkMediaAction")) action->setEnabled(!busy && selected && row->text(1) == tr("Offline"));
    if (auto* action = findChild<QAction*>("refreshMediaAction")) action->setEnabled(!busy && selected && row->text(1) != tr("Offline"));
    for (const auto* name : {"openSourceButton", "mediaPreviewButton"}) if (auto* button = findChild<QPushButton*>(name)) button->setEnabled(selected && row->text(1) != tr("Offline"));
    for (const auto* name : {"importMediaButton", "toolbarImportMedia"}) if (auto* button = findChild<QToolButton*>(name)) button->setEnabled(!busy);
}

void MainWindow::refreshMedia() {
    auto* tree = findChild<QTreeWidget*>(QStringLiteral("mediaBin"));
    const auto selected = tree->currentItem() ? tree->currentItem()->data(0, Qt::UserRole).toString() : QString{};
    const int scroll = tree->verticalScrollBar()->value();
    const QSignalBlocker block(tree);
    tree->clear();
    QStringList offlineIds;
    for (const auto& m : project_.media) {
        auto found = inspections_.constFind(m.id);
        if (found != inspections_.cend() && editor::media::pathIdentity(found->media.path) != editor::media::pathIdentity(m.path)) found = inspections_.cend();
        const bool available = editor::project::mediaAvailable(m.path);
        if (!available) offlineIds.append(m.id);
        const QString state = !available ? tr("Offline") : found != inspections_.cend() && !found->error.isEmpty() ? tr("Inspection failed") : tr("Online");
        auto* row = new QTreeWidgetItem(tree, {m.name, state});
        row->setData(0, Qt::UserRole, m.id); row->setToolTip(0, m.path);
        if (found != inspections_.cend() && !found->thumbnail.isNull() && state != tr("Offline")) row->setIcon(0, QPixmap::fromImage(found->thumbnail));
        if (m.id == selected) tree->setCurrentItem(row);
    }
    timeline_->canvas()->setOfflineMediaIds(offlineIds);
    for (qsizetype i = 0; i < importFailures_.size(); ++i) {
        const auto& failure = importFailures_[i];
        auto* row = new QTreeWidgetItem(tree, {failure.media.name, failure.state});
        row->setData(0, Qt::UserRole + 1, static_cast<int>(i));
        row->setToolTip(0, failure.media.path + "\n" + failure.error);
    }
    if (!tree->currentItem() && tree->topLevelItemCount() > 0) tree->setCurrentItem(tree->topLevelItem(0));
    tree->verticalScrollBar()->setValue(scroll);
    findChild<QLabel*>(QStringLiteral("mediaHint"))->setText(project_.media.isEmpty() ? tr("Import media to begin") : tr("%1 media file(s) · %2 offline. Sources stay in their original folders.").arg(project_.media.size()).arg(offlineIds.size()));
    findChild<QWidget*>("emptyProjectActions")->setVisible(project_.media.isEmpty());
    const auto query = findChild<QLineEdit*>("mediaSearch")->text();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) tree->topLevelItem(i)->setHidden(!tree->topLevelItem(i)->text(0).contains(query, Qt::CaseInsensitive));
    showMediaDetails();
}

void MainWindow::showMediaDetails() {
    auto* details = findChild<QPlainTextEdit*>(QStringLiteral("mediaDetails"));
    auto* image = findChild<QLabel*>(QStringLiteral("mediaThumbnail"));
    auto* waveform = findChild<WaveformWidget*>(QStringLiteral("audioWaveform"));
    auto* caption = findChild<QLabel*>(QStringLiteral("waveformCaption"));
    image->clear(); waveform->setPeaks({}); caption->clear(); details->clear();
    image->hide(); waveform->hide(); caption->hide();
    refreshMediaActions();
    const auto* row = findChild<QTreeWidget*>(QStringLiteral("mediaBin"))->currentItem();
    if (!row) { timeline_->setSelectedMedia({}); details->setPlainText(tr("Select imported media to see its details.")); return; }
    const auto id = row->data(0, Qt::UserRole).toString();
    if (timeline_) timeline_->setSelectedMedia(id);
    if (id.isEmpty()) {
        const auto& failure = importFailures_[row->data(0, Qt::UserRole + 1).toInt()];
        details->setPlainText(failure.media.path + "\n" + failure.state + ": " + failure.error + tr("\nThis failed import was not added to the project. Check the file, then retry using Import media.")); return;
    }
    for (const auto& saved : project_.media) if (saved.id == id) {
        auto found = inspections_.constFind(id);
        if (found != inspections_.cend() && editor::media::pathIdentity(found->media.path) != editor::media::pathIdentity(saved.path)) found = inspections_.cend();
        const auto& m = found != inspections_.cend() && found->error.isEmpty() ? found->media : saved;
        QString text = saved.path + "\n" + tr("State: %1 | Container: %2 | %3 bytes\n").arg(row->text(1), m.container).arg(m.sizeBytes);
        qint64 durationUs = found != inspections_.cend() ? found->durationUs : 0;
        if (durationUs > 0) text += tr("Duration: %1 s (container)\n").arg(durationUs / 1000000.0, 0, 'f', 6);
        for (const auto& s : m.streams) {
            const double seconds = static_cast<double>(s.durationTicks) * s.timeBase.numerator / s.timeBase.denominator;
            text += tr("Stream %1: %2, %3\n  Duration: %4 | time base %5/%6 | start %7 ticks\n")
                .arg(s.index).arg(s.kind, s.codec).arg(s.durationTicks > 0 ? QString::number(seconds, 'f', 6) + tr(" s (reported/estimated)") : tr("unknown"))
                .arg(s.timeBase.numerator).arg(s.timeBase.denominator).arg(s.startTicks);
            if (s.kind == "video") text += tr("  %1 × %2 | %3/%4 fps (average/declared) | %5-bit | rotation %6°\n  Timing: %7\n")
                .arg(s.width).arg(s.height).arg(s.frameRate.numerator).arg(s.frameRate.denominator).arg(s.bitDepth).arg(s.rotation)
                .arg(s.variableFrameRate ? tr("variable intervals observed") : tr("no variable intervals recorded; see probe notes"));
            else text += tr("  %1 Hz | %2 channel(s)\n").arg(s.sampleRate).arg(s.channels);
        }
        if (found != inspections_.cend()) {
            text += "\n" + found->notes;
            if (row->text(1) != tr("Offline")) {
                if (!found->thumbnail.isNull()) { image->setPixmap(QPixmap::fromImage(found->thumbnail).scaled(176, 99, Qt::KeepAspectRatio, Qt::SmoothTransformation)); image->show(); }
                waveform->setPeaks(found->waveform);
                waveform->setVisible(!found->waveform.isEmpty()); caption->show();
                if (!found->waveform.isEmpty()) caption->setText(tr("Audio peaks: first audio stream, opening %1 s (up to 30 s)").arg(found->waveformSeconds, 0, 'f', 2));
                else caption->setText(tr("No waveform available (see stream details)."));
            }
        } else text += tr("\nSelect Refresh selected media to regenerate inspection aids.");
        details->setPlainText(text); break;
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!canReplaceDocument()) { event->ignore(); return; }
    clearSnapshot();
    cancelImport();
    settings_.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings_.setValue(QStringLiteral("window/state15_1"), saveState(2));
    settings_.setValue(QStringLiteral("window/splitter"), workspace_->saveState());
    settings_.sync();
    if (settings_.status() != QSettings::NoError) {
        diagnostics_.reportError(this, tr("Could not save workspace settings"),
            tr("The layout could not be saved to %1. Check folder permissions and available disk space.").arg(settings_.fileName()));
    } else {
        qInfo().noquote() << "Workspace settings saved:" << settings_.fileName();
    }
    QMainWindow::closeEvent(event);
}


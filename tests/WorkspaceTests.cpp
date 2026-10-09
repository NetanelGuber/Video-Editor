#include "MainWindow.h"
#include "Diagnostics.h"
#include "ui/InspectorWidget.h"
#include "timeline/TimelineCanvas.h"
#include "project/ProjectStore.h"
#include "project/Effects.h"
#include "export/ExportDialog.h"
#include "playback/MonitorWidget.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QGroupBox>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <cstdio>
#include <cmath>
using namespace editor;
namespace {
bool waitFor(const std::function<bool()>& predicate, int timeout = 30000) {
    QElapsedTimer time; time.start();
    while (!predicate() && time.elapsed() < timeout) { QApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1); }
    return predicate();
}
void dismiss(const char* name, const std::function<void(QDialog*)>& inspect = {}) {
    QTimer::singleShot(0, [name, inspect] { for (auto* w : QApplication::topLevelWidgets()) if (w->objectName() == name) { auto* dialog = qobject_cast<QDialog*>(w); if (inspect) inspect(dialog); dialog->reject(); } });
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/segoeui.ttf"));
    if (app.arguments().size() < 3) return 2;
    QDir root(app.arguments()[1]), output(app.arguments()[2]); QDir().mkpath(output.absolutePath());
    const bool review = app.arguments().contains("--visual-review");
    QJsonArray checks, failures;
    auto check = [&](bool ok, const QString& description) { checks.append(QJsonObject{{"passed", ok}, {"description", description}}); if (!ok) { failures.append(description); std::fprintf(stderr, "FAIL: %s\n", qPrintable(description)); } };
    QSettings settings(output.filePath("workspace-settings.ini"), QSettings::IniFormat); settings.clear();
    Diagnostics diagnostics(output.filePath("workspace.log")); MainWindow window(settings, diagnostics); window.resize(1400, 900); window.show(); QApplication::processEvents();
    auto action = [&](const char* name) { return window.findChild<QAction*>(name); };
    auto* timeline = window.findChild<timeline::TimelineWidget*>("timelineArea");
    auto* inspectorDock = window.findChild<QDockWidget*>("inspectorDock");
    auto* inspector = window.findChild<ui::InspectorWidget*>("inspector");
    auto* tabs = window.findChild<QTabWidget*>("monitorTabs");
    auto* media = window.findChild<QTreeWidget*>("mediaBin");
    auto* mediaDock = window.findChild<QDockWidget*>("mediaBinDock");
    auto* mediaState = window.findChild<QLabel*>("mediaTaskState");
    auto menuContains = [&](const char* menu, const char* name) { return window.findChild<QMenu*>(menu)->actions().contains(action(name)); };
    check(window.menuBar()->actions().size() == 6 && menuContains("fileMenu", "exportSequenceAction") && menuContains("mediaMenu", "importFilesAction") && menuContains("editMenu", "timelineUndo") && menuContains("sequenceMenu", "nestSequenceAction"), "Six task menus route the existing production actions");
    check(menuContains("sequenceMenu", "setSequenceEndToPlayheadAction") && menuContains("sequenceMenu", "fitSequenceToClipsAction") && menuContains("sequenceMenu", "manualSequenceEndAction") &&
        window.findChild<QPushButton*>("sequenceEndButton")->isVisible(),
        "Sequence menu and visible timeline header expose sequence end controls");
    check(!menuContains("fileMenu", "importFilesAction") && !action("diagnosticErrorAction"), "Media lives in Media; test error command is absent from production");
    check(mediaDock->windowTitle() == "Project Media" && inspectorDock->isVisible(), "Project Media and optional Inspector use the concept names");
    check(action("exportSequenceAction")->text() == "Export video…" && action("exportSequenceAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_E) && window.findChild<QToolBar*>("workspaceToolbar")->actions().contains(action("exportSequenceAction")), "Export video is prominent and retains Ctrl+E");
    check(action("importFilesAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_I) && action("timelineSplit")->shortcut() == QKeySequence(Qt::Key_S), "Import and editing shortcuts stay stable");
    check(window.findChild<QLabel*>("mediaHint")->text() == "Import media to begin" && window.findChild<QToolButton*>("importMediaButton")->isVisible() && window.findChild<QWidget*>("emptyProjectActions")->isVisible(), "Empty workspace provides Import media and New/Open without a menu");
    const auto choices = window.findChild<QToolButton*>("importMediaButton")->menu()->actions();
    check(choices.size() == 2 && choices[0]->text() == "Files…" && choices[1]->text() == "Folder…", "Single Import media menu exposes Files and Folder");
    check(!action("timelineInsert")->isEnabled() && !action("timelineClipProperties")->isEnabled() && !action("timelineTrackAudio")->isEnabled() && !window.findChild<QPushButton*>("openSourceButton")->isEnabled(), "Empty selection disables placement, properties, audio and preview");
    check(!window.findChild<QToolBar*>("sequenceContextToolbar")->actions().contains(action("nestSequenceAction")) && !window.findChild<QToolBar*>("timelinePlacementToolbar")->actions().contains(action("timelineClipEffects")), "Occasional sequence tools and selection forms no longer crowd the timeline");
    window.grab().save(output.filePath("empty-workspace.png"));
    check(window.importPaths({root.filePath("fixtures/generated/session-5/flash-beep.mp4")}), "Production import starts");
    check(mediaState->isVisible() && mediaState->text().contains("Importing") && action("cancelImportAction")->isEnabled() && !window.findChild<QToolButton*>("importMediaButton")->isEnabled(), "Inline import state offers Cancel and blocks duplicate work");
    check(waitFor([&] { return !window.mediaBusy(); }) && window.project().media.size() == 1 && mediaState->text().contains("Finished"), "Completed import updates state and keeps a next action");
    check(media->topLevelItem(0)->text(1) == "Online" && action("timelineInsert")->isEnabled() && !action("relinkMediaAction")->isEnabled(), "Online media permits placement and hides the offline Locate action");
    window.findChild<QLineEdit*>("mediaSearch")->setText("no-such-media"); check(media->topLevelItem(0)->isHidden(), "Search filters production rows");
    timeline->addTrack("video"); check(media->topLevelItem(0)->isHidden(), "Search survives project and media refresh"); timeline->undo();
    window.findChild<QLineEdit*>("mediaSearch")->clear();
    window.findChild<QPushButton*>("openSourceButton")->click(); check(tabs->currentIndex() == 1, "Preview source explicitly switches to Source");
    action("timelineInsert")->trigger(); check(tabs->currentIndex() == 0 && timeline->sequence()->tracks[0].clips.size() == 1, "Placement returns to Sequence and uses existing insertion");
    action("timelineClipEffects")->trigger(); check(inspector->findChild<QTabWidget*>("inspectorTabs")->currentIndex() == 1 && !window.findChild<QDialog*>("effectProperties"), "Effects command selects Inspector without modal hopping");
    check(inspector->findChild<QLabel*>("inspectorContext")->text().contains(timeline->sequence()->tracks[0].clips[0].name), "Inspector follows selected clip identity");
    auto original = timeline->editor().state();
    const auto sid = timeline->sequence()->id, video = timeline->sequence()->tracks[0].id, audio = timeline->sequence()->tracks[1].id;
    const auto videoClip = timeline->sequence()->tracks[0].clips[0].id;
    auto effect = project::defaultEffect("opacity");
    timeline->execute(timeline::SetClipEffects{sid, video, videoClip, {effect}});
    const auto withEffect = timeline->editor().state();
    inspector->findChild<QListWidget*>("inspectorEffectStack")->item(0)->setCheckState(Qt::Unchecked);
    check(!timeline->sequence()->tracks[0].clips[0].effects[0].enabled, "Inline effect toggle uses production command"); timeline->undo(); check(timeline->editor().state() == withEffect, "Effect toggle undo restores complete state"); timeline->undo(); check(timeline->editor().state() == original, "Undo returns exactly to original clip state");
    inspector->findChild<QCheckBox*>("inspectorTrackLocked")->click(); check(!action("timelineClipEffects")->isEnabled() && !inspector->findChild<QPushButton*>("inspectorEditEffects")->isEnabled(), "Locked track disables action and Inspector editing");
    inspector->findChild<QCheckBox*>("inspectorTrackLocked")->click();
    bool fallback = false; inspectorDock->hide();
    dismiss("effectProperties", [&](QDialog*) { fallback = true; }); action("timelineClipEffects")->trigger();
    check(fallback, "Hidden Inspector routes same Effects action to focused dialog"); inspectorDock->show();
    timeline->setPlayhead(30, true);
    original = timeline->editor().state(); action("timelineSplit")->trigger(); check(timeline->sequence()->tracks[0].clips.size() == 2 && tabs->currentIndex() == 0, "Edit menu Split follows normal timeline path"); action("timelineUndo")->trigger(); check(timeline->editor().state() == original, "Edit menu Undo restores split exactly");
    timeline->setPlayhead(0, true);
    auto* streams = timeline->findChild<QComboBox*>("timelineStream");
    for (int i = 0; i < streams->count(); ++i) if (streams->itemText(i).contains("audio")) streams->setCurrentIndex(i);
    action("timelineInsert")->trigger();
    const auto audioClip = timeline->sequence()->tracks[1].clips[0].id;
    timeline->execute(timeline::SetTrackAudio{sid, audio, 0.87654321, false, false, 0.12345678, {}, {0, {{0, 0.8, "hold"}}, {}}});
    timeline->execute(timeline::SetClipAudio{sid, audio, audioClip, 0.7654321, false, 2, 3, -0.123456, {0, {{0, 0.5, "linear"}}, {}}});
    original = timeline->editor().state();
    action("timelineTrackAudio")->trigger(); check(inspector->findChild<QTabWidget*>("inspectorTabs")->currentIndex() == 2, "Track audio selects Audio / Track context");
    inspector->findChild<QPushButton*>("inspectorApplyTrackAudio")->click(); inspector->findChild<QPushButton*>("inspectorApplyClipAudio")->click();
    check(timeline->editor().state() == original, "Untouched Inspector forms preserve full precision, fades, routing and automation");
    inspector->findChild<QCheckBox*>("inspectorTrackMute")->click(); inspector->findChild<QPushButton*>("inspectorApplyTrackAudio")->click();
    check(timeline->sequence()->tracks[1].muted && timeline->sequence()->tracks[1].audioAutomation == original.project.sequences[0].tracks[1].audioAutomation, "Inline track mute retains existing automation");
    auto* applyAudio = inspector->findChild<QPushButton*>("inspectorApplyTrackAudio");
    window.activateWindow(); window.raise(); QApplication::processEvents(); applyAudio->setFocus(); QApplication::processEvents();
    QTest::keyClick(applyAudio, Qt::Key_Z, Qt::ControlModifier);
    check(timeline->editor().state() == original, "Ctrl+Z from Inspector restores complete track audio state");
    timeline->addTitle("Original title");
    inspector->findChild<QPlainTextEdit*>("inspectorTitleText")->setPlainText("Edited title"); inspector->findChild<QPushButton*>("inspectorApplyTitle")->click();
    check(window.project().titles[0].text == "Edited title", "Inline title edit updates live project via UpsertTitle"); timeline->undo(); check(window.project().titles[0].text == "Original title", "Inline title undo retains styling and clip timing");
    const auto saved = output.filePath("workspace.veproject"); check(window.saveProjectPath(saved), "Save edited workspace fixture");
    const auto savedProject = window.project(); check(window.openProjectPath(saved) && waitFor([&] { return !window.mediaBusy(); }) && window.project() == savedProject, "Inspector edits survive atomic save/reopen");
    window.grab().save(output.filePath("editing-workspace.png"));
    window.resize(850, 700); QApplication::processEvents(); check(!inspectorDock->isVisible() && window.centralWidget()->width() >= 380 && window.findChild<QWidget*>("previewArea")->height() >= 270 && timeline->isVisible(), "Narrow workspace retains usable viewer and timeline, hiding Inspector");
    check(media->height() >= 90 && media->visualItemRect(media->topLevelItem(0)).height() <= media->viewport()->height(), "Narrow Project Media keeps a complete row visible");
    window.grab().save(output.filePath("narrow-workspace.png")); window.resize(1400, 900); QApplication::processEvents(); check(inspectorDock->isVisible(), "Automatically hidden Inspector returns when space permits");
    window.findChild<QPushButton*>("openSourceButton")->click(); timeline->setPlayhead(1, true); check(tabs->currentIndex() == 0 && window.findChild<QLabel*>("sequenceSummary")->text().contains("Position"), "Seek returns to Sequence with position summary");
    exporting::ExportDialog dialog(playback::compileSequence(window.project()), {}); dialog.show();
    check(dialog.windowTitle() == "Export video" && dialog.findChild<QLabel*>("exportSummary")->text().contains("Choose an output") && !dialog.findChild<QTableWidget*>("exportNativeOptions")->isVisible(), "Export summary explains destination; raw options start collapsed");
    dialog.findChild<QTabWidget*>("exportTabs")->setCurrentIndex(1);
    dialog.findChild<QGroupBox*>("exportNativeGroup")->setChecked(true); check(dialog.findChild<QTableWidget*>("exportNativeOptions")->isVisible(), "Expandable details reveal existing encoder controls");
    dialog.findChild<QSpinBox*>("exportWidth")->setValue(321); check(!dialog.findChild<QPushButton*>("exportStart")->isEnabled() && dialog.findChild<QLabel*>("exportMessage")->text().contains("even"), "Conflicts remain immediate and block export");
    dialog.findChild<QSpinBox*>("exportWidth")->setValue(320); dialog.findChild<QSpinBox*>("exportHeight")->setValue(180); dialog.findChild<QLineEdit*>("exportPath")->setText(output.filePath("workspace-export.mp4"));
    check(waitFor([&] { return dialog.findChild<QPushButton*>("exportStart")->isEnabled(); }), "Only successful exact-settings check enables Export");
    check(dialog.findChild<QLabel*>("exportCapabilities")->text().contains("Validated"), "Ready feedback names validated encoder");
    const auto exported = output.filePath("workspace-export.mp4");
    check(!QFile::exists(exported) || QFile::remove(exported), "Remove only this suite's disposable export before repeat verification");
    dialog.grab().save(output.filePath("export-ready.png")); dialog.findChild<QPushButton*>("exportStart")->click();
    check(!dialog.findChild<QPushButton*>("exportStart")->isEnabled() && dialog.findChild<QLabel*>("exportMessage")->text().contains("Exporting"), "Export progress disables repeat start");
    check(waitFor([&] { return dialog.findChild<QPushButton*>("exportStart")->isEnabled(); }) && dialog.findChild<QLabel*>("exportMessage")->text().contains("Export complete") && dialog.findChild<QLabel*>("exportMessage")->text().contains("Encoder:"), "Completion identifies destination and actual encoder"); dialog.reject();
    auto offline = project::newProject("Offline workspace"); auto missing = savedProject.media[0]; missing.id = project::newId(); missing.path = output.filePath("missing-source.mp4"); offline.media = {missing};
    check(project::ProjectStore::save(offline, output.filePath("offline.veproject")).isEmpty() && window.openProjectPath(output.filePath("offline.veproject")), "Prepare production Offline state");
    check(media->topLevelItem(0)->text(1) == "Offline" && window.findChild<QPushButton*>("relinkMediaActionButton")->isVisible() && !window.findChild<QPushButton*>("openSourceButton")->isEnabled(), "Offline item has a direct Locate file action and cannot preview");
    check(window.relinkOfflineFolder(root.filePath("fixtures/generated/session-5")) && waitFor([&] { return !window.mediaBusy(); }) && mediaState->text().contains("remain offline"), "Folder relink reports remaining offline references");
    window.grab().save(output.filePath("offline-workspace.png"));
    QFile broken(output.filePath("failed-import.mp4")); check(broken.open(QIODevice::WriteOnly), "Create disposable invalid media fixture"); broken.write("not a media container"); broken.close();
    const auto beforeFailure = window.project();
    check(window.importPaths({broken.fileName()}) && waitFor([&] { return !window.mediaBusy(); }) && window.project() == beforeFailure && mediaState->text().contains("retry Import media"), "Failed import preserves project and provides an inline retry action");
    media->setCurrentItem(media->topLevelItem(media->topLevelItemCount() - 1));
    check(window.findChild<QPlainTextEdit*>("mediaDetails")->toPlainText().contains("retry using Import media"), "Failed file details explain preservation and recovery");
    const auto snapshot = QDir(window.recoveryDirectory()).filePath(beforeFailure.id + ".veproject.autosave");
    check(QDir().mkpath(window.recoveryDirectory()) && project::ProjectStore::autosave(beforeFailure, snapshot).isEmpty() && window.recoverProjectPath(snapshot), "Recover a disposable snapshot through production recovery path");
    check(window.findChild<QLabel*>("workspaceNotice")->isVisible() && window.findChild<QLabel*>("workspaceNotice")->text().contains("Save project as"), "Recovery keeps a persistent review and save-working-copy notice");
    window.resetWorkspace(); check(mediaDock->isVisible() && inspectorDock->isVisible() && window.dockWidgetArea(inspectorDock) == Qt::RightDockWidgetArea, "Reset workspace restores native docks");
    check(window.saveProjectPath(output.filePath("offline.veproject")), "Save test state before close");
    check(!window.findChild<QLabel*>("workspaceNotice")->isVisible(), "Successful save resolves recovery notice");
    inspectorDock->hide(); window.close();
    MainWindow reopened(settings, diagnostics); reopened.show(); check(!reopened.findChild<QDockWidget*>("inspectorDock")->isVisible(), "Inspector visibility persists under the new layout key"); reopened.close();
    QFile report(output.filePath("workspace-result.json")); if (!report.open(QIODevice::WriteOnly)) return 1; report.write(QJsonDocument(QJsonObject{{"passed", failures.isEmpty()}, {"checkCount", checks.size()}, {"checks", checks}, {"failures", failures}, {"platform", QApplication::platformName()}, {"evidenceLevel", "Production Qt Widgets automated routing/state/edits/export and raster layout; physical scaling and subjective appearance require visual review"}}).toJson()); report.close();
    std::printf("Workspace tests: %lld checks, %lld failures\n", static_cast<long long>(checks.size()), static_cast<long long>(failures.size()));
    if (review && failures.isEmpty()) { window.openProjectPath(saved); window.show(); window.resize(1400, 900); window.resetWorkspace(); app.setQuitOnLastWindowClosed(true); return app.exec(); }
    return failures.isEmpty() ? 0 : 1;
}

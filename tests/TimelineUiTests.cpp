#include "MainWindow.h"
#include "Diagnostics.h"
#include "timeline/TimelineWidget.h"
#include "timeline/TimelineCanvas.h"
#include "timeline/PropertyDialogs.h"
#include "project/Effects.h"
#include "playback/MonitorWidget.h"
#include "media/Inspection.h"
#include "project/ProjectStore.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTableWidget>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QProgressBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QMenu>
#include <memory>
#include <cstdio>
#include <cmath>
#include <limits>

using namespace editor;
bool waitFor(const std::function<bool()>& predicate, int timeout = 10000) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < timeout) {
        if (predicate()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1);
    }
    return predicate();
}
void drag(QWidget* target, QPoint from, QPoint to) {
    QTest::mousePress(target, Qt::LeftButton, Qt::NoModifier, from);
    QMouseEvent move(QEvent::MouseMove, to, target->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &move);
    QTest::mouseRelease(target, Qt::LeftButton, Qt::NoModifier, to);
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    if (app.arguments().size() != 3) return 2;
    QDir root(app.arguments()[1]), output(app.arguments()[2]); QDir().mkpath(output.absolutePath());
    QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/segoeui.ttf"));
    QJsonArray failures; int checks = 0;
    auto check = [&](bool ok, const QString& message) { ++checks; if (!ok) { failures.append(message); std::fprintf(stderr, "FAIL: %s\n", qPrintable(message)); } };
    const auto inspected = media::inspect(root.filePath("fixtures/generated/session-5/flash-beep.mp4"));
    check(inspected.error.isEmpty(), "Inspect real decoded synthetic media"); if (!inspected.error.isEmpty()) return 1;
    auto project = project::newProject("Session 6 first cut"); project.media.append(inspected.media);
    auto& seq = project.sequences[0]; seq.durationFrames = 360;
    project::Track upper; upper.id = project::newId(); upper.kind = "video"; upper.name = "Video 2"; seq.tracks.insert(1, upper);
    const auto sid = seq.id, video = seq.tracks[0].id, video2 = seq.tracks[1].id, audio = seq.tracks[2].id;
    project::Clip first;
    for (const auto& stream : inspected.media.streams) {
        project::Clip clip; clip.id = project::newId(); clip.name = stream.kind == "video" ? "Flash and beep video" : "Beep audio";
        clip.mediaId = inspected.media.id; clip.kind = stream.kind; clip.streamIndex = stream.index; clip.durationFrames = 90;
        clip.sourceDurationTicks = project::framesToTicks(90, seq.frameRate, stream.timeBase, project::Rounding::Nearest).value_or(0);
        seq.tracks[stream.kind == "video" ? 0 : 2].clips.append(clip); if (stream.kind == "video") first = clip;
    }
    check(project::validate(project).isEmpty(), "Native timeline fixture validates");
    const auto fixture = root.filePath("fixtures/generated/session-6/first-cut.veproject");
    QDir().mkpath(QFileInfo(fixture).absolutePath());
    check(project::ProjectStore::save(project, fixture).isEmpty(), "Prepare human first-cut project");
    QSettings settings(output.filePath("settings.ini"), QSettings::IniFormat); settings.clear();
    Diagnostics diagnostics(output.filePath("timeline-ui.log")); MainWindow window(settings, diagnostics); window.show();
    check(window.openProjectPath(fixture), "Open fixture in real MainWindow");
    check(waitFor([&] { return !window.mediaBusy(); }), "Background inspection completes");
    auto* ui = window.findChild<timeline::TimelineWidget*>("timelineArea");
    auto* canvas = ui->canvas(); auto* surface = canvas->viewport();
    {
        auto endProject = project;
        endProject.sequences[0].tracks[2].enabled = false;
        endProject.sequences[0].tracks[2].locked = true;
        endProject.sequences[0].tracks[2].clips[0].startFrame = 30;
        timeline::TimelineWidget endWidget; endWidget.setProject(endProject);
        auto* fit = endWidget.findChild<QAction*>("fitSequenceToClipsAction");
        auto* setEnd = endWidget.findChild<QAction*>("setSequenceEndToPlayheadAction");
        auto* endButton = endWidget.findChild<QPushButton*>("sequenceEndButton");
        check(fit && setEnd && endButton && endButton->menu() &&
            endButton->menu()->actions().contains(fit) && endButton->menu()->actions().contains(setEnd),
            "Sequence end menu exposes both production resize actions");
        if (!fit || !setEnd) return 1;
        endWidget.setPlayhead(330);
        fit->trigger();
        const auto fitted = endWidget.editor().state().project;
        check(endWidget.sequence()->durationFrames == 120 && endWidget.playhead() == 120 &&
            endWidget.sequence()->tracks == endProject.sequences[0].tracks,
            "Fit removes the empty tail, clamps playhead and preserves all clips including disabled locked audio");
        endWidget.undo();
        check(endWidget.editor().state().project == endProject, "Undo fit restores the exact explicit sequence duration");
        endWidget.redo();
        check(endWidget.editor().state().project == fitted, "Redo fit restores the frame-exact end");
        endWidget.undo(); endWidget.setPlayhead(150);
        setEnd->trigger();
        check(endWidget.sequence()->durationFrames == 150 && endWidget.sequence()->tracks == endProject.sequences[0].tracks,
            "Set end to playhead retains intentional trailing space without retiming clips");
        const auto endAt150 = endWidget.editor().state().project;
        QString resizeError;
        QObject::connect(&endWidget, &timeline::TimelineWidget::editError, [&](const QString& message) { resizeError = message; });
        endWidget.setPlayhead(100); setEnd->trigger();
        check(endWidget.editor().state().project == endAt150 && resizeError.contains("Trim or delete"),
            "Set end refuses to cut disabled locked clips and provides actionable trim instructions");
        endWidget.setPlayhead(120); setEnd->trigger();
        check(endWidget.sequence()->durationFrames == 120, "Set end accepts the exact exclusive clip boundary");
        setEnd->trigger(); endWidget.undo();
        check(endWidget.editor().state().project == endAt150, "Repeated unchanged resize actions add no undo entries");
        endWidget.redo(); fit->trigger(); fit->trigger(); endWidget.undo();
        check(!endWidget.sequence()->automaticEnd && endWidget.sequence()->durationFrames == 120,
            "Automatic mode is undoable even at the same end and repeated activation adds no history");
        endWidget.redo();
        endWidget.execute(timeline::SetTrackLocked{sid, audio, false});
        auto added = first; added.id = project::newId(); added.startFrame = 150;
        check(endWidget.execute(timeline::InsertClip{sid, video, added}).isEmpty() && endWidget.sequence()->durationFrames == 240,
            "Automatic mode grows when a later clip is inserted");
        check(endWidget.execute(timeline::DeleteClip{sid, video, added.id}).isEmpty() && endWidget.sequence()->durationFrames == 120,
            "Automatic mode shrinks when the last clip is deleted");
        check(endWidget.execute(timeline::RemoveTrack{sid, audio}).isEmpty() && endWidget.sequence()->durationFrames == 90,
            "Automatic mode follows removal of the last occupied track");
        check(endWidget.execute(timeline::TrimClip{sid, video, first.id, 0, 45}).isEmpty() && endWidget.sequence()->durationFrames == 45,
            "Automatic mode follows a clip out-point trim");
        check(endWidget.execute(timeline::MoveClip{sid, video, first.id, video, 30}).isEmpty() && endWidget.sequence()->durationFrames == 75,
            "Automatic mode follows clip moves with frame-exact duration");
        endWidget.undo();
        check(endWidget.sequence()->automaticEnd && endWidget.sequence()->durationFrames == 45,
            "Undo restores the automatic end with the clip edit");
        endWidget.execute(timeline::ResizeSequence{sid, 150});
        endWidget.execute(timeline::TrimClip{sid, video, first.id, 0, 30});
        check(!endWidget.sequence()->automaticEnd && endWidget.sequence()->durationFrames == 150,
            "Manual end remains after later clips are shortened");
        check(project::ProjectStore::save(fitted, output.filePath("fitted-end.veproject")).isEmpty() &&
            project::ProjectStore::load(output.filePath("fitted-end.veproject")).project == fitted,
            "Resized sequence end survives atomic save and reopen");
        auto empty = project::newProject("Empty sequence end"); empty.sequences[0].durationFrames = 90;
        endWidget.setProject(empty); fit->trigger();
        check(endWidget.sequence()->durationFrames == 0 && endWidget.playhead() == 0,
            "Fit an empty sequence removes all trailing space");
        endWidget.undo(); endWidget.setPlayhead(0); setEnd->trigger();
        check(endWidget.sequence()->durationFrames == 0, "An empty sequence can end at frame zero");
        endWidget.newSequence("Automatic new sequence");
        check(endWidget.sequence()->automaticEnd && endWidget.sequence()->durationFrames == 0,
            "New sequences default to automatic end");
        endWidget.setTimeDisplay(TimeDisplay::Seconds);
        QTimer::singleShot(0, [&] {
            auto* dialog = endWidget.findChild<QDialog*>("sequenceEndDialog");
            dialog->findChild<QLineEdit*>("sequenceEndValue")->setText("2.5");
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });
        endWidget.findChild<QAction*>("manualSequenceEndAction")->trigger();
        check(!endWidget.sequence()->automaticEnd && endWidget.sequence()->durationFrames == 75,
            "Native manual end dialog accepts seconds and extends beyond the current playhead range");
        const auto beforeCancel = endWidget.editor().state().project;
        QTimer::singleShot(0, [&] { endWidget.findChild<QDialog*>("sequenceEndDialog")->reject(); });
        endWidget.findChild<QAction*>("manualSequenceEndAction")->trigger();
        check(endWidget.editor().state().project == beforeCancel, "Cancel manual end leaves mode and duration unchanged");
    }
    {
        timeline::TimelineWidget primaryWidget; primaryWidget.setProject(project);
        primaryWidget.execute(timeline::SetSelection{{sid, {video}, {first.id}}});
        auto* choosePrimary = primaryWidget.findChild<QAction*>("timelineSetPrimaryVideo");
        const auto beforePrimaryChoice = primaryWidget.editor().state().project;
        if (choosePrimary) choosePrimary->trigger();
        check(choosePrimary && choosePrimary->isEnabled() &&
            primaryWidget.editor().state().project.sequences[0].primaryVideoMediaId == inspected.media.id &&
            primaryWidget.editor().state().project.sequences[0].primaryVideoStreamIndex == first.streamIndex &&
            primaryWidget.editor().state().project.sequences[0].tracks == beforePrimaryChoice.sequences[0].tracks &&
            primaryWidget.editor().state().project.sequences[0].frameRate == beforePrimaryChoice.sequences[0].frameRate,
            "Selected timeline video can be designated Primary Video without changing clip edits or sequence timing");
        primaryWidget.execute(timeline::SetSelection{{sid, {}, {}}});
        const bool clearActionAvailable = choosePrimary && choosePrimary->isEnabled() && choosePrimary->text() == "Clear Primary Video";
        if (choosePrimary) choosePrimary->trigger();
        check(clearActionAvailable &&
            primaryWidget.editor().state().project.sequences[0].primaryVideoMediaId.isEmpty() &&
            primaryWidget.editor().state().project.sequences[0].tracks == beforePrimaryChoice.sequences[0].tracks,
            "Primary Video can be cleared with no clip selected and without changing timeline edits");
    }
    auto* monitor = window.findChild<playback::MonitorWidget*>("sequenceMonitor");
    auto* peakLabel = window.findChild<QLabel*>("audioPeakLabel"); auto* peakMeter = window.findChild<QProgressBar*>("audioPeakMeter");
    monitor->controller().audioMetersChanged({{"masterPeak", 0.5}, {"clippedSamples", 0}, {"trackPeaks", QJsonObject{{audio, 0.5}}}});
    check(peakLabel && peakMeter && peakLabel->text().contains("-6.0 dBFS") && peakMeter->value() > 50,
        "Native status meter presents live peak level and track detail");
    monitor->controller().audioMetersChanged({{"masterPeak", 1.25}, {"clippedSamples", 3}, {"error", "missing audio source"}});
    check(peakLabel->text().contains("CLIP") && window.statusBar()->currentMessage().contains("Audio decode error"),
        "Native status meter latches clipping and makes missing-audio errors visible");
    monitor->controller().audioMetersChanged({});
    auto* tabs = window.findChild<QTabWidget*>("monitorTabs");
    // Display units affect every time control without editing or seeking the project.
    const auto beforeDisplay = ui->editor().state(); const auto undoBeforeDisplay = ui->editor().undoCount();
    auto* secondsAction = window.findChild<QAction*>("timelineSecondsAction");
    auto* framesAction = window.findChild<QAction*>("timelineFramesAction");
    check(framesAction->isChecked() && ui->timeDisplay() == TimeDisplay::Frames, "Frames remain the default display");
    ui->setPlayhead(45); secondsAction->trigger();
    check(secondsAction->isChecked() && !framesAction->isChecked() && ui->findChild<QLabel*>("timelinePosition")->text().contains("1.500 s"), "Exclusive seconds choice updates position immediately");
    check(window.findChild<QLabel*>("sequenceSummary")->text().contains("12.000 s") && window.findChild<QLabel*>("sequenceSummary")->text().contains("30/1 fps"), "Sequence duration changes units while frame rate remains fps");
    bool monitorsSeconds = true;
    for (auto* viewer : window.findChildren<playback::MonitorWidget*>()) monitorsSeconds &= viewer->findChild<QLabel*>("monitorTime")->text().contains(" s / ");
    check(monitorsSeconds, "Both source and sequence monitors use seconds");
    check(ui->editor().state() == beforeDisplay && ui->editor().undoCount() == undoBeforeDisplay && !window.isProjectDirty(), "Display changes preserve project, dirty state and undo history");
    {
        timeline::TimelineWidget ruler; ruler.resize(1000, 400); ruler.show(); QApplication::processEvents();
        auto p = project::newProject(); p.sequences[0].durationFrames = 108000;
        ruler.setTimeDisplay(TimeDisplay::Seconds);
        auto* zoom = ruler.findChild<QSlider*>("timelineZoom"); auto* scale = ruler.canvas();
        auto secondsValue = [](const QString& label) { return label.chopped(2).toDouble(); };
        for (const project::Rational rate : {project::Rational{24, 1}, project::Rational{30, 1}, project::Rational{30000, 1001}, project::Rational{60, 1}}) {
            p.sequences[0].frameRate = rate; ruler.setProject(p);
            double previousStep = std::numeric_limits<double>::max(); bool adaptive = true, fit = true, positions = true;
            for (const int level : {0, 25, 50, 75, 100}) {
                zoom->setValue(level); scale->horizontalScrollBar()->setValue(0);
                const auto ticks = scale->rulerTicks();
                if (ticks.size() < 2) { adaptive = false; continue; }
                const auto step = secondsValue(ticks[1].label) - secondsValue(ticks[0].label);
                const double pixelsPerFrame = (scale->xAt(1000) - scale->xAt(0)) / 1000.0;
                adaptive &= step <= previousStep && step >= static_cast<double>(rate.denominator) / rate.numerator;
                previousStep = step;
                for (qsizetype i = 0; i < ticks.size(); ++i) {
                    const auto expectedFrame = secondsValue(ticks[i].label) * rate.numerator / rate.denominator;
                    positions &= std::abs(static_cast<double>(scale->frameAt(ticks[i].x)) - expectedFrame) <= 0.51 + 0.5 / pixelsPerFrame;
                    if (i) fit &= ticks[i].x - ticks[i - 1].x >= scale->fontMetrics().horizontalAdvance(ticks[i - 1].label) + 12;
                }
            }
            check(adaptive && previousStep < 1, "Seconds intervals shrink below one second at zoom for " + QString::number(rate.numerator));
            check(fit, "Seconds labels fit for " + QString::number(rate.numerator));
            check(positions, "Seconds labels match rational frame positions for " + QString::number(rate.numerator));
            scale->horizontalScrollBar()->setValue(scale->horizontalScrollBar()->maximum());
            const auto scrolled = scale->rulerTicks();
            check(scrolled.size() > 1 && secondsValue(scrolled.front().label) > 0, "Scrolled seconds ruler uses absolute times");
        }
        p.sequences[0].durationFrames = std::numeric_limits<qint64>::max(); ruler.setProject(p);
        scale->horizontalScrollBar()->setValue(scale->horizontalScrollBar()->maximum());
        check(scale->rulerTicks().size() < 100, "Huge absolute times produce bounded ruler work");
        ruler.close();
    }
    {
        // Decimal inputs use the real production dialogs and retain integer-frame storage.
        const TimeFormat time{{30, 1}, TimeDisplay::Seconds};
        QLineEdit field; time.initialize(&field, 1); qint64 parsed = 0;
        check(time.read(&field, parsed) && parsed == 1, "Untouched fractional seconds retain exact one-frame duration");
        field.setText("0.05"); check(time.read(&field, parsed) && parsed == 2, "Half-frame seconds input rounds to nearest frame");
        field.setText("-1"); check(!time.read(&field, parsed), "Negative seconds are rejected");
        field.setText("9999999999999999999"); check(!time.read(&field, parsed), "Overflowing seconds are rejected");
        const TimeFormat ntsc{{30000, 1001}, TimeDisplay::Seconds}; ntsc.initialize(&field, 123456789);
        check(ntsc.read(&field, parsed) && parsed == 123456789, "Unchanged fractional-rate times preserve exact frames");
        field.setText("1.001"); check(ntsc.read(&field, parsed) && parsed == 30, "Fractional-rate seconds input converts exactly");
        project::Title title; title.id = project::newId();
        QTimer::singleShot(0, &app, [&] {
            auto* dialog = QApplication::activeModalWidget(); auto* length = dialog->findChild<QLineEdit*>("titleDuration");
            check(length->text() == "3", "Title duration opens in seconds"); length->setText("1.25");
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });
        const auto titleEdit = timeline::titleDialog(&window, title, 90, time);
        check(titleEdit && titleEdit->durationFrames == 38, "Title seconds edits store nearest frame");
        auto audioClip = first; audioClip.kind = "audio";
        QTimer::singleShot(0, &app, [&] {
            auto* dialog = QApplication::activeModalWidget(); dialog->findChild<QLineEdit*>("clipFadeIn")->setText("0.5");
            dialog->findChild<QLineEdit*>("clipFadeOut")->setText("1.25");
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });
        const auto audioEdit = timeline::clipAudioDialog(&window, sid, audio, audioClip, time);
        check(audioEdit && audioEdit->fadeInFrames == 15 && audioEdit->fadeOutFrames == 38, "Audio fades accept seconds");
        auto effectsClip = first; effectsClip.effects = {project::defaultEffect("videoFade"), project::defaultEffect("transform")};
        QTimer::singleShot(0, &app, [&] {
            auto* dialog = QApplication::activeModalWidget(); dialog->findChild<QLineEdit*>("effect_inFrames")->setText("0.25");
            dialog->findChild<QListWidget*>("effectStack")->setCurrentRow(1);
            dialog->findChild<QPushButton*>("keyframeAdd")->click();
            auto* table = dialog->findChild<QTableWidget*>("effectKeyframes");
            check(table->horizontalHeaderItem(1)->text().contains("seconds") && qobject_cast<QLineEdit*>(table->cellWidget(0, 1))->text() == "1", "Keyframe playhead time and column use seconds");
            qobject_cast<QLineEdit*>(table->cellWidget(0, 1))->setText("0.5");
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });
        const auto effectEdit = timeline::effectsDialog(&window, effectsClip, 30, time);
        check(effectEdit && (*effectEdit)[0].parameters["inFrames"].toString() == "8" && (*effectEdit)[1].keyframes.first().first().frame == 15, "Video fades and keyframes convert seconds to stored frames");
    }
    framesAction->trigger(); ui->setPlayhead(0);
    window.activateWindow(); canvas->setFocus(); QApplication::processEvents();
    check(ui->findChild<QLabel*>("timelinePosition")->text().contains("Frame 0") && window.findChild<QLabel*>("sequenceSummary")->text().contains("360 frames"), "Switching back restores frame displays");
    QImage image; int frameCount = 0;
    QObject::connect(&monitor->controller(), &playback::PlaybackController::frameReady, &app, [&](const QImage& value) { image = value; if (!value.isNull()) ++frameCount; });
    monitor->controller().seek(0); // Subscribe before requesting the frame; opening may already have displayed it.
    check(waitFor([&] { return frameCount > 0; }), "Viewer decodes initial sequence frame");
    auto verifyViewer = [&](const QString& reason) {
        check(monitor->controller().description().sequence == *ui->sequence() && tabs->currentWidget() == monitor && monitor->isVisible(), "Live viewer graph/visibility after " + reason);
        check(window.project() == ui->editor().state().project, "Persistence model matches editor after " + reason);
    };
    // Use the actual bin model's payload, then enter through the header before moving to Video 2.
    // Native Qt sends move/drop events only after the viewport accepts drag entry.
    auto* bin = window.findChild<QTreeWidget*>("mediaBin");
    bin->setCurrentItem(bin->topLevelItem(0));
    const auto sourceIndex = bin->model()->index(0, 0);
    check(sourceIndex.flags().testFlag(Qt::ItemIsDragEnabled), "Imported media row is draggable");
    check(bin->supportedDragActions() == Qt::CopyAction && bin->defaultDropAction() == Qt::CopyAction, "Media-bin drag offers copy placement without moving source rows");
    std::unique_ptr<QMimeData> binPayload(bin->model()->mimeData({sourceIndex}));
    check(binPayload && binPayload->data(timeline::MediaMime) == inspected.media.id.toUtf8(), "Real media-bin model exports timeline MIME and stable media ID");
    if (binPayload) {
        const int video2Y = timeline::TimelineCanvas::Ruler + timeline::TimelineCanvas::Row + timeline::TimelineCanvas::Row / 2;
        const QPoint throughHeader(10, video2Y), intoVideo2(canvas->xAt(120), video2Y);
        QDragEnterEvent routedEnter(throughHeader, Qt::CopyAction, binPayload.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(surface, &routedEnter);
        check(routedEnter.isAccepted(), "Drag entering through track labels stays eligible for subsequent movement");
        if (routedEnter.isAccepted()) {
            QDragMoveEvent headerMove(throughHeader, Qt::CopyAction, binPayload.get(), Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(surface, &headerMove);
            check(!headerMove.isAccepted(), "Track labels do not accept final clip placement");
            QDragMoveEvent routedMove(intoVideo2, Qt::CopyAction, binPayload.get(), Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(surface, &routedMove);
            check(routedMove.isAccepted(), "Drag can move from labels into Video 2");
            QDropEvent routedDrop(intoVideo2, Qt::CopyAction, binPayload.get(), Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(surface, &routedDrop);
            check(routedDrop.isAccepted() && ui->sequence()->tracks[1].clips.size() == 1 && ui->sequence()->tracks[1].clips[0].startFrame == 120, "Real bin payload drops onto Video 2 after crossing labels");
            verifyViewer("routed Video 2 drop"); ui->undo();
            check(ui->editor().state().project == project, "Routed drop undo restores original project");
        }
        QDragLeaveEvent leave; QApplication::sendEvent(surface, &leave);
        QDragEnterEvent rulerEnter(QPoint(canvas->xAt(120), 10), Qt::CopyAction, binPayload.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(surface, &rulerEnter);
        check(rulerEnter.isAccepted(), "Drag entering through ruler stays eligible for track movement");
        QApplication::sendEvent(surface, &leave);
        ui->execute(timeline::SetTrackLocked{sid, video2, true});
        const auto lockedDropState = ui->editor().state();
        QDragEnterEvent lockedEnter(throughHeader, Qt::CopyAction, binPayload.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(surface, &lockedEnter);
        QDragMoveEvent lockedMove(intoVideo2, Qt::CopyAction, binPayload.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(surface, &lockedMove);
        QDropEvent lockedDrop(intoVideo2, Qt::CopyAction, binPayload.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(surface, &lockedDrop);
        check(lockedEnter.isAccepted() && !lockedMove.isAccepted() && !lockedDrop.isAccepted() && ui->editor().state() == lockedDropState, "Accepting drag entry still rejects locked-track placement transactionally");
        QApplication::sendEvent(surface, &leave); ui->undo();
        QDragEnterEvent headerEnter(throughHeader, Qt::CopyAction, binPayload.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(surface, &headerEnter);
        QDropEvent headerDrop(throughHeader, Qt::CopyAction, binPayload.get(), Qt::LeftButton, Qt::NoModifier);
        const auto beforeHeaderDrop = ui->editor().state(); QApplication::sendEvent(surface, &headerDrop);
        check(headerEnter.isAccepted() && !headerDrop.isAccepted() && ui->editor().state() == beforeHeaderDrop, "Entering headers does not allow a drop onto labels");
        QApplication::sendEvent(surface, &leave);
        QMimeData unknown; unknown.setData(timeline::MediaMime, "unknown-media-id");
        QDragEnterEvent unknownEnter(intoVideo2, Qt::CopyAction, &unknown, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(surface, &unknownEnter);
        check(!unknownEnter.isAccepted(), "Unknown media drag remains rejected at entry");
        QApplication::sendEvent(surface, &leave);
    }
    // Real drop events exercise the viewport and MIME compatibility path.
    QMimeData data; data.setData(timeline::MediaMime, inspected.media.id.toUtf8());
    const QPoint dropAt(canvas->xAt(150), timeline::TimelineCanvas::Ruler + timeline::TimelineCanvas::Row / 2);
    QDragEnterEvent enter(dropAt, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(surface, &enter);
    QDropEvent drop(dropAt, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(surface, &drop);
    check(enter.isAccepted() && drop.isAccepted() && ui->sequence()->tracks[0].clips.size() == 2, "Media-bin MIME drag places clip on chosen track");
    verifyViewer("placement");
    auto placed = ui->sequence()->tracks[0].clips.last();
    check(placed.startFrame == 150 && placed.durationFrames == 360, "Placement uses exact sequence frames and reported full source duration");
    auto baseline = ui->editor().state();
    ui->undo(); check(ui->sequence()->tracks[0].clips.size() == 1, "Placement undo removes clip and restores selection");
    ui->redo(); check(ui->editor().state() == baseline, "Placement redo retains IDs and exact selection");
    auto* snap = ui->findChild<QAction*>("timelineSnap"); snap->setChecked(false);
    // Zoom out so the full source clip and both trim handles are available in the viewport.
    ui->findChild<QSlider*>("timelineZoom")->setValue(30); QCoreApplication::processEvents();
    auto rect = canvas->clipRect(placed.id);
    drag(surface, QPoint(rect.right() - 3, rect.center().y()), QPoint(canvas->xAt(240), rect.center().y()));
    check(ui->sequence()->tracks[0].clips.last().durationFrames == 90, QString("Out handle trim commits one source-aware command: duration %1").arg(ui->sequence()->tracks[0].clips.last().durationFrames)); verifyViewer("trim");
    rect = canvas->clipRect(placed.id);
    drag(surface, QPoint(rect.left() + 3, rect.center().y()), QPoint(canvas->xAt(165), rect.center().y()));
    check(ui->sequence()->tracks[0].clips.last().startFrame == 165 && ui->sequence()->tracks[0].clips.last().durationFrames == 75, QString("In handle updates source and preserves out edge: start %1 duration %2").arg(ui->sequence()->tracks[0].clips.last().startFrame).arg(ui->sequence()->tracks[0].clips.last().durationFrames));
    rect = canvas->clipRect(placed.id);
    const auto grabOffset = canvas->frameAt(rect.center().x()) - ui->sequence()->tracks[0].clips.last().startFrame;
    drag(surface, rect.center(), QPoint(canvas->xAt(180 + grabOffset), rect.center().y() + timeline::TimelineCanvas::Row));
    check(ui->sequence()->tracks[1].clips.size() == 1 && ui->sequence()->tracks[1].clips[0].startFrame == 180 && ui->editor().state().selection.trackIds == QStringList{video2}, "Mouse move across compatible tracks preserves identity and selects destination");
    verifyViewer("move");
    // Ruler seek and shortcuts use actual Qt event routing, not direct model commands.
    QTest::mouseClick(surface, Qt::LeftButton, Qt::NoModifier, QPoint(canvas->xAt(210), 12));
    check(ui->playhead() == 210 && monitor->controller().positionUs() == 7000000, "Ruler click seeks exact frame in live viewer");
    QTest::keyClick(canvas, Qt::Key_S); QCoreApplication::processEvents();
    check(ui->sequence()->tracks[1].clips.size() == 2 && ui->editor().state().selection.clipIds.size() == 2, "Split shortcut preserves selection of both halves"); verifyViewer("split");
    auto mixed = ui->editor().state();
    QTest::keyClick(canvas, Qt::Key_Right); check(ui->playhead() == 211, "Right-arrow steps one exact sequence frame");
    QTest::keyClick(canvas, Qt::Key_Left); check(ui->playhead() == 210, "Left-arrow reverses frame step");
    QTest::keyClick(canvas, Qt::Key_Delete); QCoreApplication::processEvents();
    check(ui->sequence()->tracks[1].clips.isEmpty() && ui->editor().state().selection.clipIds.isEmpty(), "Delete shortcut prunes selected clips");
    QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier); QCoreApplication::processEvents();
    check(ui->editor().state() == mixed, "Undo shortcut restores complete mixed state and selection");
    QTest::keyClick(canvas, Qt::Key_Y, Qt::ControlModifier); QCoreApplication::processEvents();
    check(ui->sequence()->tracks[1].clips.isEmpty(), "Redo shortcut restores delete"); ui->undo();
    check(ui->playhead() == 210, "Undo leaves playhead coherent at retained sequence frame");
    // Lock/enabled controls route through the same command history.
    const int rowY = timeline::TimelineCanvas::Ruler + timeline::TimelineCanvas::Row + 37;
    QTest::mouseClick(surface, Qt::LeftButton, Qt::NoModifier, QPoint(100, rowY));
    check(ui->sequence()->tracks[1].locked, "Track lock control works");
    const auto locked = ui->editor().state(); ui->deleteSelected();
    check(ui->editor().state() == locked, "Locked selection delete is rejected transactionally");
    QTest::mouseClick(surface, Qt::LeftButton, Qt::NoModifier, QPoint(100, rowY));
    QTest::mouseClick(surface, Qt::LeftButton, Qt::NoModifier, QPoint(20, rowY));
    check(!ui->sequence()->tracks[1].enabled && playback::activeSource(monitor->controller().description().video, 7000000) == std::nullopt, "Disabled track updates compiled preview");
    ui->undo();
    // Snap candidates and scroll/zoom preserve integer timing and edits.
    snap->setChecked(true); check(canvas->snapFrame(179) == 180 && canvas->snapFrame(183) == 180, "Snapping chooses nearby clip boundary");
    const auto beforeViewport = ui->editor().state();
    ui->findChild<QSlider*>("timelineZoom")->setValue(75); canvas->horizontalScrollBar()->setValue(canvas->horizontalScrollBar()->maximum());
    check(canvas->frameAt(timeline::TimelineCanvas::Header) > 0 && ui->editor().state() == beforeViewport, "Horizontal scroll and zoom do not mutate edit model");
    canvas->horizontalScrollBar()->setValue(0); ui->findChild<QSlider*>("timelineZoom")->setValue(30);
    // Escape aborts the entire pending drag rather than leaving a hidden gesture to commit.
    rect = canvas->clipRect(first.id);
    QTest::mousePress(surface, Qt::LeftButton, Qt::NoModifier, rect.center());
    QMouseEvent pendingMove(QEvent::MouseMove, rect.center() + QPoint(30, 0), surface->mapToGlobal(rect.center() + QPoint(30, 0)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(surface, &pendingMove); const auto beforeEscape = ui->editor().state().project;
    QTest::keyClick(canvas, Qt::Key_Escape); QTest::mouseRelease(surface, Qt::LeftButton, Qt::NoModifier, rect.center() + QPoint(30, 0));
    check(ui->editor().state().project == beforeEscape && ui->editor().state().selection.clipIds.isEmpty(), "Escape cancels drag without moving clip");
    // Ripple deletion moves only subsequent video clips, retaining the audio interval.
    auto later = first; later.id = project::newId(); later.startFrame = 120;
    ui->execute(timeline::InsertClip{sid, video, later});
    ui->execute(timeline::SetSelection{{sid, {video}, {first.id}}}); ui->findChild<QAction*>("timelineRipple")->setChecked(true);
    const auto audioBefore = ui->sequence()->tracks[2]; const auto upperBefore = ui->sequence()->tracks[1]; ui->deleteSelected();
    check(ui->sequence()->tracks[0].clips.size() == 1 && ui->sequence()->tracks[0].clips[0].startFrame == 30 && ui->sequence()->tracks[2] == audioBefore && ui->sequence()->tracks[1] == upperBefore, "Ripple delete shifts later clips on edited track only");
    ui->undo(); ui->findChild<QAction*>("timelineRipple")->setChecked(false);
    // Gap close and frame nudges are keyboard/action commands with exact restoration.
    ui->setPlayhead(105, true); ui->execute(timeline::SetSelection{{sid, {video}, {}}}); ui->closeGapAtPlayhead();
    check(ui->sequence()->tracks[0].clips[1].startFrame == 90, "Close gap shifts later clip by precise empty interval"); ui->undo();
    ui->execute(timeline::SetSelection{{sid, {video}, {later.id}}}); QTest::keyClick(canvas, Qt::Key_Right, Qt::ControlModifier);
    check(ui->sequence()->tracks[0].clips[1].startFrame == 121, "Ctrl+right nudges selection by one frame"); ui->undo();
    ui->setPlayhead(150, true); QTest::keyClick(canvas, Qt::Key_BracketRight);
    check(ui->sequence()->tracks[0].clips[1].durationFrames == 30, "Trim out shortcut uses playhead"); ui->undo();
    // Minimal title placement, edit, timing and real controller output.
    ui->setPlayhead(30, true); ui->addTitle("Session 6 title"); verifyViewer("title placement");
    const auto withTitle = ui->editor().state();
    check(withTitle.project.titles.size() == 1 && ui->sequence()->tracks.last().kind == "title", "Title record/track/clip created in one atomic undo entry");
    const auto titleFrame = playback::renderTitles(monitor->controller().description(), {}, 1000000);
    check(!titleFrame.isNull() && titleFrame.sizeInBytes() <= 1280 * 720 * 4, "Title in a gap renders on bounded sequence canvas");
    const int oldFrames = frameCount; check(waitFor([&] { return frameCount > oldFrames; }), "Title appears through asynchronous live controller");
    auto title = withTitle.project.titles[0]; title.text = "Edited title"; ui->execute(timeline::UpsertTitle{title});
    const auto newTitleFrame = playback::renderTitles(monitor->controller().description(), {}, 1000000);
    check(newTitleFrame != titleFrame, "Title text edit changes preview pixels"); verifyViewer("title text");
    ui->undo(); check(ui->editor().state() == withTitle, "Title text undo restores exact state");
    canvas->verticalScrollBar()->setValue(canvas->verticalScrollBar()->maximum());
    const auto titleClip = ui->sequence()->tracks.last().clips.first();
    QTimer::singleShot(0, &window, [] {
        for (auto* widget : QApplication::topLevelWidgets()) if (widget->objectName() == "titleProperties") {
            widget->findChild<QPlainTextEdit*>("titleText")->setPlainText("Text from real title dialog");
            widget->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        }
    });
    QTest::mouseDClick(surface, Qt::LeftButton, Qt::NoModifier, canvas->clipRect(titleClip.id).center());
    check(ui->editor().state().project.titles[0].text == "Text from real title dialog", "Double-click title edits text through undoable dialog"); ui->undo();
    check(ui->editor().state() == withTitle, "Title dialog undo restores model and selection");
    canvas->verticalScrollBar()->setValue(0);
    ui->undo(); check(ui->editor().state().project.titles.isEmpty(), "Title placement undo removes title, clip and new track together");
    ui->redo(); check(ui->editor().state() == withTitle, "Title placement redo reuses stable IDs");
    ui->setPlayhead(120, true);
    check(playback::renderTitles(monitor->controller().description(), {}, 4000000).isNull(), "Title disappears at its exclusive out frame");
    // Exercise audio-stream selection and normal placement without creating implicit linked clips.
    ui->setSelectedMedia(inspected.media.id); auto* streams = ui->findChild<QComboBox*>("timelineStream");
    int audioStream = -1; for (const auto& s : inspected.media.streams) if (s.kind == "audio") audioStream = s.index;
    streams->setCurrentIndex(streams->findData(audioStream));
    check(ui->placeMedia(inspected.media.id, audio, 270) && ui->sequence()->tracks[2].clips.size() == 2, "Chosen audio stream places on audio track");
    // Failed multi-edit must preserve model AND undo branch.
    const auto beforeBatch = ui->editor().state(); const auto count = ui->editor().undoCount();
    const auto error = ui->executeBatch({timeline::SetTrackLocked{sid, video, true}, timeline::DeleteClip{sid, video, first.id}}, "Invalid locked gesture");
    check(!error.isEmpty() && ui->editor().state() == beforeBatch && ui->editor().undoCount() == count, "Batch failure restores all intermediate changes/history");
    // Every mixed undo/redo round trip preserves exact model, references and selection.
    const auto final = ui->editor().state();
    while (ui->editor().canUndo()) ui->undo();
    check(ui->editor().state().project == project, "Whole UI history returns to opened model");
    while (ui->editor().canRedo()) ui->redo();
    check(ui->editor().state() == final, "Whole UI redo returns exact mixed state");
    const auto saved = output.filePath("edited.veproject");
    check(window.saveProjectPath(saved) && !window.isProjectDirty(), "Save commits current editor and clears dirty");
    ui->execute(timeline::SetSelection{{sid, {video}, {first.id}}}); check(!window.isProjectDirty(), "Selection-only command does not dirty saved document");
    ui->execute(timeline::MoveClip{sid, video, first.id, video, 1}); check(window.isProjectDirty(), "Frame edit dirties document");
    ui->undo(); check(!window.isProjectDirty(), "Undo to saved project clears dirty without losing history");
    check(window.openProjectPath(saved) && window.project() == final.project && !ui->editor().canUndo() && ui->playhead() == 0, "Reopen retains edits and resets history/playhead");
    check(waitFor([&] { return !window.mediaBusy(); }), "Reopen inspection finishes");
    secondsAction->trigger();
    window.close();
    {
        QSettings reopenedSettings(settings.fileName(), QSettings::IniFormat); MainWindow reopened(reopenedSettings, diagnostics);
        check(reopened.findChild<timeline::TimelineWidget*>()->timeDisplay() == TimeDisplay::Seconds && reopened.findChild<QAction*>("timelineSecondsAction")->isChecked(), "Seconds preference survives a fresh application window");
        reopened.close();
    }
    QJsonObject result{{"passed", failures.isEmpty()}, {"failures", failures}, {"checkCount", checks},
        {"evidenceLevel", "Automated offscreen Qt mouse/drop/keyboard events, live render graph/decoded frame/title pixels, command history and atomic save/reopen; human interaction/rendering acceptance pending"},
        {"humanFixture", fixture}};
    QFile report(output.filePath("timeline-ui-result.json"));
    if (!report.open(QIODevice::WriteOnly) || report.write(QJsonDocument(result).toJson()) < 0) return 2;
    return failures.isEmpty() ? 0 : 1;
}

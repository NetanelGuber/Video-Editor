#include "TimelineWidget.h"
#include "TimelineCanvas.h"
#include "PropertyDialogs.h"
#include <QAction>
#include <QComboBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QSlider>
#include <QToolBar>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QMetaMethod>
#include <algorithm>
#include <limits>

namespace editor::timeline {
MediaBinTree::MediaBinTree(QWidget* parent) : QTreeWidget(parent) {
    setDragEnabled(true); setDragDropMode(QAbstractItemView::DragOnly);
    setSupportedDragActions(Qt::CopyAction); setDefaultDropAction(Qt::CopyAction);
}
QStringList MediaBinTree::mimeTypes() const { return {QString::fromLatin1(MediaMime)}; }
QMimeData* MediaBinTree::mimeData(const QList<QTreeWidgetItem*>& items) const {
    auto* mime = new QMimeData;
    if (!items.isEmpty()) {
        const auto id = items.first()->data(0, Qt::UserRole).toString();
        if (!id.isEmpty()) mime->setData(MediaMime, id.toUtf8());
    }
    return mime;
}
TimelineWidget::TimelineWidget(QWidget* parent) : QWidget(parent) {
    setObjectName("timelineArea");
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(4, 4, 4, 4);
    auto* sequenceTools = new QToolBar(this); sequenceTools->setObjectName("sequenceContextToolbar"); layout->addWidget(sequenceTools);
    sequences_ = new QComboBox(this); sequences_->setObjectName("sequenceSelector"); sequenceTools->addWidget(sequences_);
    connect(sequences_, &QComboBox::activated, this, [this](int index) { emit activated(); playhead_ = 0; execute(ActivateSequence{sequences_->itemData(index).toString()}); });
    auto* newAction = sequenceTools->addAction("New sequence…"); newAction->setObjectName("newSequenceAction");
    connect(newAction, &QAction::triggered, this, [this] { bool ok; const auto name = QInputDialog::getText(this, "New sequence", "Sequence name:", QLineEdit::Normal, "Sequence", &ok); if (ok && !name.trimmed().isEmpty()) newSequence(name); });
    auto* nest = sequenceTools->addAction("Nest sequence…"); nest->setObjectName("nestSequenceAction");
    connect(nest, &QAction::triggered, this, [this] {
        QDialog dialog(this); dialog.setObjectName("nestSequenceDialog"); dialog.setWindowTitle("Nest sequence"); QFormLayout form(&dialog);
        QComboBox source; source.setObjectName("nestedSequenceSource");
        for (const auto& s : editor_.state().project.sequences) if (s.id != editor_.state().project.activeSequenceId && s.durationFrames > 0) source.addItem(s.name, s.id);
        if (!source.count()) { emit editError("Create and edit a different sequence first, then open the parent sequence."); return; }
        QCheckBox audio("Include child audio on a separate audio track"); audio.setChecked(true); form.addRow("Source sequence:", &source); form.addRow(&audio);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); form.addRow(&buttons); connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) nestSequence(source.currentData().toString(), audio.isChecked());
    });
    auto* openChild = sequenceTools->addAction("Open child"); openChild->setObjectName("openChildAction");
    connect(openChild, &QAction::triggered, this, [this] {
        if (const auto* s = sequence()) for (const auto& t : s->tracks) for (const auto& c : t.clips) if (!c.sequenceId.isEmpty() && editor_.state().selection.clipIds.contains(c.id)) { const auto id = c.sequenceId; playhead_ = 0; emit activated(); execute(ActivateSequence{id}); return; }
        emit editError("Select a nested clip to open its child sequence.");
    });
    auto* multicam = sequenceTools->addAction("New multicamera…"); multicam->setObjectName("newMulticamAction"); connect(multicam, &QAction::triggered, this, &TimelineWidget::createMulticam);
    auto* ungroup = sequenceTools->addAction("Ungroup cameras"); ungroup->setObjectName("ungroupCamerasAction"); connect(ungroup, &QAction::triggered, this, [this] { if (const auto* s = sequence()) execute(SetMulticam{s->id, {}}); });
    overview_ = sequenceTools->addAction("Camera overview"); overview_->setObjectName("cameraOverviewAction"); overview_->setCheckable(true); connect(overview_, &QAction::toggled, this, &TimelineWidget::cameraOverviewChanged);
    for (int i = 0; i < 4; ++i) { auto* camera = sequenceTools->addAction(QString("Camera %1").arg(i + 1)); camera->setObjectName(QString("camera%1Action").arg(i + 1)); connect(camera, &QAction::triggered, this, [this, i] { switchCamera(i); }); }
    auto* tools = new QToolBar(this); tools->setObjectName("timelineToolbar"); layout->addWidget(tools);
    auto action = [this, tools](const QString& text, const char* name, QKeySequence key, auto callback) {
        auto* a = tools->addAction(text); a->setObjectName(name);
        if (!key.isEmpty()) { a->setShortcut(key); a->setShortcutContext(Qt::WidgetWithChildrenShortcut); addAction(a); }
        connect(a, &QAction::triggered, this, callback); return a;
    };
    undo_ = action("Undo", "timelineUndo", QKeySequence::Undo, [this] { undo(); });
    redo_ = action("Redo", "timelineRedo", QKeySequence::Redo, [this] { redo(); });
    undo_->setShortcutContext(Qt::WindowShortcut); redo_->setShortcutContext(Qt::WindowShortcut);
    tools->addSeparator();
    action("Split", "timelineSplit", QKeySequence(Qt::Key_S), [this] { splitAtPlayhead(); });
    action("Delete", "timelineDelete", QKeySequence(Qt::Key_Delete), [this] { deleteSelected(); });
    action("Set selected video as Primary Video", "timelineSetPrimaryVideo", {}, [this] { setSelectedPrimaryVideo(); });
    action("Trim in", "timelineTrimIn", QKeySequence(Qt::Key_BracketLeft), [this] { trimToPlayhead(true); });
    action("Trim out", "timelineTrimOut", QKeySequence(Qt::Key_BracketRight), [this] { trimToPlayhead(false); });
    ripple_ = action("Ripple", "timelineRipple", QKeySequence(Qt::Key_R), [] {}); ripple_->setCheckable(true);
    ripple_->setToolTip("Ripple on this track: shift later clips only on the edited track. Ripple trim changes only the out point.");
    snap_ = action("Snap", "timelineSnap", QKeySequence(Qt::Key_N), [] {}); snap_->setCheckable(true); snap_->setChecked(true);
    action("Close gap", "timelineCloseGap", {}, [this] { closeGapAtPlayhead(); });
    auto* trackMenu = new QMenu(this);
    for (const auto& kind : {QString("video"), QString("audio"), QString("title")})
        trackMenu->addAction("Add " + kind + " track", this, [this, kind] { addTrack(kind); });
    auto* trackButton = new QPushButton("Add track", this); trackButton->setObjectName("addTrackButton"); trackButton->setMenu(trackMenu); tools->addWidget(trackButton);
    action("Add title", "timelineAddTitle", {}, [this] {
        bool ok = false; const auto text = QInputDialog::getText(this, "Add title", "Title text:", QLineEdit::Normal, "Title", &ok);
        if (ok && !text.isEmpty()) addTitle(text);
    });
    position_ = new QLabel(this); position_->setObjectName("timelinePosition"); position_->setContentsMargins(4, 0, 8, 0); sequenceTools->addWidget(position_);
    sequenceTools->addSeparator();
    auto* primarySummary = new QLabel(this); primarySummary->setObjectName("timelinePrimaryVideo"); primarySummary->setContentsMargins(8, 0, 4, 0); sequenceTools->addWidget(primarySummary);
    auto* placement = new QToolBar(this); placement->setObjectName("timelinePlacementToolbar"); layout->addWidget(placement);
    stream_ = new QComboBox(this); stream_->setObjectName("timelineStream"); stream_->setMinimumContentsLength(10); stream_->setMaximumWidth(240); stream_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); placement->addWidget(stream_);
    auto* insert = placement->addAction("Add selected media at playhead"); insert->setObjectName("timelineInsert");
    insert->setToolTip("Insert the selected Project Media stream at the playhead on a compatible unlocked track.");
    connect(insert, &QAction::triggered, this, [this] { placeMedia(selectedMedia_, {}, playhead_); });
    placement->addWidget(new QLabel("  Zoom ", this));
    zoom_ = new QSlider(Qt::Horizontal, this); zoom_->setObjectName("timelineZoom"); zoom_->setRange(0, 100); zoom_->setValue(50); zoom_->setMaximumWidth(160); placement->addWidget(zoom_);
    auto* properties = new QAction("Clip…", this); properties->setObjectName("timelineClipProperties");
    connect(properties, &QAction::triggered, this, [this] { requestProperties("clip"); });
    auto* effects = new QAction("Effects…", this); effects->setObjectName("timelineClipEffects");
    connect(effects, &QAction::triggered, this, [this] { requestProperties("effects"); });
    auto* audio = new QAction("Track audio…", this); audio->setObjectName("timelineTrackAudio");
    audio->setToolTip("Adjust the selected audio track, including its clip, routing and automation.");
    connect(audio, &QAction::triggered, this, [this] { requestProperties("audio"); });
    auto* buses = new QAction("Audio buses…", this); buses->setObjectName("timelineAudioBuses");
    connect(buses, &QAction::triggered, this, &TimelineWidget::manageAudioBuses);
    // Sequence management lives in the Sequence menu. Only relevant camera/child actions
    // join the sequence header, leaving routine edits easy to scan.
    for (auto* a : sequenceTools->actions()) if (!a->objectName().isEmpty()) sequenceTools->removeAction(a);
    canvas_ = new TimelineCanvas(*this); layout->addWidget(canvas_, 1);
    connect(zoom_, &QSlider::valueChanged, canvas_, &TimelineCanvas::setZoom);
    auto shortcut = [this](QKeySequence key, auto callback) {
        auto* a = new QAction(this); a->setShortcut(key); a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        addAction(a); connect(a, &QAction::triggered, this, callback);
    };
    shortcut(Qt::Key_Space, [this] { emit activated(); emit playRequested(); });
    shortcut(Qt::Key_Left, [this] { stepPlayhead(-1); }); shortcut(Qt::Key_Right, [this] { stepPlayhead(1); });
    shortcut(Qt::CTRL | Qt::Key_Left, [this] { moveSelected(-1); }); shortcut(Qt::CTRL | Qt::Key_Right, [this] { moveSelected(1); });
    shortcut(Qt::Key_Home, [this] { setPlayhead(0, true); canvas_->ensurePlayheadVisible(); });
    shortcut(Qt::Key_End, [this] { if (const auto* s = sequence()) setPlayhead(s->durationFrames, true); canvas_->ensurePlayheadVisible(); });
    shortcut(Qt::Key_Escape, [this] { canvas_->cancelGesture(); execute(SetSelection{{editor_.state().project.activeSequenceId, {}, {}}}); });
    refresh();
}
const Sequence* TimelineWidget::sequence() const {
    const auto& p = editor_.state().project;
    for (const auto& s : p.sequences) if (s.id == p.activeSequenceId) return &s;
    return nullptr;
}
EditMode TimelineWidget::mode() const { return ripple_->isChecked() ? EditMode::Ripple : EditMode::Normal; }
void TimelineWidget::setProject(Project project) {
    canvas_->cancelGesture();
    const auto error = editor_.replaceProject(std::move(project));
    if (!error.isEmpty()) { emit editError(error); return; }
    playhead_ = 0; selectedMedia_.clear(); setSelectedMedia({}); refresh();
}
QString TimelineWidget::execute(const Command& command) { return executeBatch({command}, {}); }
QString TimelineWidget::executeBatch(const QVector<Command>& commands, const QString& label) {
    const auto before = editor_.state().project;
    const auto error = editor_.executeBatch(commands, label);
    if (!error.isEmpty()) { emit editError(error); return error; }
    publish(before); return {};
}
void TimelineWidget::publish(const Project& before) {
    if (const auto* s = sequence()) playhead_ = std::min(playhead_, s->durationFrames);
    refresh(); emit stateChanged();
    if (before != editor_.state().project) emit projectChanged();
}
void TimelineWidget::undo() { const auto before = editor_.state().project; if (editor_.undo()) publish(before); }
void TimelineWidget::redo() { const auto before = editor_.state().project; if (editor_.redo()) publish(before); }
void TimelineWidget::refresh() {
    { QSignalBlocker block(sequences_); sequences_->clear(); const auto& p = editor_.state().project; for (const auto& s : p.sequences) sequences_->addItem(s.name, s.id); sequences_->setCurrentIndex(sequences_->findData(p.activeSequenceId)); }
    const auto* s = sequence(); const auto count = s && s->multicam ? s->multicam->cameraTrackIds.size() : 0;
    overview_->setEnabled(count > 0); if (!count) overview_->setChecked(false);
    for (int i = 0; i < 4; ++i) findChild<QAction*>(QString("camera%1Action").arg(i + 1))->setEnabled(i < count);
    undo_->setEnabled(editor_.canUndo()); redo_->setEnabled(editor_.canRedo());
    undo_->setToolTip("Undo " + editor_.undoLabel()); redo_->setToolTip("Redo " + editor_.redoLabel());
    const auto& selected = editor_.state().selection;
    const Track* track = nullptr; const Clip* clip = nullptr;
    bool nested = false, editableSelection = false;
    if (s) for (const auto& t : s->tracks) {
        if (selected.trackIds.contains(t.id)) track = &t;
        for (const auto& c : t.clips) if (selected.clipIds.contains(c.id)) {
            editableSelection |= !t.locked; nested |= !c.sequenceId.isEmpty();
            if (selected.clipIds.size() == 1) { clip = &c; track = &t; }
        }
    }
    findChild<QAction*>("timelineDelete")->setEnabled(editableSelection);
    for (const auto* name : {"timelineTrimIn", "timelineTrimOut"}) findChild<QAction*>(name)->setEnabled(editableSelection);
    auto* primaryAction = findChild<QAction*>("timelineSetPrimaryVideo");
    const bool canSetPrimary = s && clip && clip->kind == "video" && !clip->mediaId.isEmpty();
    const bool canClearPrimary = s && selected.clipIds.isEmpty() && !s->primaryVideoMediaId.isEmpty();
    primaryAction->setEnabled(canSetPrimary || canClearPrimary);
    const bool alreadyPrimary = canSetPrimary && s->primaryVideoMediaId == clip->mediaId && s->primaryVideoStreamIndex == clip->streamIndex;
    primaryAction->setText(canClearPrimary ? tr("Clear Primary Video") : alreadyPrimary ? tr("Selected video is Primary Video") : tr("Set selected video as Primary Video"));
    auto* primarySummary = findChild<QLabel*>("timelinePrimaryVideo");
    if (s) {
        QString summary = tr("Primary video: none · export follows sequence FPS %1/%2")
            .arg(s->frameRate.numerator).arg(s->frameRate.denominator);
        if (!s->primaryVideoMediaId.isEmpty()) {
            for (const auto& m : editor_.state().project.media) if (m.id == s->primaryVideoMediaId)
                for (const auto& stream : m.streams) if (stream.index == s->primaryVideoStreamIndex && stream.kind == "video") {
                    summary = stream.frameRate.numerator > 0 && stream.frameRate.denominator > 0 ?
                        tr("Primary video: %1 · %2/%3 fps").arg(m.name).arg(stream.frameRate.numerator).arg(stream.frameRate.denominator) :
                        tr("Primary video: %1 · frame rate unavailable, using sequence FPS").arg(m.name);
                }
        }
        primarySummary->setText(summary);
        primarySummary->setToolTip(tr("The primary video's inspected frame rate supplies the default export FPS. Changing it does not retime clips or the sequence."));
    } else primarySummary->clear();
    findChild<QAction*>("timelineClipProperties")->setEnabled(clip && track && !track->locked);
    findChild<QAction*>("timelineClipEffects")->setEnabled(clip && track && !track->locked);
    findChild<QAction*>("timelineTrackAudio")->setEnabled(track && track->kind == "audio" && !track->locked && selected.trackIds.size() == 1);
    findChild<QAction*>("timelineInsert")->setEnabled(!selectedMedia_.isEmpty() && stream_->count() > 0);
    auto* context = findChild<QToolBar*>("sequenceContextToolbar");
    auto* child = findChild<QAction*>("openChildAction"); child->setEnabled(nested);
    if (nested && !context->actions().contains(child)) context->addAction(child);
    if (!nested) context->removeAction(child);
    findChild<QAction*>("ungroupCamerasAction")->setEnabled(count > 0);
    for (const auto* name : {"cameraOverviewAction", "camera1Action", "camera2Action", "camera3Action", "camera4Action"}) {
        auto* a = findChild<QAction*>(name); const bool useful = count > 0 && a->isEnabled();
        if (useful && !context->actions().contains(a)) context->addAction(a);
        if (!useful) context->removeAction(a);
    }
    refreshPosition(); canvas_->refresh();
}
bool TimelineWidget::cameraOverview() const { return overview_->isChecked(); }
void TimelineWidget::newSequence(const QString& name) {
    auto child = newProject().sequences[0]; child.name = name;
    if (const auto* s = sequence()) { child.frameRate = s->frameRate; child.width = s->width; child.height = s->height; child.sampleRate = s->sampleRate; }
    const auto id = child.id; playhead_ = 0; emit activated(); executeBatch({AddSequence{child}, ActivateSequence{id}}, "New sequence");
}
bool TimelineWidget::nestSequence(const QString& id, bool audio) {
    const auto* parent = sequence(); if (!parent) return false;
    const auto sid = parent->id; QVector<Command> commands;
    for (const auto& child : editor_.state().project.sequences) if (child.id == id && child.durationFrames > 0) {
        for (const auto& kind : audio ? QStringList{"video", "audio"} : QStringList{"video"}) {
            auto tid = destination(kind);
            if (tid.isEmpty()) { Track t; t.id = newId(); t.name = kind + " nested"; t.kind = kind; tid = t.id; commands.append(AddTrack{sid, t}); }
            Clip c; c.id = newId(); c.name = child.name; c.kind = kind; c.sequenceId = child.id; c.startFrame = playhead_; c.durationFrames = c.sourceDurationTicks = child.durationFrames;
            commands.append(InsertClip{sid, tid, c, mode()});
            if (kind == "video") commands.append(SetSelection{{sid, {tid}, {c.id}}});
        }
        emit activated(); return executeBatch(commands, "Nest sequence").isEmpty();
    }
    emit editError("Choose a nonempty child sequence."); return false;
}
void TimelineWidget::switchCamera(int index) {
    const auto* s = sequence(); if (!s || !s->multicam || index < 0 || index >= s->multicam->cameraTrackIds.size()) return;
    emit activated();
    execute(SwitchCamera{s->id, s->multicam->cameraTrackIds[index], playhead_});
}
void TimelineWidget::createMulticam() {
    const auto* current = sequence(); if (!current) return;
    const auto settings = *current;
    const TimeFormat time{current->frameRate, timeDisplay_};
    QDialog dialog(this); dialog.setObjectName("multicamDialog"); dialog.setWindowTitle("New multicamera sequence"); QFormLayout form(&dialog);
    QLineEdit name("Multicamera"); name.setObjectName("multicamName"); form.addRow("Name:", &name);
    QLabel help("Choose 2–4 cameras. Source offsets align the same event at sequence start.\nThe shortest remaining camera sets the duration. Audio stays on camera 1."); form.addRow(&help);
    QVector<QComboBox*> cameras; QVector<QLineEdit*> offsets;
    for (int i = 0; i < 4; ++i) {
        auto* camera = new QComboBox(&dialog); camera->setObjectName(QString("multicamSource%1").arg(i + 1)); camera->addItem("None", "");
        for (const auto& m : editor_.state().project.media) for (const auto& stream : m.streams) if (stream.kind == "video") { camera->addItem(m.name + QString(" · stream %1").arg(stream.index), m.id + "/" + QString::number(stream.index)); }
        auto* offset = new QLineEdit(&dialog); offset->setObjectName(QString("multicamOffset%1").arg(i + 1)); time.initialize(offset, 0);
        cameras.append(camera); offsets.append(offset); form.addRow(QString("Camera %1:").arg(i + 1), camera); form.addRow(time.caption("Source offset"), offset);
    }
    QLabel error; error.setObjectName("multicamError"); error.setWordWrap(true); form.addRow(&error);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        auto s = newProject().sequences[0]; s.name = name.text().trimmed(); s.frameRate = time.rate; s.width = settings.width; s.height = settings.height; s.sampleRate = settings.sampleRate; s.tracks.clear();
        Multicam group; qint64 duration = std::numeric_limits<qint64>::max(); QSet<QString> used;
        const Media* audioMedia = nullptr; qint64 audioOffset = 0;
        for (int i = 0; i < cameras.size(); ++i) {
            const auto selection = cameras[i]->currentData().toString(); if (selection.isEmpty()) continue;
            if (used.contains(selection)) { error.setText("Choose distinct camera sources."); return; } used.insert(selection);
            qint64 offset; if (!time.read(offsets[i], offset)) { error.setText("Enter a valid source offset."); return; }
            const auto parts = selection.split('/');
            for (const auto& m : editor_.state().project.media) if (m.id == parts[0]) for (const auto& stream : m.streams) if (stream.index == parts[1].toInt()) {
                const auto frames = ticksToFrames(stream.durationTicks, stream.timeBase, s.frameRate, Rounding::Floor).value_or(0);
                if (offset >= frames) { error.setText("Source offset must be before the camera ends."); return; }
                Track t; t.id = newId(); t.name = QString("Camera %1 · %2").arg(group.cameraTrackIds.size() + 1).arg(m.name);
                Clip c; c.id = newId(); c.name = m.name; c.mediaId = m.id; c.streamIndex = stream.index; c.durationFrames = frames - offset;
                c.sourceInTicks = framesToTicks(offset, s.frameRate, stream.timeBase, Rounding::Nearest).value_or(0);
                t.clips = {c}; s.tracks.append(t); group.cameraTrackIds.append(t.id); duration = std::min(duration, c.durationFrames);
                if (!audioMedia) { audioMedia = &m; audioOffset = offset; }
            }
        }
        if (group.cameraTrackIds.size() < 2 || s.name.isEmpty()) { error.setText("Enter a name and select at least two cameras."); return; }
        for (const auto& cameraTrack : s.tracks) if (!cameraTrack.clips.isEmpty()) {
            s.primaryVideoMediaId = cameraTrack.clips.first().mediaId;
            s.primaryVideoStreamIndex = cameraTrack.clips.first().streamIndex;
            break;
        }
        s.durationFrames = duration;
        for (auto& t : s.tracks) { auto& c = t.clips[0]; c.durationFrames = duration;
            for (const auto& m : editor_.state().project.media) if (m.id == c.mediaId) for (const auto& stream : m.streams) if (stream.index == c.streamIndex) c.sourceDurationTicks = framesToTicks(duration, s.frameRate, stream.timeBase, Rounding::Nearest).value_or(0);
        }
        if (audioMedia) for (const auto& stream : audioMedia->streams) if (stream.kind == "audio") {
            const auto in = framesToTicks(audioOffset, s.frameRate, stream.timeBase, Rounding::Nearest).value_or(0);
            const auto span = framesToTicks(duration, s.frameRate, stream.timeBase, Rounding::Nearest).value_or(0);
            if (in <= stream.durationTicks && span <= stream.durationTicks - in) {
                Track t; t.id = newId(); t.name = "Camera 1 audio"; t.kind = "audio";
                Clip c; c.id = newId(); c.name = audioMedia->name; c.kind = "audio"; c.mediaId = audioMedia->id; c.streamIndex = stream.index; c.durationFrames = duration; c.sourceInTicks = in; c.sourceDurationTicks = span; t.clips = {c}; s.tracks.append(t);
            }
            break;
        }
        group.cuts = {{0, group.cameraTrackIds[0]}}; s.multicam = group; const auto id = s.id;
        const auto result = executeBatch({AddSequence{s}, ActivateSequence{id}}, "New multicamera sequence");
        if (!result.isEmpty()) { error.setText(result); return; }
        playhead_ = 0; refreshPosition(); dialog.accept();
    });
    emit activated(); dialog.exec();
}
void TimelineWidget::setTimeDisplay(TimeDisplay display) {
    timeDisplay_ = display; refreshPosition(); canvas_->viewport()->update();
}
void TimelineWidget::refreshPosition() {
    const auto* s = sequence();
    if (timeDisplay_ == TimeDisplay::Seconds && s) {
        const double seconds = static_cast<double>(playhead_) * s->frameRate.denominator / s->frameRate.numerator;
        position_->setText(tr("  %1 s").arg(QString::number(seconds, 'f', 3)));
    } else position_->setText(tr("  Frame %1").arg(playhead_));
}
void TimelineWidget::setPlayhead(qint64 frame, bool seek) {
    const auto* s = sequence();
    playhead_ = std::clamp(frame, 0LL, s ? s->durationFrames : 0LL);
    refreshPosition(); canvas_->viewport()->update();
    if (seek) { emit activated(); emit seekRequested(playhead_); }
}
void TimelineWidget::stepPlayhead(int direction) {
    const auto* s = sequence(); if (!s) return;
    const auto target = direction < 0 ? std::max<qint64>(0, playhead_ - 1) : playhead_ < s->durationFrames ? playhead_ + 1 : playhead_;
    setPlayhead(target, true); canvas_->ensurePlayheadVisible();
}
void TimelineWidget::setSelectedMedia(const QString& id) {
    const auto oldStream = stream_->currentData().toInt(); const bool same = id == selectedMedia_;
    selectedMedia_ = id; const QSignalBlocker block(stream_); stream_->clear();
    for (const auto& m : editor_.state().project.media) if (m.id == id)
        for (const auto& s : m.streams) stream_->addItem(QString("%1 · stream %2 · %3").arg(m.name).arg(s.index).arg(s.kind), s.index);
    if (same && stream_->findData(oldStream) >= 0) stream_->setCurrentIndex(stream_->findData(oldStream));
    findChild<QAction*>("timelineInsert")->setEnabled(!id.isEmpty() && stream_->count() > 0);
}
void TimelineWidget::setSelectedPrimaryVideo() {
    const auto* s = sequence();
    if (!s) return;
    if (editor_.state().selection.clipIds.isEmpty() && !s->primaryVideoMediaId.isEmpty()) {
        execute(SetPrimaryVideo{s->id, {}, -1});
        return;
    }
    if (editor_.state().selection.clipIds.size() != 1) return;
    for (const auto& t : s->tracks) for (const auto& c : t.clips)
        if (editor_.state().selection.clipIds.contains(c.id) && c.kind == "video" && !c.mediaId.isEmpty()) {
            execute(SetPrimaryVideo{s->id, c.mediaId, c.streamIndex});
            return;
        }
}
void TimelineWidget::requestProperties(const QString& section) {
    emit activated();
    if (isSignalConnected(QMetaMethod::fromSignal(&TimelineWidget::propertiesRequested))) emit propertiesRequested(section);
    else if (section == "effects") editClipEffects();
    else if (section == "audio") editTrackAudio();
    else editClipProperties();
}
QString TimelineWidget::destination(const QString& kind) const {
    const auto* s = sequence(); if (!s) return {};
    for (const auto& id : editor_.state().selection.trackIds)
        for (const auto& t : s->tracks) if (t.id == id && t.kind == kind && !t.locked) return id;
    for (const auto& t : s->tracks) if (t.kind == kind && !t.locked) return t.id;
    return {};
}
bool TimelineWidget::placeMedia(const QString& mediaId, const QString& trackId, qint64 frame) {
    const auto* s = sequence(); if (!s) return false;
    const auto sequenceId = s->id; QVector<Command> commands;
    for (const auto& m : editor_.state().project.media) if (m.id == mediaId) {
        const Track* target = nullptr;
        for (const auto& t : s->tracks) if (t.id == trackId) target = &t;
        const Stream* source = nullptr;
        for (const auto& stream : m.streams) {
            if (target && stream.kind != target->kind) continue;
            if (!source) source = &stream;
            if (mediaId == selectedMedia_ && stream.index == stream_->currentData().toInt()) { source = &stream; break; }
        }
        if (!source || source->durationTicks <= 0 || (target && target->locked)) {
            emit editError("Choose an unlocked video/audio track and a source with a known duration."); return false;
        }
        auto dest = target ? target->id : destination(source->kind);
        if (dest.isEmpty()) {
            Track track; track.id = newId(); track.name = source->kind + " track"; track.kind = source->kind; dest = track.id;
            commands.append(AddTrack{sequenceId, track});
        }
        // Floor prevents a timeline interval extending beyond the reported source endpoint.
        const auto duration = ticksToFrames(source->durationTicks, source->timeBase, s->frameRate, Rounding::Floor);
        const auto ticks = duration ? framesToTicks(*duration, s->frameRate, source->timeBase, Rounding::Nearest) : std::nullopt;
        if (!duration || *duration < 1 || !ticks || *ticks < 1 || *ticks > source->durationTicks) {
            emit editError("The source is too short or its timing cannot be represented in sequence frames."); return false;
        }
        Clip clip; clip.id = newId(); clip.name = m.name; clip.kind = source->kind; clip.mediaId = m.id; clip.streamIndex = source->index;
        clip.startFrame = frame; clip.durationFrames = *duration; clip.sourceDurationTicks = *ticks;
        commands.append(InsertClip{sequenceId, dest, clip, mode()});
        commands.append(SetSelection{{sequenceId, {dest}, {clip.id}}});
        emit activated(); const bool placed = executeBatch(commands, "Place media").isEmpty();
        if (placed) canvas_->setFocus();
        return placed;
    }
    emit editError("Select imported media first."); return false;
}
void TimelineWidget::addTrack(const QString& kind) {
    if (const auto* s = sequence()) {
        Track t; t.id = newId(); t.kind = kind; t.name = QString("%1 %2").arg(kind).arg(s->tracks.size() + 1);
        executeBatch({AddTrack{s->id, t}, SetSelection{{s->id, {t.id}, {}}}}, "Add track"); canvas_->setFocus();
    }
}
void TimelineWidget::addTitle(const QString& text) {
    const auto* s = sequence(); if (!s || text.isEmpty()) return;
    const auto sid = s->id; QVector<Command> commands;
    auto dest = destination("title");
    if (dest.isEmpty()) { Track t; t.id = newId(); t.kind = "title"; t.name = "Titles"; dest = t.id; commands.append(AddTrack{sid, t}); }
    Title title; title.id = newId(); title.text = text;
    Clip clip; clip.id = newId(); clip.name = text; clip.kind = "title"; clip.titleId = title.id; clip.startFrame = playhead_;
    clip.durationFrames = scaleTime(3, s->frameRate.numerator, s->frameRate.denominator, Rounding::Nearest).value_or(90);
    commands.append(UpsertTitle{title}); commands.append(InsertClip{sid, dest, clip, mode()});
    commands.append(SetSelection{{sid, {dest}, {clip.id}}}); emit activated(); executeBatch(commands, "Place title"); canvas_->setFocus();
}
void TimelineWidget::editClipProperties(const QString& trackId, const QString& clipId) {
    const auto* s = sequence(); if (!s) return;
    const auto sid = s->id;
    const auto& selection = editor_.state().selection;
    if (clipId.isEmpty() && selection.clipIds.size() != 1) { emit editError("Select one clip to edit its properties."); return; }
    for (const auto& t : s->tracks) for (const auto& c : t.clips)
        if ((clipId.isEmpty() ? selection.clipIds.contains(c.id) : c.id == clipId) && (trackId.isEmpty() || t.id == trackId)) {
            if (t.locked) { emit editError("Unlock the track before editing properties."); return; }
            const auto track = t; const auto clip = c; // Dialog event loop must not retain model references.
            if (clip.kind == "audio") {
                editAudioProperties(track, &clip, "clipAudioProperties");
            } else if (clip.kind == "title") {
                emit activated();
                for (const auto& title : editor_.state().project.titles) if (title.id == clip.titleId) {
                    const auto original = title;
                    if (const auto edited = titleDialog(this, original, clip.durationFrames, {s->frameRate, timeDisplay_}))
                        executeBatch({UpsertTitle{edited->title}, TrimClip{sid, track.id, clip.id, clip.startFrame, edited->durationFrames}}, "Edit title properties");
                    break;
                }
            } else editClipEffects(track.id, clip.id);
            return;
        }
}
void TimelineWidget::editClipEffects(const QString& trackId, const QString& clipId) {
    const auto* s = sequence(); if (!s) return;
    const auto sid = s->id; const auto& selection = editor_.state().selection;
    if (clipId.isEmpty() && selection.clipIds.size() != 1) { emit editError("Select one clip to edit effects or speed."); return; }
    for (const auto& t : s->tracks) for (const auto& c : t.clips)
        if ((clipId.isEmpty() ? selection.clipIds.contains(c.id) : c.id == clipId) && (trackId.isEmpty() || trackId == t.id)) {
            if (t.locked) { emit editError("Unlock the track before editing effects or speed."); return; }
            const auto tid = t.id; const auto clip = c; emit activated();
            if (const auto effects = effectsDialog(this, clip, std::clamp(playhead_ - clip.startFrame, 0LL, clip.durationFrames - 1), {s->frameRate, timeDisplay_})) execute(SetClipEffects{sid, tid, clip.id, *effects});
            return;
        }
}
void TimelineWidget::editTrackAudio(const QString& trackId) {
    const auto* s = sequence(); if (!s) return;
    for (const auto& t : s->tracks) if (trackId.isEmpty() ? editor_.state().selection.trackIds.contains(t.id) : t.id == trackId) {
        if (t.kind != "audio") { emit editError("Select an audio track to adjust its mix."); return; }
        if (t.locked) { emit editError("Unlock the track before editing audio."); return; }
        const auto copy = t;
        Clip selectedClip; const Clip* clip = nullptr;
        const auto& selectedIds = editor_.state().selection.clipIds;
        if (selectedIds.size() == 1)
            for (const auto& candidate : copy.clips) if (candidate.id == selectedIds.first()) { selectedClip = candidate; clip = &selectedClip; break; }
        editAudioProperties(copy, clip, "trackAudioProperties");
        return;
    }
    emit editError("Select an audio track first.");
}
void TimelineWidget::editAudioProperties(const Track& track, const Clip* clip, const char* dialogObjectName) {
    const auto* s = sequence(); if (!s) return;
    emit activated();
    if (const auto edited = audioDialog(this, s->id, track, clip, {s->frameRate, timeDisplay_}, dialogObjectName, s->audioBuses)) {
        QVector<Command> commands{edited->track};
        if (edited->clip) commands.append(*edited->clip);
        executeBatch(commands, "Edit audio properties");
    }
}
void TimelineWidget::manageAudioBuses() {
    const auto* s = sequence(); if (!s) return;
    const auto sid = s->id; const auto original = s->audioBuses; emit activated();
    const auto edited = audioBusManagerDialog(this, original); if (!edited) return;
    QVector<Command> commands;
    for (const auto& bus : *edited) commands.append(UpsertAudioBus{sid, bus});
    for (const auto& old : original) if (std::none_of(edited->begin(), edited->end(), [&](const auto& bus) { return bus.id == old.id; }))
        commands.append(RemoveAudioBus{sid, old.id});
    if (!commands.isEmpty()) executeBatch(commands, "Edit audio buses");
}
void TimelineWidget::select(const QString& trackId, const QString& clipId, bool toggle) {
    const auto* s = sequence(); if (!s) return;
    Selection selection = toggle ? editor_.state().selection : Selection{s->id, {}, {}};
    selection.sequenceId = s->id;
    selection.trackIds = trackId.isEmpty() ? QStringList{} : QStringList{trackId};
    if (!clipId.isEmpty()) {
        if (toggle && selection.clipIds.contains(clipId)) selection.clipIds.removeAll(clipId);
        else if (!selection.clipIds.contains(clipId)) selection.clipIds.append(clipId);
    }
    execute(SetSelection{selection});
}
void TimelineWidget::splitAtPlayhead() {
    const auto* s = sequence(); if (!s) return;
    const auto selected = editor_.state().selection.clipIds; QVector<Command> commands;
    for (const auto& t : s->tracks) for (const auto& c : t.clips)
        if (!t.locked && (selected.isEmpty() || selected.contains(c.id)) && playhead_ > c.startFrame && playhead_ < c.startFrame + c.durationFrames) {
            QStringList effects; for (const auto& e : c.effects) { Q_UNUSED(e); effects.append(newId()); }
            commands.append(SplitClip{s->id, t.id, c.id, playhead_, newId(), effects});
        }
    if (commands.isEmpty()) emit editError("Place the playhead inside an unlocked clip to split it.");
    else executeBatch(commands, "Split at playhead");
}
void TimelineWidget::deleteSelected() {
    const auto* s = sequence(); if (!s) return;
    QVector<Command> commands;
    for (const auto& t : s->tracks) {
        auto clips = t.clips; std::sort(clips.begin(), clips.end(), [](const auto& a, const auto& b) { return a.startFrame > b.startFrame; });
        for (const auto& c : clips) if (editor_.state().selection.clipIds.contains(c.id)) commands.append(DeleteClip{s->id, t.id, c.id, mode()});
    }
    executeBatch(commands, "Delete selection");
}
void TimelineWidget::closeGapAtPlayhead() {
    const auto* s = sequence(); if (!s) return;
    for (const auto& t : s->tracks) if (editor_.state().selection.trackIds.contains(t.id))
        for (const auto& gap : gaps(*s, t)) if (playhead_ >= gap.startFrame && playhead_ < gap.startFrame + gap.durationFrames) {
            execute(CloseGap{s->id, t.id, gap.startFrame, gap.durationFrames}); return;
        }
    emit editError("Select a track and place the playhead inside an empty gap.");
}
void TimelineWidget::trimToPlayhead(bool in) {
    const auto* s = sequence(); if (!s) return; QVector<Command> commands;
    for (const auto& t : s->tracks) for (const auto& c : t.clips) if (editor_.state().selection.clipIds.contains(c.id)) {
        const auto stop = c.startFrame + c.durationFrames;
        if (playhead_ <= c.startFrame || playhead_ >= stop) { emit editError("Place the playhead strictly inside each selected clip."); return; }
        commands.append(TrimClip{s->id, t.id, c.id, in ? playhead_ : c.startFrame, in ? stop - playhead_ : playhead_ - c.startFrame, mode()});
    }
    executeBatch(commands, in ? "Trim in to playhead" : "Trim out to playhead");
}
void TimelineWidget::moveSelected(int direction) {
    const auto* s = sequence(); if (!s) return; QVector<Command> commands;
    for (const auto& t : s->tracks) for (const auto& c : t.clips) if (editor_.state().selection.clipIds.contains(c.id)) {
        if ((direction < 0 && c.startFrame == 0) || (direction > 0 && c.startFrame + c.durationFrames == std::numeric_limits<qint64>::max())) {
            emit editError("Move would exceed sequence frame bounds."); return;
        }
        commands.append(MoveClip{s->id, t.id, c.id, t.id, c.startFrame + direction, mode()});
    }
    executeBatch(commands, "Nudge selection");
}
}

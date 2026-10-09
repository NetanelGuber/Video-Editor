#include "TimelineCanvas.h"
#include <QAction>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QInputDialog>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSlider>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

namespace editor::timeline {
namespace {
constexpr auto MaxFrame = std::numeric_limits<qint64>::max();
qint64 roundedFrame(long double frame) {
    if (frame <= 0) return 0;
    if (frame >= static_cast<long double>(MaxFrame)) return MaxFrame;
    return static_cast<qint64>(std::round(frame));
}
}
TimelineCanvas::TimelineCanvas(TimelineWidget& owner) : QAbstractScrollArea(&owner), owner_(owner) {
    setObjectName("timelineCanvas"); setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 140); viewport()->setAcceptDrops(true); viewport()->setMouseTracking(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn); setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(viewport(), &QWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        const auto tid = trackAt(point.y()); QString cid;
        if (const auto* clip = hitClip(point)) cid = clip->id;
        if (tid.isEmpty()) return;
        emit owner_.activated(); owner_.select(tid, cid, false);
        QMenu menu(this);
        for (const auto* name : {"timelineClipProperties", "timelineClipEffects", "timelineTrackAudio", "timelineSplit", "timelineTrimIn", "timelineTrimOut", "timelineDelete"}) menu.addAction(owner_.findChild<QAction*>(name));
        menu.exec(viewport()->mapToGlobal(point));
    });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    scrub_.setSingleShot(true); scrub_.setInterval(40);
    connect(&scrub_, &QTimer::timeout, this, [this] { emit owner_.seekRequested(owner_.playhead()); });
    autoScroll_.setInterval(40);
    connect(&autoScroll_, &QTimer::timeout, this, [this] {
        if (gesture_ == Gesture::None) return;
        const int direction = cursor_.x() < Header + 16 ? -1 : cursor_.x() > viewport()->width() - 16 ? 1 : 0;
        if (direction) { auto* bar = horizontalScrollBar(); bar->setValue(bar->value() + direction * bar->singleStep()); updateGesture(cursor_); }
    });
}
qint64 TimelineCanvas::firstFrame() const { return static_cast<qint64>(horizontalScrollBar()->value()) * scrollUnit_; }
int TimelineCanvas::xAt(qint64 frame) const {
    const auto value = Header + static_cast<long double>(frame - firstFrame()) * pixels_;
    return static_cast<int>(std::round(std::clamp(value, -1000000.0L, 1000000.0L)));
}
qint64 TimelineCanvas::frameAt(int x) const {
    return roundedFrame(static_cast<long double>(firstFrame()) + (x - Header) / pixels_);
}
void TimelineCanvas::setZoom(int value) {
    // Preserve the visible frame near the playhead when zoom changes.
    const auto anchor = owner_.playhead(); const auto first = firstFrame();
    pixels_ = std::pow(2.0, (value - 50) / 12.5) * 4.0;
    refresh(); horizontalScrollBar()->setValue(static_cast<int>(std::min<qint64>(horizontalScrollBar()->maximum(), first / scrollUnit_)));
    if (anchor >= first && xAt(anchor) > viewport()->width()) ensurePlayheadVisible();
}
void TimelineCanvas::refresh() {
    const auto* s = owner_.sequence(); const auto first = firstFrame();
    const auto visible = std::max<qint64>(1, static_cast<qint64>(std::max(1, viewport()->width() - Header) / pixels_));
    const auto duration = s ? s->durationFrames : 0;
    // Always leave room to place media past the current end without extending the document by scrolling.
    const auto extra = std::min<qint64>(MaxFrame - duration, std::max<qint64>(300, visible));
    const auto maximum = std::max<qint64>(0, duration + extra - visible);
    scrollUnit_ = std::max<qint64>(1, maximum / 1000000 + (maximum % 1000000 != 0));
    horizontalScrollBar()->setRange(0, static_cast<int>(maximum / scrollUnit_));
    horizontalScrollBar()->setPageStep(static_cast<int>(std::max<qint64>(1, visible / scrollUnit_)));
    horizontalScrollBar()->setSingleStep(static_cast<int>(std::max<qint64>(1, visible / 12 / scrollUnit_)));
    horizontalScrollBar()->setValue(static_cast<int>(std::min<qint64>(horizontalScrollBar()->maximum(), first / scrollUnit_)));
    verticalScrollBar()->setRange(0, std::max(0, (s ? static_cast<int>(s->tracks.size()) : 0) * Row - (viewport()->height() - Ruler)));
    verticalScrollBar()->setPageStep(viewport()->height() - Ruler); verticalScrollBar()->setSingleStep(Row);
    viewport()->update();
}
void TimelineCanvas::resizeEvent(QResizeEvent* e) { QAbstractScrollArea::resizeEvent(e); refresh(); }
void TimelineCanvas::setOfflineMediaIds(const QStringList& ids) {
    const QSet<QString> next(ids.begin(), ids.end());
    if (next != offlineMediaIds_) { offlineMediaIds_ = next; viewport()->update(); }
}
QString TimelineCanvas::clipCaption(const Clip& clip) const {
    auto caption = clip.name;
    if (clip.kind == "title") for (const auto& title : owner_.editor().state().project.titles)
        if (title.id == clip.titleId) { caption = title.text; break; }
    if (clip.kind == "audio") caption += clip.muted ? " · muted" : QString(" · %1×").arg(clip.gain);
    bool offline = offlineMediaIds_.contains(clip.mediaId);
    if (!clip.sequenceId.isEmpty()) {
        caption = tr("Sequence · ") + caption;
        for (const auto& id : sequenceMediaIds(owner_.editor().state().project, clip.sequenceId)) offline |= offlineMediaIds_.contains(id);
    }
    if (offline) caption = tr("OFFLINE · ") + caption;
    return caption;
}
void TimelineCanvas::focusInEvent(QFocusEvent* e) { QAbstractScrollArea::focusInEvent(e); emit owner_.activated(); }
const Track* TimelineCanvas::track(const QString& id) const {
    if (const auto* s = owner_.sequence()) for (const auto& t : s->tracks) if (t.id == id) return &t;
    return nullptr;
}
QString TimelineCanvas::trackAt(int y) const {
    const auto* s = owner_.sequence(); if (!s || y < Ruler) return {};
    const auto row = (y - Ruler + verticalScrollBar()->value()) / Row;
    return row >= 0 && row < s->tracks.size() ? s->tracks[row].id : QString{};
}
QRect TimelineCanvas::clipRect(const QString& id) const {
    const auto* s = owner_.sequence(); if (!s) return {};
    for (qsizetype i = 0; i < s->tracks.size(); ++i) for (const auto& c : s->tracks[i].clips) if (c.id == id) {
        const int x = xAt(c.startFrame), stop = xAt(c.startFrame + c.durationFrames);
        return {x, Ruler + static_cast<int>(i) * Row - verticalScrollBar()->value() + 5, std::max(2, stop - x), Row - 10};
    }
    return {};
}
const Clip* TimelineCanvas::hitClip(QPoint point, QString* trackId) const {
    if (point.x() < Header || point.y() < Ruler) return nullptr;
    const auto id = trackAt(point.y()); const auto* t = track(id); if (!t) return nullptr;
    if (trackId) *trackId = id;
    for (auto it = t->clips.crbegin(); it != t->clips.crend(); ++it) if (clipRect(it->id).contains(point)) return &*it;
    return nullptr;
}
qint64 TimelineCanvas::snapFrame(qint64 frame, const QString& excluded) const {
    if (!owner_.snap_->isChecked()) return frame;
    const auto* s = owner_.sequence(); if (!s) return frame;
    qint64 result = frame; double best = 8.0;
    auto candidate = [&](qint64 target) {
        const auto distance = std::abs(static_cast<long double>(target - frame)) * pixels_;
        if (distance <= best) { best = static_cast<double>(distance); result = target; }
    };
    candidate(0); candidate(s->durationFrames); candidate(owner_.playhead());
    for (const auto& t : s->tracks) for (const auto& c : t.clips) if (c.id != excluded) { candidate(c.startFrame); candidate(c.startFrame + c.durationFrames); }
    return result;
}
void TimelineCanvas::ensurePlayheadVisible() {
    const auto x = xAt(owner_.playhead());
    if (x < Header || x > viewport()->width() - 20) {
        const auto visible = static_cast<qint64>(std::max(1, viewport()->width() - Header) / pixels_);
        const auto first = std::max<qint64>(0, owner_.playhead() - visible / 3);
        horizontalScrollBar()->setValue(static_cast<int>(std::min<qint64>(horizontalScrollBar()->maximum(), first / scrollUnit_)));
    }
}
QVector<TimelineCanvas::RulerTick> TimelineCanvas::rulerTicks() const {
    QVector<RulerTick> ticks;
    const auto* s = owner_.sequence(); if (!s) return ticks;
    const auto first = firstFrame();
    const int width = viewport()->width();
    if (owner_.timeDisplay() == TimeDisplay::Frames) {
        const qint64 step = std::max<qint64>(1, static_cast<qint64>(std::ceil(80 / pixels_)));
        for (qint64 tick = first - first % step; xAt(tick) < width;) {
            const int x = xAt(tick); if (x >= Header) ticks.append({x, QString::number(tick)});
            if (tick > MaxFrame - step) break;
            tick += step;
        }
        return ticks;
    }
    const double secondsPerFrame = static_cast<double>(s->frameRate.denominator) / s->frameRate.numerator;
    const double pixelsPerSecond = pixels_ / secondsPerFrame;
    const double firstSeconds = static_cast<double>(first) * secondsPerFrame;
    const double endSeconds = firstSeconds + std::max(0, width - Header) / pixelsPerSecond;
    auto niceStep = [](double minimum) {
        const double scale = std::pow(10.0, std::floor(std::log10(minimum)));
        for (const double factor : {1.0, 2.0, 5.0, 10.0})
            if (factor * scale >= minimum) return factor * scale;
        return 10 * scale;
    };
    auto precision = [](double step) { return std::max(0, static_cast<int>(std::ceil(-std::log10(step) - 1e-9))); };
    auto label = [](double seconds, int digits) { return QString::number(seconds, 'f', digits) + " s"; };
    double step = niceStep(std::max(secondsPerFrame, 80 / pixelsPerSecond));
    // Measure the longest visible label, including its fractional digits and unit.
    // Re-evaluate after choosing a coarser interval because its precision can change.
    for (;;) {
        const int digits = precision(step);
        const int spacing = std::max(80, std::max(fontMetrics().horizontalAdvance(label(firstSeconds, digits)),
            fontMetrics().horizontalAdvance(label(endSeconds, digits))) + 16);
        if (step * pixelsPerSecond >= spacing) break;
        step = niceStep(std::max(step * 1.01, spacing / pixelsPerSecond));
    }
    const int digits = precision(step);
    const double aligned = std::floor(firstSeconds / step) * step;
    const double offset = (aligned - firstSeconds) * pixelsPerSecond;
    // Iterate over local offsets with a viewport-sized bound, even at huge absolute times.
    const int count = std::max(0, width - Header) / 80 + 3;
    for (int i = 0; i < count; ++i) {
        const double x = Header + offset + i * step * pixelsPerSecond;
        if (x >= width) break;
        if (x >= Header) ticks.append({static_cast<int>(std::round(x)), label(aligned + i * step, digits)});
    }
    return ticks;
}
void TimelineCanvas::paintEvent(QPaintEvent*) {
    QPainter p(viewport()); p.fillRect(viewport()->rect(), QColor("#191d24"));
    const auto* s = owner_.sequence(); if (!s) return;
    p.setClipRect(QRect(0, Ruler, viewport()->width(), viewport()->height() - Ruler));
    for (qsizetype i = 0; i < s->tracks.size(); ++i) {
        const auto& t = s->tracks[i]; const int y = Ruler + static_cast<int>(i) * Row - verticalScrollBar()->value();
        p.fillRect(QRect(0, y, viewport()->width(), Row), i % 2 ? QColor("#202630") : QColor("#252c37"));
        p.setClipRect(QRect(Header, std::max(Ruler, y), viewport()->width() - Header, std::max(0, y + Row - std::max(Ruler, y))));
        for (const auto& c : t.clips) {
            const auto r = clipRect(c.id); if (r.right() < Header || r.left() > viewport()->width()) continue;
            const QColor color = clipCaption(c).startsWith(tr("OFFLINE · ")) ? QColor("#963e43") : c.kind == "audio" ? QColor("#39785d") : c.kind == "title" ? QColor("#78519b") : QColor("#386a9b");
            p.setBrush(t.enabled ? color : QColor("#555b64"));
            p.setPen(QPen(owner_.editor().state().selection.clipIds.contains(c.id) ? QColor("#ffe285") : color.lighter(140), 2));
            p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 3, 3);
            p.setPen(Qt::white); const auto textRect = r.adjusted(8, 3, -8, -3);
            const auto caption = clipCaption(c);
            p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(caption, Qt::ElideRight, std::max(0, textRect.width())));
            p.setPen(QColor("#dde6f0")); p.drawLine(r.left() + 4, r.top() + 12, r.left() + 4, r.bottom() - 12);
            p.drawLine(r.right() - 4, r.top() + 12, r.right() - 4, r.bottom() - 12);
        }
        p.setClipRect(QRect(0, Ruler, viewport()->width(), viewport()->height() - Ruler));
        p.fillRect(QRect(0, y, Header, Row), owner_.editor().state().selection.trackIds.contains(t.id) ? QColor("#404c5e") : QColor("#303846"));
        p.setPen(Qt::white); p.drawText(QRect(8, y + 4, Header - 16, 20), Qt::AlignLeft, p.fontMetrics().elidedText(t.name, Qt::ElideRight, Header - 16));
        p.setPen(t.enabled ? QColor("#9ce3b3") : QColor("#aaaaaa")); p.drawText(QRect(8, y + 28, 60, 20), Qt::AlignLeft, t.enabled ? "E: on" : "E: off");
        p.setPen(t.locked ? QColor("#ffd78a") : QColor("#aaaaaa")); p.drawText(QRect(78, y + 28, 70, 20), Qt::AlignLeft, t.locked ? "L: locked" : "L: open");
        if (t.kind == "audio") { p.setPen(Qt::white); p.drawText(QRect(8, y + 49, Header - 16, 18), Qt::AlignLeft,
            QString("%1%2%3×").arg(t.muted ? "Mute · " : "").arg(t.solo ? "Solo · " : "").arg(t.gain)); }
    }
    p.setClipping(false); p.fillRect(QRect(0, 0, viewport()->width(), Ruler), QColor("#11151c"));
    p.setPen(QColor("#c4cedb")); p.drawText(QRect(8, 0, Header - 8, Ruler), Qt::AlignVCenter,
        owner_.timeDisplay() == TimeDisplay::Seconds ? tr("Tracks / seconds") : tr("Tracks / frames"));
    p.setClipRect(QRect(Header, 0, viewport()->width() - Header, Ruler));
    for (const auto& tick : rulerTicks()) {
        p.drawLine(tick.x, Ruler - 7, tick.x, Ruler); p.drawText(tick.x + 3, 17, tick.label);
    }
    p.setClipRect(QRect(Header, 0, viewport()->width() - Header, viewport()->height()));
    if (s->multicam) for (const auto& cut : s->multicam->cuts) {
        const int x = xAt(cut.frame); if (x < Header || x > viewport()->width()) continue;
        p.setPen(QPen(QColor("#71c4ee"), 1, Qt::DashLine)); p.drawLine(x, Ruler, x, viewport()->height());
        const auto index = s->multicam->cameraTrackIds.indexOf(cut.trackId) + 1;
        p.drawText(x + 3, Ruler + 14, QString("Cam %1").arg(index));
    }
    p.setPen(QPen(QColor("#fa7b65"), 2)); const int head = xAt(owner_.playhead()); p.drawLine(head, 0, head, viewport()->height());
    const int sequenceEnd = xAt(s->durationFrames); p.setPen(QPen(QColor("#7c8492"), 1, Qt::DashLine)); p.drawLine(sequenceEnd, Ruler, sequenceEnd, viewport()->height());
    if (moved_ && gesture_ != Gesture::Seek && gesture_ != Gesture::None) {
        int row = 0; for (const auto& t : s->tracks) { if (t.id == targetTrack_) break; ++row; }
        p.setBrush(QColor(255, 226, 133, 65)); p.setPen(QPen(QColor("#ffe285"), 2, Qt::DashLine));
        const int x = xAt(targetStart_);
        p.drawRect(QRect(x, Ruler + row * Row - verticalScrollBar()->value() + 5, std::max(2, xAt(targetStart_ + targetDuration_) - x), Row - 10));
    }
}
void TimelineCanvas::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    setFocus(); emit owner_.activated(); cancelGesture();
    const auto point = e->position().toPoint(); press_ = cursor_ = point;
    const auto tid = trackAt(point.y()); const auto* t = track(tid);
    if (point.x() < Header && t) {
        if ((point.y() - Ruler + verticalScrollBar()->value()) % Row >= 26) {
            if (point.x() < 70) owner_.execute(SetTrackEnabled{owner_.sequence()->id, tid, !t->enabled});
            else owner_.execute(SetTrackLocked{owner_.sequence()->id, tid, !t->locked});
        } else owner_.select(tid, {}, false);
        return;
    }
    if (point.x() < Header) return;
    QString clipTrack;
    if (const auto* clip = hitClip(point, &clipTrack)) {
        const auto copy = *clip;
        owner_.select(clipTrack, copy.id, e->modifiers().testFlag(Qt::ControlModifier));
        if (e->modifiers().testFlag(Qt::ControlModifier) || track(clipTrack)->locked) return;
        dragClip_ = copy; dragTrack_ = targetTrack_ = clipTrack;
        targetStart_ = copy.startFrame; targetDuration_ = copy.durationFrames;
        offset_ = frameAt(point.x()) - copy.startFrame;
        const auto r = clipRect(copy.id);
        gesture_ = point.x() - r.left() <= 7 ? Gesture::In : r.right() - point.x() <= 7 ? Gesture::Out : Gesture::Move;
    } else {
        if (point.y() >= Ruler) owner_.select(tid, {}, false);
        gesture_ = Gesture::Seek; owner_.setPlayhead(frameAt(point.x()), true);
    }
    autoScroll_.start();
}
void TimelineCanvas::updateGesture(QPoint point) {
    cursor_ = point;
    if (gesture_ == Gesture::None) return;
    if (gesture_ == Gesture::Seek) { owner_.setPlayhead(frameAt(point.x())); scrub_.start(); return; }
    if (!moved_ && (point - press_).manhattanLength() < QApplication::startDragDistance()) return;
    moved_ = true;
    const auto stop = dragClip_.startFrame + dragClip_.durationFrames;
    if (gesture_ == Gesture::Move) {
        const auto requestedStart = std::max<qint64>(0, frameAt(point.x()) - offset_);
        targetStart_ = snapFrame(requestedStart, dragClip_.id);
        // Snap the exclusive out edge as well as the in edge.
        if (targetStart_ == requestedStart && targetStart_ <= MaxFrame - dragClip_.durationFrames) {
            const auto snappedOut = snapFrame(targetStart_ + dragClip_.durationFrames, dragClip_.id);
            if (snappedOut >= dragClip_.durationFrames && snappedOut != targetStart_ + dragClip_.durationFrames) targetStart_ = snappedOut - dragClip_.durationFrames;
        }
        targetStart_ = std::min(targetStart_, MaxFrame - dragClip_.durationFrames);
        const auto* destination = track(trackAt(point.y()));
        targetTrack_ = destination && destination->kind == dragClip_.kind && !destination->locked ? destination->id : dragTrack_;
    } else if (gesture_ == Gesture::In) {
        targetStart_ = std::min(snapFrame(frameAt(point.x()), dragClip_.id), stop - 1); targetDuration_ = stop - targetStart_;
    } else {
        const auto out = std::max(snapFrame(frameAt(point.x()), dragClip_.id), dragClip_.startFrame + 1);
        targetDuration_ = out - dragClip_.startFrame;
    }
    viewport()->update();
}
void TimelineCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (gesture_ != Gesture::None) { updateGesture(e->position().toPoint()); return; }
    const auto* c = hitClip(e->position().toPoint());
    if (c) { const auto r = clipRect(c->id); viewport()->setCursor(e->position().x() - r.left() <= 7 || r.right() - e->position().x() <= 7 ? Qt::SizeHorCursor : Qt::OpenHandCursor); }
    else viewport()->unsetCursor();
}
void TimelineCanvas::cancelGesture() { gesture_ = Gesture::None; moved_ = false; autoScroll_.stop(); scrub_.stop(); viewport()->update(); }
void TimelineCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || gesture_ == Gesture::None) return;
    updateGesture(e->position().toPoint());
    const auto gesture = gesture_; const bool moved = moved_; cancelGesture();
    if (gesture == Gesture::Seek) { owner_.setPlayhead(frameAt(e->position().toPoint().x()), true); return; }
    if (!moved) return;
    const auto* s = owner_.sequence(); if (!s) return;
    if (gesture == Gesture::Move) owner_.executeBatch({MoveClip{s->id, dragTrack_, dragClip_.id, targetTrack_, targetStart_, owner_.mode()},
        SetSelection{{s->id, {targetTrack_}, {dragClip_.id}}}}, "Move clip");
    else owner_.execute(TrimClip{s->id, dragTrack_, dragClip_.id, targetStart_, targetDuration_, owner_.mode()});
}
void TimelineCanvas::mouseDoubleClickEvent(QMouseEvent* e) {
    cancelGesture();
    const auto point = e->position().toPoint();
    const auto tid = trackAt(point.y());
    if (const auto* c = hitClip(point)) owner_.editClipProperties(tid, c->id);
    else if (point.x() < Header) owner_.editTrackAudio(tid);
}
void TimelineCanvas::wheelEvent(QWheelEvent* e) {
    if (e->modifiers().testFlag(Qt::ControlModifier)) { owner_.zoom_->setValue(owner_.zoom_->value() + (e->angleDelta().y() > 0 ? 4 : -4)); e->accept(); }
    else if (e->modifiers().testFlag(Qt::ShiftModifier) || e->angleDelta().x()) {
        auto* bar = horizontalScrollBar(); const int delta = e->angleDelta().x() ? e->angleDelta().x() : e->angleDelta().y();
        bar->setValue(bar->value() - delta / 120 * bar->singleStep() * 3); e->accept();
    } else QAbstractScrollArea::wheelEvent(e);
}
bool TimelineCanvas::acceptDrop(QDropEvent* e, bool commit) {
    const auto point = e->position().toPoint(); const auto* t = track(trackAt(point.y()));
    if (!e->possibleActions().testFlag(Qt::CopyAction) || !e->mimeData()->hasFormat(MediaMime) || !t || t->locked || t->kind == "title" || point.x() < Header) { e->ignore(); return false; }
    const auto id = QString::fromUtf8(e->mimeData()->data(MediaMime));
    bool compatible = false;
    for (const auto& m : owner_.editor().state().project.media) if (m.id == id)
        for (const auto& stream : m.streams) compatible |= stream.kind == t->kind && stream.durationTicks > 0;
    if (!compatible) { e->ignore(); return false; }
    if (commit && !owner_.placeMedia(id, t->id, snapFrame(frameAt(point.x())))) { e->ignore(); return false; }
    e->setDropAction(Qt::CopyAction); e->accept(); return true;
}
bool TimelineCanvas::viewportEvent(QEvent* e) {
    if (e->type() == QEvent::DragEnter) {
        auto* enter = static_cast<QDragEnterEvent*>(e);
        enter->ignore();
        // Entry accepts a known source across the whole viewport. Rejecting labels/ruler here
        // prevents Qt from delivering later movement into a valid track body.
        if (enter->possibleActions().testFlag(Qt::CopyAction) && enter->mimeData()->hasFormat(MediaMime)) {
            const auto id = QString::fromUtf8(enter->mimeData()->data(MediaMime));
            for (const auto& media : owner_.editor().state().project.media) if (media.id == id) {
                enter->setDropAction(Qt::CopyAction); enter->accept(); break;
            }
        }
        return true;
    }
    if (e->type() == QEvent::DragMove || e->type() == QEvent::Drop) {
        acceptDrop(static_cast<QDropEvent*>(e), e->type() == QEvent::Drop); return true;
    }
    if (e->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) cancelGesture();
    return QAbstractScrollArea::viewportEvent(e);
}
}

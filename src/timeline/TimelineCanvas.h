#pragma once
#include "TimelineWidget.h"
#include <QAbstractScrollArea>
#include <QTimer>
namespace editor::timeline {
class TimelineCanvas final : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit TimelineCanvas(TimelineWidget& owner);
    void refresh();
    void setZoom(int value);
    qint64 frameAt(int x) const;
    int xAt(qint64 frame) const;
    QRect clipRect(const QString& id) const; // Also used by offscreen interaction verification.
    QString trackAt(int y) const;
    qint64 snapFrame(qint64 frame, const QString& excluded = {}) const;
    void ensurePlayheadVisible();
    void cancelGesture();
    void setOfflineMediaIds(const QStringList& ids);
    QString clipCaption(const Clip& clip) const;
    struct RulerTick { int x; QString label; };
    QVector<RulerTick> rulerTicks() const;
    static constexpr int Header = 156, Ruler = 28, Row = 72;
protected:
    void focusInEvent(QFocusEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    bool viewportEvent(QEvent*) override;
private:
    TimelineWidget& owner_;
    QSet<QString> offlineMediaIds_;
    double pixels_ = 4.0;
    qint64 scrollUnit_ = 1;
    qint64 firstFrame() const;
    const Clip* hitClip(QPoint point, QString* trackId = nullptr) const;
    const Track* track(const QString& id) const;
    enum class Gesture { None, Seek, Move, In, Out };
    Gesture gesture_ = Gesture::None;
    QString dragTrack_, targetTrack_;
    Clip dragClip_;
    qint64 offset_ = 0, targetStart_ = 0, targetDuration_ = 1;
    bool moved_ = false;
    QPoint press_, cursor_;
    QTimer scrub_, autoScroll_;
    void updateGesture(QPoint point);
    bool acceptDrop(QDropEvent* event, bool commit);
};
}

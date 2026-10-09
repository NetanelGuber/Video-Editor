#pragma once
#include "Timeline.h"
#include "ui/TimeDisplay.h"
#include <QWidget>
#include <QTreeWidget>

class QAction;
class QComboBox;
class QLabel;
class QSlider;
namespace editor::timeline {
inline constexpr auto MediaMime = "application/x-video-editor-media";
using editor::TimeDisplay;
class MediaBinTree final : public QTreeWidget {
public:
    explicit MediaBinTree(QWidget* parent = nullptr);
protected:
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;
    QStringList mimeTypes() const override;
    Qt::DropActions supportedDropActions() const override { return Qt::CopyAction; }
};
class TimelineCanvas;
class TimelineWidget final : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(QWidget* parent = nullptr);
    const TimelineEditor& editor() const { return editor_; }
    const Sequence* sequence() const;
    void setProject(Project project);
    QString execute(const Command& command);
    QString executeBatch(const QVector<Command>& commands, const QString& label);
    void undo();
    void redo();
    void setSelectedMedia(const QString& id);
    qint64 playhead() const { return playhead_; }
    void setPlayhead(qint64 frame, bool seek = false);
    void stepPlayhead(int direction);
    TimeDisplay timeDisplay() const { return timeDisplay_; }
    void setTimeDisplay(TimeDisplay display);
    bool placeMedia(const QString& mediaId, const QString& trackId, qint64 frame);
    void addTrack(const QString& kind);
    void addTitle(const QString& text);
    void editClipProperties(const QString& trackId = {}, const QString& clipId = {});
    void editClipEffects(const QString& trackId = {}, const QString& clipId = {});
    void editTrackAudio(const QString& trackId = {});
    void manageAudioBuses();
    void newSequence(const QString& name);
    bool nestSequence(const QString& id, bool audio = true);
    void switchCamera(int index);
    void createMulticam();
    bool cameraOverview() const;
    void splitAtPlayhead();
    void deleteSelected();
    void closeGapAtPlayhead();
    void trimToPlayhead(bool in);
    void requestProperties(const QString& section);
    void setSelectedPrimaryVideo();
    void moveSelected(int direction);
    TimelineCanvas* canvas() const { return canvas_; }
signals:
    void projectChanged();
    void stateChanged();
    void seekRequested(qint64 frame);
    void playRequested();
    void activated();
    void editError(const QString& message);
    void cameraOverviewChanged(bool enabled);
    void propertiesRequested(const QString& section);
private:
    friend class TimelineCanvas;
    TimelineEditor editor_{newProject()};
    TimelineCanvas* canvas_;
    QSlider* zoom_;
    QComboBox* stream_;
    QComboBox* sequences_;
    QAction* overview_;
    QLabel* position_;
    QAction *undo_, *redo_, *ripple_, *snap_;
    QString selectedMedia_;
    qint64 playhead_ = 0;
    TimeDisplay timeDisplay_ = TimeDisplay::Frames;
    void refreshPosition();
    EditMode mode() const;
    void publish(const Project& before);
    void editAudioProperties(const Track& track, const Clip* clip, const char* dialogObjectName);
    void refresh();
    QString destination(const QString& kind) const;
    void select(const QString& trackId, const QString& clipId, bool toggle);
};
}

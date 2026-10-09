#pragma once
#include "timeline/TimelineWidget.h"
#include <QWidget>
#include <optional>

class QLabel;
class QTabWidget;
class QPlainTextEdit;
class QPushButton;
class QListWidget;
class QDoubleSpinBox;
class QCheckBox;
namespace editor::ui {
// Presentation only: every edit goes through the same TimelineEditor commands as dialogs.
class InspectorWidget final : public QWidget {
    Q_OBJECT
public:
    explicit InspectorWidget(timeline::TimelineWidget& timeline, QWidget* parent = nullptr);
    void refresh();
    void showSection(const QString& section);
    void showMedia(const QString& id);
    void showTimeline();
private:
    timeline::TimelineWidget& timeline_;
    QString mediaId_;
    timeline::Selection selection_;
    std::optional<project::Track> track_;
    std::optional<project::Clip> clip_;
    QLabel *context_, *summary_, *audioHint_;
    QTabWidget* tabs_;
    QPlainTextEdit* title_;
    QPushButton *titleApply_, *clipDialog_, *effectsDialog_, *audioDialog_, *trackApply_, *clipApply_;
    QListWidget* effects_;
    QCheckBox *enabled_, *locked_, *trackMute_, *trackSolo_, *clipMute_;
    QDoubleSpinBox *trackGain_, *trackPan_, *clipGain_, *clipPan_;
    void applyAudio(bool clip);
};
}

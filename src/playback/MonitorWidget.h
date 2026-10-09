#pragma once
#include "PlaybackController.h"
#include "ui/TimeDisplay.h"
#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;
class QStackedWidget;
class QComboBox;
namespace editor::playback {
class RasterSurface;
class GlSurface;
class MonitorWidget final : public QWidget {
    Q_OBJECT
public:
    explicit MonitorWidget(const QString& name, QWidget* parent = nullptr);
    PlaybackController& controller() { return controller_; }
    void setDescription(RenderDescription description, const QString& name);
    void setTimeDisplay(TimeDisplay display);
signals:
    void playRequested();
private:
    PlaybackController controller_;
    RasterSurface* raster_;
    GlSurface* gl_ = nullptr;
    QStackedWidget* surfaces_;
    QLabel *caption_, *time_, *status_;
    QPushButton* play_;
    QSlider* seek_;
    QComboBox* presentation_;
    QImage image_;
    TimeDisplay timeDisplay_ = TimeDisplay::Frames;
    void refreshTime();
    QTimer scrub_, statistics_;
    void showFrame(const QImage& image);
    void updateStatus();
    void setPresentation(int index);
};
}

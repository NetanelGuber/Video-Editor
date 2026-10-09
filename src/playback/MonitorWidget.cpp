#include "MonitorWidget.h"
#include <QJsonArray>
#include <QApplication>
#include <QOpenGLWidget>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPainter>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QComboBox>
#include <QStackedWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QJsonDocument>

namespace editor::playback {
namespace {
void paintFrame(QPainter& painter, const QRect& area, const QImage& image) {
    painter.fillRect(area, QColor("#11151b"));
    if (image.isNull()) { painter.setPen(Qt::lightGray); painter.drawText(area, Qt::AlignCenter, "No video frame / sequence gap"); return; }
    const auto size = image.size().scaled(area.size(), Qt::KeepAspectRatio);
    QRect destination(QPoint{}, size); destination.moveCenter(area.center());
    painter.setRenderHint(QPainter::SmoothPixmapTransform); painter.drawImage(destination, image);
}
QString timeText(qint64 us) {
    const qint64 ms = us / 1000;
    return QString("%1:%2:%3.%4").arg(ms / 3600000, 2, 10, QLatin1Char('0')).arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0')).arg(ms % 1000, 3, 10, QLatin1Char('0'));
}
}
class RasterSurface final : public QWidget {
public:
    QImage image;
    explicit RasterSurface(QWidget* parent) : QWidget(parent) { setMinimumSize(240, 135); }
    void paintEvent(QPaintEvent*) override { QPainter painter(this); paintFrame(painter, rect(), image); }
};
class GlSurface final : public QOpenGLWidget {
public:
    QImage image;
    QString renderer;
    explicit GlSurface(QWidget* parent) : QOpenGLWidget(parent) { setMinimumSize(240, 135); }
    void initializeGL() override {
        auto* functions = context()->functions();
        renderer = QString::fromLatin1(reinterpret_cast<const char*>(functions->glGetString(GL_RENDERER)));
    }
    void paintGL() override { QPainter painter(this); paintFrame(painter, rect(), image); }
};
MonitorWidget::MonitorWidget(const QString& name, QWidget* parent) : QWidget(parent), controller_(this) {
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(4, 4, 4, 4);
    caption_ = new QLabel(name, this); caption_->setTextFormat(Qt::PlainText); layout->addWidget(caption_);
    surfaces_ = new QStackedWidget(this); raster_ = new RasterSurface(surfaces_); surfaces_->addWidget(raster_); layout->addWidget(surfaces_, 1);
    seek_ = new QSlider(Qt::Horizontal, this); seek_->setObjectName("seekSlider"); seek_->setRange(0, 100000); layout->addWidget(seek_);
    auto* transport = new QHBoxLayout;
    auto* previous = new QPushButton("Previous frame", this); previous->setObjectName("previousFrameButton");
    play_ = new QPushButton("Play", this); play_->setObjectName("playButton");
    auto* next = new QPushButton("Next frame", this); next->setObjectName("nextFrameButton");
    transport->addWidget(previous); transport->addWidget(play_); transport->addWidget(next);
    time_ = new QLabel(this); time_->setObjectName("monitorTime"); time_->setTextFormat(Qt::PlainText); transport->addWidget(time_, 1); layout->addLayout(transport);
    auto* paths = new QHBoxLayout;
    auto* decode = new QComboBox(this); decode->setObjectName("decodeMode"); decode->addItems({"CPU decode", "D3D11VA decode (CPU fallback)"});
    presentation_ = new QComboBox(this); presentation_->setObjectName("presentationMode"); presentation_->addItems({"CPU presentation", "OpenGL presentation"});
    paths->addWidget(decode); paths->addWidget(presentation_); layout->addLayout(paths);
    auto* profile = new QHBoxLayout;
    auto* quality = new QComboBox(this); quality->setObjectName("previewQuality");
    quality->addItems({"Preview: 360p", "Preview: 720p", "Preview: 1080p"}); quality->setCurrentIndex(1);
    auto* memory = new QComboBox(this); memory->setObjectName("previewMemory");
    memory->addItems({"Standard memory", "Low memory (360p)"});
    quality->setToolTip("Preview is limited to 30 fps. Export always uses originals at the chosen output resolution and frame rate.");
    memory->setToolTip("Video queue: 32 MiB standard or 4 MiB low memory. Codec reference frames and audio use separate bounded buffers.");
    profile->addWidget(quality); profile->addWidget(memory); layout->addLayout(profile);
    auto applyProfile = [this, quality, memory] {
        const QSize sizes[]{{640, 360}, {1280, 720}, {1920, 1080}};
        controller_.setPreviewProfile(sizes[quality->currentIndex()], memory->currentIndex() == 1);
    };
    connect(quality, &QComboBox::currentIndexChanged, this, applyProfile);
    connect(memory, &QComboBox::currentIndexChanged, this, applyProfile);
    status_ = new QLabel(this); status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true); layout->addWidget(status_);
    scrub_.setSingleShot(true); scrub_.setInterval(45);
    auto seekSlider = [this] { controller_.seek(project::scaleTime(seek_->value(), controller_.description().durationUs, 100000, project::Rounding::Nearest).value_or(0)); };
    connect(&scrub_, &QTimer::timeout, this, seekSlider);
    connect(seek_, &QSlider::sliderMoved, this, [this] { controller_.pause(); scrub_.start(); });
    connect(seek_, &QSlider::sliderReleased, this, [this, seekSlider] { scrub_.stop(); seekSlider(); });
    connect(seek_, &QSlider::valueChanged, this, [this, seekSlider] { if (!seek_->isSliderDown()) { controller_.pause(); seekSlider(); } });
    connect(play_, &QPushButton::clicked, this, [this] {
        if (controller_.isPlaying()) controller_.pause(); else { emit playRequested(); controller_.play(); }
    });
    auto step = [this](int direction) {
        controller_.pause(); const auto fps = controller_.description().frameRate;
        const auto current = project::ticksToFrames(controller_.positionUs(), {1, 1000000}, fps, project::Rounding::Nearest).value_or(0);
        const auto target = project::framesToTicks(std::max<qint64>(0, current + direction), fps, {1, 1000000}, project::Rounding::Nearest).value_or(0);
        controller_.seek(target);
    };
    connect(previous, &QPushButton::clicked, this, [step] { step(-1); }); connect(next, &QPushButton::clicked, this, [step] { step(1); });
    connect(decode, &QComboBox::currentIndexChanged, this, [this](int index) { controller_.setDecodeMode(index == 0 ? DecodeMode::Cpu : DecodeMode::D3D11); });
    connect(presentation_, &QComboBox::currentIndexChanged, this, &MonitorWidget::setPresentation);
    connect(&controller_, &PlaybackController::frameReady, this, &MonitorWidget::showFrame);
    connect(&controller_, &PlaybackController::positionChanged, this, [this](qint64 us) {
        refreshTime();
        if (!seek_->isSliderDown()) { QSignalBlocker block(seek_); seek_->setValue(static_cast<int>(project::scaleTime(us, 100000,
            std::max<qint64>(1, controller_.description().durationUs), project::Rounding::Nearest).value_or(0))); }
    });
    connect(&controller_, &PlaybackController::stateChanged, this, &MonitorWidget::updateStatus);
    statistics_.setInterval(500); connect(&statistics_, &QTimer::timeout, this, &MonitorWidget::updateStatus); statistics_.start(); updateStatus();
}
void MonitorWidget::setDescription(RenderDescription description, const QString& name) {
    caption_->setText(name); caption_->setToolTip(description.limitations.join('\n')); controller_.setDescription(std::move(description));
    refreshTime();
}
void MonitorWidget::setTimeDisplay(TimeDisplay display) { timeDisplay_ = display; refreshTime(); }
void MonitorWidget::refreshTime() {
    auto format = [this](qint64 us) {
        return timeDisplay_ == TimeDisplay::Seconds ? QString::number(static_cast<double>(us) / 1000000, 'f', 3) + " s" : timeText(us);
    };
    time_->setText(format(controller_.positionUs()) + " / " + format(controller_.description().durationUs));
}
void MonitorWidget::showFrame(const QImage& image) {
    image_ = image; raster_->image = image; raster_->update(); if (gl_) { gl_->image = image; gl_->update(); }
}
void MonitorWidget::setPresentation(int index) {
    if (index == 0) { surfaces_->setCurrentWidget(raster_); return; }
    if (QApplication::platformName() == "offscreen") {
        presentation_->setCurrentIndex(0); presentation_->setToolTip("OpenGL is unavailable on the offscreen test platform; CPU fallback."); return;
    }
    if (!gl_) { gl_ = new GlSurface(surfaces_); surfaces_->addWidget(gl_); gl_->image = image_; }
    surfaces_->setCurrentWidget(gl_);
    QTimer::singleShot(100, this, [this] {
        if (presentation_->currentIndex() == 1 && !gl_->isValid()) {
            presentation_->setCurrentIndex(0); presentation_->setToolTip("OpenGL context creation failed; CPU fallback.");
        }
    });
}
void MonitorWidget::updateStatus() {
    if (presentation_->currentIndex() == 1 && gl_ && gl_->context() && !gl_->isValid()) {
        presentation_->setCurrentIndex(0); presentation_->setToolTip("OpenGL context lost; CPU fallback.");
    }
    play_->setText(controller_.isPlaying() ? "Pause" : "Play");
    const auto stats = controller_.stats(), video = stats["video"].toObject(), audio = stats["audioOutput"].toObject();
    QString state = stats["buffering"].toBool() ? "Loading / seeking…" : controller_.isPlaying() ? "Playing" : "Paused";
    QString error = stats["visualError"].toString();
    for (const auto& layer : stats["videoLayers"].toArray()) {
        const auto failure = layer.toObject()["error"].toString();
        if (!failure.isEmpty()) error += " " + failure;
    }
    if (!video["fallback"].toString().isEmpty()) error += " " + video["fallback"].toString();
    const auto audioDecode = stats["audioDecode"].toObject(); if (!audioDecode["error"].toString().isEmpty()) error += " " + audioDecode["error"].toString();
    if (!audio["error"].toString().isEmpty()) error += " " + audio["error"].toString();
    status_->setText(QString("%1 · %2 · presented %3 / dropped %4%5").arg(state, video["path"].toString("CPU"))
        .arg(stats["presented"].toInt()).arg(stats["dropped"].toInt()).arg(error.isEmpty() ? QString{} : " · " + error));
    QString details = QString::fromUtf8(QJsonDocument(stats).toJson(QJsonDocument::Indented));
    if (gl_) details += "\nOpenGL renderer: " + gl_->renderer;
    status_->setToolTip(details); status_->setAccessibleDescription(details);
}
}

#pragma once
#include "playback/RenderDescription.h"
#include "Capabilities.h"
#include <QThread>
#include <QMutex>
#include <QJsonObject>

namespace editor::exporting {
struct Preset {
    QString id, label, container, videoEncoder, audioEncoder;
    QStringList speeds;
    int quality = 20, audioBitrate = 192000;
};
QVector<Preset> presets();
QString validateExport(const playback::RenderDescription& description, const project::ExportSettings& settings);

// Immutable edit snapshot; queues, rasterization, encoding and disk I/O stay off the UI thread.
class ExportWorker final : public QThread {
    Q_OBJECT
public:
    ExportWorker(playback::RenderDescription description, project::ExportSettings settings, QObject* parent = nullptr);
    ~ExportWorker() override;
    void cancel();
    QJsonObject result() const;
signals:
    void progress(int percent);
protected:
    void run() override;
private:
    QString render(qint64& frames, qint64& samples);
    playback::RenderDescription description_;
    project::ExportSettings settings_;
    mutable QMutex mutex_;
    QJsonObject result_;
    QJsonObject audioMetrics_;
    EncoderResolution encoder_;
};
}

#pragma once
#include "project/Project.h"
#include <QJsonArray>
#include <QThread>
#include <functional>

namespace editor::exporting {
struct CompatibilityProfile {
    QString id, label, container, codec, extension, explanation;
    int maxLongEdge = 4096, maxShortEdge = 4096, maxFps = 240;
    bool softwareOnly = false;
};
QVector<CompatibilityProfile> compatibilityProfiles();
std::optional<CompatibilityProfile> compatibilityProfile(const QString& id);
QStringList encoderCandidates(const project::ExportSettings& settings);
QString encoderPixelFormat(const project::ExportSettings& settings, const QString& encoder);
QString exportCombinationError(const project::ExportSettings& settings);
QString compiledCombinationError(const project::ExportSettings& settings);
QJsonObject compiledCapabilities();
QStringList videoEncoderNames();
QString capabilityCacheFilePath();
QByteArray encoderCatalogCacheKey(const project::ExportSettings& settings);
QJsonArray cachedEncoderCatalog(const project::ExportSettings& settings, const QString& cachePath = {});
bool cacheEncoderCatalog(const project::ExportSettings& settings, const QJsonArray& probes, const QString& cachePath = {});
QJsonObject cachedCompatibleProbe(const project::ExportSettings& settings, const QString& encoder, const QString& cachePath = {});
void cacheCompatibleProbes(const QJsonArray& probes, const QString& cachePath = {});
QJsonObject encoderControls(const QString& encoder);
QJsonArray compatibleContainers(const QString& encoder);
QString containerExtension(const QString& container);
bool containerAcceptsVideo(const QString& container, const QString& encoder);
project::ExportSettings adaptToEncoder(project::ExportSettings settings, const QString& encoder);
QString nativeSettingsError(const project::ExportSettings& settings, const QString& encoder);
// Bounded child process. No driver calls execute on the GUI thread.
QJsonObject runCapabilityProbe(const QStringList& arguments, const QByteArray& input = {},
    const std::function<bool()>& cancelled = {}, int deadlineMs = 15000, const QString& executable = {});
QJsonObject probeEncoder(const project::ExportSettings& settings, const QString& encoder,
    const std::function<bool()>& cancelled = {}, int deadlineMs = 15000);
QJsonObject discoverCapabilities(const project::ExportSettings& settings, bool devices,
    const std::function<bool()>& cancelled = {}, bool allCustomEncoders = false, bool forceRefresh = false, const QString& cachePath = {});
struct EncoderResolution {
    QString encoder, pixelFormat, warning, error;
};
EncoderResolution resolveEncoder(const project::ExportSettings& settings, const QJsonArray& probes);
QByteArray probeSettings(const project::ExportSettings& settings, const QString& encoder);
// Invoked only inside the disposable helper process.
QJsonObject localEncoderProbe(const QJsonObject& request);
QJsonObject localDeviceProbe(const QString& type);

class CapabilityWorker final : public QThread {
public:
    CapabilityWorker(project::ExportSettings settings, bool devices, QObject* parent = nullptr, bool allCustomEncoders = false, bool forceRefresh = false)
        : QThread(parent), settings_(std::move(settings)), devices_(devices), allCustomEncoders_(allCustomEncoders), forceRefresh_(forceRefresh) {}
    ~CapabilityWorker() override { requestInterruption(); wait(); }
    QJsonObject result() const { return result_; } // Read only after finished.
protected:
    void run() override {
        result_ = discoverCapabilities(settings_, devices_, [this] { return isInterruptionRequested(); }, allCustomEncoders_, forceRefresh_);
        // QThread can clear its interruption flag as it finishes; retain the outcome in the result.
        result_["cancelled"] = isInterruptionRequested();
    }
private:
    project::ExportSettings settings_;
    bool devices_;
    bool allCustomEncoders_;
    bool forceRefresh_;
    QJsonObject result_;
};
}

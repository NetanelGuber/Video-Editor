#include "export/Capabilities.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdio>
#include <stdexcept>

using namespace editor;
namespace {
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2) return 2;
    QJsonArray checks, failures;
    auto check = [&](bool passed, const QString& name) {
        checks.append(name);
        if (!passed) { failures.append(name); std::fprintf(stderr, "FAIL: %s\n", qPrintable(name)); }
    };
    QDir().mkpath(app.arguments()[1]);
    QTemporaryDir temporary(QDir(app.arguments()[1]).filePath("capability-cache-XXXXXX"));
    if (!temporary.isValid()) throw std::runtime_error("Cannot create temporary cache directory.");
    const auto path = QDir(temporary.path()).filePath("export-capabilities.json");
    project::ExportSettings settings; settings.compatibilityProfile = "custom"; settings.width = 320; settings.height = 180;
    settings.frameRate = {30000, 1001};
    QJsonArray catalog;
    for (const auto& name : exporting::videoEncoderNames()) {
        const auto configuration = QJsonDocument::fromJson(exporting::probeSettings(settings, name)).object();
        catalog.append(QJsonObject{{"encoder", name}, {"usable", true}, {"pixelFormat", "yuv420p"}, {"configuration", configuration}});
    }
    check(!catalog.isEmpty() && exporting::cacheEncoderCatalog(settings, catalog, path), "A complete custom encoder catalog writes to the JSON cache");
    QFile file(path); const bool opened = file.open(QIODevice::ReadOnly); const auto root = QJsonDocument::fromJson(file.readAll()).object(); file.close();
    check(opened && root["formatVersion"].toInt() == 1 && root["fingerprint"].toObject().contains("ffmpegConfiguration") &&
        root["fingerprint"].toObject().contains("gpuDrivers"), "Cache records its format, FFmpeg build and GPU driver fingerprint");
    const auto loaded = exporting::cachedEncoderCatalog(settings, path);
    check(loaded.size() == catalog.size(), "A matching launch loads the complete cached encoder catalog");
    check(!exporting::cachedCompatibleProbe(settings, "libx264", path).isEmpty(), "Exact compatible settings are available from the cache");
    auto changed = settings; changed.frameRate = {60, 1};
    check(exporting::cachedEncoderCatalog(changed, path).isEmpty() && exporting::cachedCompatibleProbe(changed, "libx264", path).isEmpty(),
        "Different exact frame rates do not reuse cached compatibility results");
    const auto warm = exporting::discoverCapabilities(settings, false, {}, true, false, path);
    check(warm["catalogCacheHit"].toBool() && warm["encoderProbes"].toArray().size() == catalog.size(),
        "A warm catalog lookup skips the all-encoder probe pass");
    auto damaged = root; damaged["formatVersion"] = 0;
    check(write(path, QJsonDocument(damaged).toJson()) && exporting::cachedEncoderCatalog(settings, path).isEmpty(),
        "Unknown cache format invalidates saved results");
    damaged = root; auto fingerprint = damaged["fingerprint"].toObject(); fingerprint["ffmpegVersion"] = "changed build"; damaged["fingerprint"] = fingerprint;
    check(write(path, QJsonDocument(damaged).toJson()) && exporting::cachedEncoderCatalog(settings, path).isEmpty(),
        "Changed FFmpeg fingerprint invalidates saved results");
    damaged = root; fingerprint = damaged["fingerprint"].toObject(); auto adapters = fingerprint["gpuDrivers"].toArray();
    if (!adapters.isEmpty()) { auto adapter = adapters[0].toObject(); adapter["driverVersion"] = "changed driver"; adapters[0] = adapter; }
    fingerprint["gpuDrivers"] = adapters; damaged["fingerprint"] = fingerprint;
    check(write(path, QJsonDocument(damaged).toJson()) && exporting::cachedEncoderCatalog(settings, path).isEmpty(),
        "Changed GPU driver fingerprint invalidates saved results");
    check(!exporting::cacheEncoderCatalog(settings, QJsonArray{QJsonObject{{"encoder", "partial"}, {"usable", false}}}, path),
        "Incomplete catalogs are never stored as reusable cache entries");
    check(!exporting::cacheEncoderCatalog(settings, catalog, temporary.path()), "Cache write failure is reported without throwing");
    check(write(path, "{broken") && exporting::cachedEncoderCatalog(settings, path).isEmpty(), "Malformed cache JSON is ignored safely");
    check(exporting::cacheEncoderCatalog(settings, catalog, path) && exporting::cachedEncoderCatalog(settings, path).size() == catalog.size(),
        "A new complete scan replaces malformed cache data");
    std::printf("Capability cache: %lld checks, %lld failures\n", static_cast<long long>(checks.size()), static_cast<long long>(failures.size()));
    return failures.isEmpty() ? 0 : 1;
}

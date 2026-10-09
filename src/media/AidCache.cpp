#include "AidCache.h"
#include "Inspection.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <cmath>
#include <algorithm>

namespace editor::media {
namespace {
QMutex cacheMutex;
constexpr qint64 EntryBytes = 1024 * 1024;
QString entryPath(const QString& key, const CacheOptions& options) {
    return QDir(aidCacheDirectory(options)).filePath(key + ".aids.json");
}
}
QString aidCacheDirectory(const CacheOptions& options) {
    if (!options.directory.isEmpty()) return options.directory;
    const auto overridePath = qEnvironmentVariable("VIDEO_EDITOR_CACHE_DIR");
    return QDir(overridePath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) : overridePath).filePath("media-aids-v1");
}
QString aidCacheKey(const QString& sourcePath) {
    const QFileInfo info(sourcePath); QFile source(sourcePath);
    if (!info.isFile() || !source.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData("media-aids-v1:ffmpeg-9.0.2:thumb256x144:wave256x30s:");
    hash.addData(pathIdentity(sourcePath).toUtf8()); hash.addData(":");
    hash.addData(QByteArray::number(info.size())); hash.addData(":");
    hash.addData(QByteArray::number(info.lastModified().toMSecsSinceEpoch()));
    hash.addData(source.read(65536));
    if (info.size() > 65536 && source.seek(std::max<qint64>(65536, info.size() - 65536))) hash.addData(source.read(65536));
    return QString::fromLatin1(hash.result().toHex());
}
bool readAids(const QString& key, Inspection& result, const CacheOptions& options) {
    if (!options.enabled || key.isEmpty() || options.budgetBytes <= 0 || options.entryLimit <= 0) return false;
    QMutexLocker lock(&cacheMutex);
    QFile file(entryPath(key, options));
    if (!file.open(QIODevice::ReadWrite) && !file.open(QIODevice::ReadOnly)) return false;
    if (file.size() > std::min(EntryBytes, options.budgetBytes)) return false;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    if (object["version"].toInt() != 1 || object["key"].toString() != key) return false;
    const auto peaks = object["waveform"].toArray(); const auto seconds = object["seconds"].toDouble(-1);
    if ((!peaks.isEmpty() && peaks.size() != 256) || !std::isfinite(seconds) || seconds < 0 || seconds > 30.001) return false;
    QVector<float> waveform;
    for (const auto& peak : peaks) {
        const double value = peak.toDouble(-1);
        if (!std::isfinite(value) || value < 0 || value > 1) return false;
        waveform.append(static_cast<float>(value));
    }
    const auto png = QByteArray::fromBase64(object["thumbnail"].toString().toLatin1());
    QImage image;
    if (!png.isEmpty()) {
        QBuffer buffer; buffer.setData(png); buffer.open(QIODevice::ReadOnly); QImageReader reader(&buffer, "PNG");
        const auto size = reader.size();
        if (!size.isValid() || size.width() > 256 || size.height() > 256 || size.width() * size.height() > 256 * 144) return false;
        image = reader.read(); if (image.isNull()) return false;
        const auto format = static_cast<QImage::Format>(object["thumbnailFormat"].toInt());
        if (format != QImage::Format_RGB888 && format != QImage::Format_RGB32 && format != QImage::Format_ARGB32) return false;
        image = image.convertToFormat(format);
    }
    if (image.isNull() && waveform.isEmpty()) return false;
    for (const auto& stream : result.media.streams) {
        if (stream.kind == "video" && image.isNull()) return false;
        if (stream.kind == "audio" && waveform.isEmpty()) return false;
    }
    result.thumbnail = image; result.waveform = waveform; result.waveformSeconds = seconds;
    result.cacheHit = true;
    file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
    return true;
}
void writeAids(const QString& key, const Inspection& result, const CacheOptions& options, const std::function<bool()>& cancelled) {
    if (!options.enabled || key.isEmpty() || !result.error.isEmpty() || result.notes.contains("resource limit") ||
        result.cancelled || (cancelled && cancelled()) || (result.thumbnail.isNull() && result.waveform.isEmpty())) return;
    for (const auto& stream : result.media.streams) {
        if (stream.kind == "video" && result.thumbnail.isNull()) return;
        if (stream.kind == "audio" && result.waveform.isEmpty()) return;
    }
    QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
    if (!result.thumbnail.isNull() && !result.thumbnail.save(&buffer, "PNG")) return;
    QJsonArray peaks; for (float peak : result.waveform) peaks.append(peak);
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"key", key}, {"thumbnail", QString::fromLatin1(png.toBase64())},
        {"thumbnailFormat", static_cast<int>(result.thumbnail.format())}, {"waveform", peaks}, {"seconds", result.waveformSeconds}}).toJson(QJsonDocument::Compact);
    if (bytes.size() > std::min(EntryBytes, options.budgetBytes) || options.entryLimit <= 0) return;
    QMutexLocker lock(&cacheMutex);
    if (cancelled && cancelled()) return;
    QDir directory(aidCacheDirectory(options)); if (!QDir().mkpath(directory.absolutePath())) return;
    QLockFile lease(directory.filePath("cache.lock")); if (!lease.tryLock(100)) return;
    const auto destination = entryPath(key, options);
    auto entries = directory.entryInfoList({"*.aids.json"}, QDir::Files, QDir::Time | QDir::Reversed);
    qint64 total = 0; int count = 0;
    for (const auto& entry : entries) if (entry.absoluteFilePath() != destination) { total += entry.size(); ++count; }
    for (const auto& entry : entries) {
        if (cancelled && cancelled()) return;
        if (total + bytes.size() <= options.budgetBytes && count + 1 <= options.entryLimit) break;
        if (entry.absoluteFilePath() != destination && QFile::remove(entry.absoluteFilePath())) { total -= entry.size(); --count; }
    }
    if (total + bytes.size() > options.budgetBytes || count + 1 > options.entryLimit) return;
    QSaveFile file(destination); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || (cancelled && cancelled())) { file.cancelWriting(); return; }
    file.commit(); // Cache failures are regenerable and never fail import/relink.
}
}

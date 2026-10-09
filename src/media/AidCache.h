#pragma once
#include <QString>
#include <functional>

namespace editor::media {
struct Inspection;
struct CacheOptions {
    QString directory; // Empty uses the local application cache; never the source/project directory.
    qint64 budgetBytes = 256 * 1024 * 1024;
    int entryLimit = 1024;
    bool enabled = true;
};
QString aidCacheDirectory(const CacheOptions& options = {});
QString aidCacheKey(const QString& sourcePath);
bool readAids(const QString& key, Inspection& result, const CacheOptions& options);
void writeAids(const QString& key, const Inspection& result, const CacheOptions& options, const std::function<bool()>& cancelled);
}

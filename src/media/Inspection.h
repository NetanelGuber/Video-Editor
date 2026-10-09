#pragma once
#include "project/Project.h"
#include "AidCache.h"
#include <QImage>
#include <QSemaphore>
#include <QSet>
#include <QThread>
#include <atomic>
#include <functional>

namespace editor::media {
// App-owned values only. FFmpeg types never cross the worker/UI boundary.
struct Inspection {
    project::Media media;
    QString mediaId, error, state, notes;
    QImage thumbnail;
    QVector<float> waveform;
    double waveformSeconds = 0;
    qint64 durationUs = 0;
    bool cancelled = false;
    bool cacheHit = false;
};
QString pathIdentity(const QString& path);
Inspection inspect(const QString& path, const std::function<bool()>& cancelled = {}, const CacheOptions& cache = {});
QString relinkError(const project::Media& original, const project::Media& replacement);

struct ImportJob { QString path, mediaId, findFileName; }; // Nonempty findFileName searches path as a folder, with unique-name matching.
class ImportWorker final : public QThread {
    Q_OBJECT
public:
    ImportWorker(QVector<ImportJob> jobs, QSet<QString> knownPaths, QObject* parent = nullptr);
    void acknowledge() { delivered_.release(); }
signals:
    void inspected(editor::media::Inspection result);
    void batchComplete(int inspected, int duplicates, bool cancelled, QString note);
protected:
    void run() override;
private:
    QVector<ImportJob> jobs_;
    QSet<QString> knownPaths_;
    QSemaphore delivered_;
};
}
Q_DECLARE_METATYPE(editor::media::Inspection)

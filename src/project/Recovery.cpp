#include "Recovery.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>

namespace editor::project {
bool mediaAvailable(const QString& path) {
    QFile file(path);
    return QFileInfo(path).isFile() && file.open(QIODevice::ReadOnly);
}
bool isRecoveryPath(const QString& path, const QString& directory) {
    const QFileInfo info(path);
    return !info.isSymLink() && info.absoluteDir().canonicalPath() == QDir(directory).canonicalPath()
        && !QDir(directory).canonicalPath().isEmpty() && info.fileName().endsWith(".veproject.autosave");
}
QVector<RecoveryCandidate> recoveryCandidates(const QString& directory) {
    QVector<RecoveryCandidate> result;
    const auto entries = QDir(directory).entryInfoList({"*.veproject.autosave"}, QDir::Files | QDir::NoSymLinks, QDir::Time);
    for (const auto& entry : entries) {
        if (result.size() >= 100) break; // Bound validation and dialog work per startup.
        QLockFile lease(entry.absoluteFilePath() + ".session.lock");
        lease.setStaleLockTime(0);
        if (!lease.tryLock(0)) continue;
        const auto loaded = ProjectStore::load(entry.absoluteFilePath());
        result.append({entry.absoluteFilePath(), loaded ? loaded.project->name : entry.fileName(), loaded.error, entry.lastModified()});
    }
    return result;
}
}

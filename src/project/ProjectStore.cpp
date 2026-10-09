#include "ProjectStore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>

namespace editor::project {
namespace {
QString absolute(const QString& path) { return QDir::cleanPath(QFileInfo(path).absoluteFilePath()); }
QString identity(const QString& path) {
    const auto info = QFileInfo(path);
    const auto canonical = info.canonicalFilePath();
    if (!canonical.isEmpty()) return canonical.toCaseFolded();
    // Resolve the parent as well, so a directory junction cannot bypass source protection.
    const auto parent = info.dir().canonicalPath();
    return (parent.isEmpty() ? absolute(path) : QDir(parent).filePath(info.fileName())).toCaseFolded();
}
QString pathError(const Project& p, const QString& target, bool backup) {
    if (target.trimmed().isEmpty() || target.contains(QChar::Null) || target.startsWith(':')) return "Invalid project destination";
    const QStringList destinations = backup ? QStringList{target, target + ".bak", target + ".bak.2", target + ".lock"} : QStringList{target, target + ".lock"};
    for (const auto& destination : destinations) {
        if (QFileInfo(destination).isSymLink()) return "Refusing to write a symbolic link: " + destination;
        for (const auto& m : p.media) {
            if (identity(m.path) == identity(destination)) return "Project destination would overwrite source media: " + destination;
        }
    }
    return {};
}
QByteArray read(const QString& path, QString& error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = "Cannot read " + path + ": " + file.errorString(); return {}; }
    if (file.size() > MaxDocumentBytes) { error = path + ": document exceeds 16 MiB limit"; return {}; }
    const auto bytes = file.read(MaxDocumentBytes + 1);
    if (file.error() != QFileDevice::NoError) error = "Cannot read " + path + ": " + file.errorString();
    return bytes;
}
QString writeAtomic(const QString& path, const QByteArray& bytes, const BeforeCommit& beforeCommit = {}) {
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return "Cannot create temporary project file for " + path + ": " + file.errorString();
    const auto firstBytes = beforeCommit ? bytes.size() / 2 : bytes.size();
    if (file.write(bytes.constData(), firstBytes) != firstBytes || !file.flush()) {
        const auto error = file.errorString(); file.cancelWriting();
        return "Cannot write project " + path + ": " + error;
    }
    if (beforeCommit) beforeCommit();
    if (firstBytes != bytes.size() && file.write(bytes.constData() + firstBytes, bytes.size() - firstBytes) != bytes.size() - firstBytes) {
        const auto error = file.errorString(); file.cancelWriting();
        return "Cannot finish writing project " + path + ": " + error;
    }
    if (!file.commit()) return "Cannot commit project " + path + ": " + file.errorString();
    return {};
}
Project forDestination(const Project& project, const QString& path) {
    auto result = project;
    const auto directory = QFileInfo(path).absoluteDir();
    for (auto& m : result.media) m.path = directory.relativeFilePath(absolute(m.path));
    if (!result.exportSettings.outputPath.isEmpty()) result.exportSettings.outputPath = directory.relativeFilePath(absolute(result.exportSettings.outputPath));
    return result;
}
QString writeProject(const Project& p, const QString& path, bool backup, const BeforeCommit& beforeCommit) {
    if (const auto error = validate(p); !error.isEmpty()) return "Project validation failed: " + error;
    for (const auto& m : p.media)
        if (!QFileInfo(m.path).isAbsolute()) return "ProjectStore requires an absolute runtime media path: " + m.path;
    if (!p.exportSettings.outputPath.isEmpty() && !QFileInfo(p.exportSettings.outputPath).isAbsolute())
        return "ProjectStore requires an absolute runtime export path";
    if (const auto error = pathError(p, path, backup); !error.isEmpty()) return error;
    const auto target = absolute(path);
    QLockFile lock(target + ".lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return "Project is locked or its directory is not writable: " + target;
    // Never rotate a corrupt existing primary over the last good backup.
    if (backup && QFileInfo::exists(target)) {
        QString error;
        const auto old = read(target, error);
        if (!error.isEmpty()) return error;
        const auto parsed = deserialize(old);
        if (!parsed) return "Existing project is invalid; preserve it and use Save As: " + parsed.error;
        if (QFileInfo::exists(target + ".bak")) {
            const auto previous = read(target + ".bak", error);
            if (!error.isEmpty()) return error;
            if (!deserialize(previous)) return "Existing backup is invalid; preserve it and use Save As: " + target + ".bak";
            const auto retainedError = previous == old ? QString{} : writeAtomic(target + ".bak.2", previous);
            if (!retainedError.isEmpty()) return "Backup retention failed; primary was left unchanged. " + retainedError;
        }
        const auto backupError = writeAtomic(target + ".bak", old);
        if (!backupError.isEmpty()) return "Backup failed; primary was left unchanged. " + backupError;
    }
    return writeAtomic(target, serialize(forDestination(p, target)), beforeCommit);
}
}
QString ProjectStore::resolvedPath(const QString& path, const QString& documentPath) {
    return absolute(QFileInfo(path).isAbsolute() ? path : QFileInfo(documentPath).absoluteDir().filePath(path));
}
LoadResult ProjectStore::load(const QString& path) {
    QString error;
    const auto bytes = read(path, error);
    if (!error.isEmpty()) return {{}, error, 0, {}};
    auto result = deserialize(bytes);
    if (!result) { result.error = path + ": " + result.error; return result; }
    for (auto& m : result.project->media) {
        m.path = resolvedPath(m.path, path);
        QFile source(m.path);
        if (!QFileInfo(m.path).isFile() || !source.open(QIODevice::ReadOnly)) result.offlineMediaIds.append(m.id);
    }
    auto& output = result.project->exportSettings.outputPath;
    if (!output.isEmpty()) output = resolvedPath(output, path);
    return result;
}
QString ProjectStore::save(const Project& p, const QString& path, BeforeCommit beforeCommit) {
    return writeProject(p, path, true, beforeCommit);
}
QString ProjectStore::autosave(const Project& p, const QString& path) {
    return writeProject(p, path, false, {});
}
}

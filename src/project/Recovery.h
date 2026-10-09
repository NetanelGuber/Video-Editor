#pragma once
#include "ProjectStore.h"
#include <QDateTime>

namespace editor::project {
struct RecoveryCandidate {
    QString path, name, error;
    QDateTime modified;
};
// Discovery never mutates snapshots. A live editor's session lock excludes its snapshot.
QVector<RecoveryCandidate> recoveryCandidates(const QString& directory);
bool isRecoveryPath(const QString& path, const QString& directory);
bool mediaAvailable(const QString& path);
}

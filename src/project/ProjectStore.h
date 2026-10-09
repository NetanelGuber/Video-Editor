#pragma once
#include "Project.h"
#include <functional>

namespace editor::project {
// Test seam: invoked after half the target temporary file is written and flushed, before commit.
// Exceptions/process termination here must leave the old target and backup intact.
using BeforeCommit = std::function<void()>;
class ProjectStore {
public:
    static LoadResult load(const QString& path);
    static QString save(const Project& project, const QString& path, BeforeCommit beforeCommit = {});
    // The UI assigns an app-data recovery path to both saved and untitled projects.
    // Autosave replaces one snapshot atomically and never changes the primary file or its backup.
    static QString autosave(const Project& project, const QString& snapshotPath);
    static QString resolvedPath(const QString& storedPath, const QString& documentPath);
};
}

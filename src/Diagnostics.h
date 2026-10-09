#pragma once
#include <QObject>
#include <QString>

class QWidget;

class Diagnostics final : public QObject {
    Q_OBJECT
public:
    explicit Diagnostics(QString logPath, QObject* parent = nullptr);
    void reportError(QWidget* parent, const QString& operation, const QString& detail, const QString& technicalDetails = {});
    const QString& logPath() const { return logPath_; }
signals:
    void errorReported(const QString& operation, const QString& detail);
private:
    QString logPath_;
};

namespace editor::logging {
bool start(const QString& directory, QString& logPath, QString& failure);
void stop();
}

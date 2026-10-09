#include "Diagnostics.h"
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QCoreApplication>
#include <QMessageBox>
#include <QMutex>
#include <QMutexLocker>
#include <cstdio>
#include <memory>

namespace {
QMutex logMutex;
std::unique_ptr<QFile> logFile;
QtMessageHandler previousHandler = nullptr;

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    const char* level = "INFO";
    switch (type) {
    case QtDebugMsg: level = "DEBUG"; break;
    case QtInfoMsg: break;
    case QtWarningMsg: level = "WARNING"; break;
    case QtCriticalMsg: level = "ERROR"; break;
    case QtFatalMsg: level = "FATAL"; break;
    }
    auto safeMessage = message;
    safeMessage.replace('\r', QStringLiteral("\\r")).replace('\n', QStringLiteral("\\n"));
    const auto line = QStringLiteral("%1 [%2] [%3] %4\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
             QString::fromLatin1(level), QString::fromUtf8(context.category ? context.category : "default"), safeMessage)
        .toUtf8();
    QMutexLocker lock(&logMutex);
    if (logFile) {
        if (logFile->write(line) != line.size() || !logFile->flush()) {
            std::fputs("Video Editor: could not write diagnostics log.\n", stderr);
        }
    }
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stderr);
}
}

bool editor::logging::start(const QString& directory, QString& logPath, QString& failure) {
    if (!QDir().mkpath(directory)) {
        failure = QStringLiteral("Cannot create log directory: %1").arg(directory);
        return false;
    }
    logPath = QDir(directory).filePath(QStringLiteral("video-editor-%1-%2.log")
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")))
        .arg(QCoreApplication::applicationPid()));
    auto file = std::make_unique<QFile>(logPath);
    if (!file->open(QIODevice::WriteOnly | QIODevice::NewOnly | QIODevice::Text)) {
        failure = QStringLiteral("Cannot open log %1: %2").arg(logPath, file->errorString());
        return false;
    }
    logFile = std::move(file);
    previousHandler = qInstallMessageHandler(messageHandler);
    return true;
}

void editor::logging::stop() {
    qInstallMessageHandler(previousHandler);
    QMutexLocker lock(&logMutex);
    logFile.reset();
}

Diagnostics::Diagnostics(QString logPath, QObject* parent)
    : QObject(parent), logPath_(std::move(logPath)) {}

void Diagnostics::reportError(QWidget* parent, const QString& operation, const QString& detail, const QString& technicalDetails) {
    qCritical().noquote() << QStringLiteral("%1: %2").arg(operation, detail);
    emit errorReported(operation, detail);
    QMessageBox box(QMessageBox::Critical, operation, detail, QMessageBox::Ok, parent);
    box.setObjectName(QStringLiteral("errorDialog"));
    box.setInformativeText(QStringLiteral("Diagnostics log: %1").arg(logPath_));
    if (!technicalDetails.isEmpty()) { qCritical().noquote() << technicalDetails; box.setDetailedText(technicalDetails); }
    box.setTextFormat(Qt::PlainText);
    box.exec();
}

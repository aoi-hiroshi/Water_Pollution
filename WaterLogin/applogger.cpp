#include "applogger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QStandardPaths>

namespace {
constexpr qint64 kMaximumLogBytes = 5 * 1024 * 1024;

QDateTime parseTimestamp(const QString &text)
{
    QDateTime timestamp = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!timestamp.isValid()) {
        timestamp = QDateTime::fromString(text, Qt::ISODate);
    }
    return timestamp;
}
}

AppLogger &AppLogger::instance()
{
    static AppLogger logger;
    return logger;
}

AppLogger::AppLogger()
{
    QString dataDirectory = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    if (dataDirectory.isEmpty()) {
        dataDirectory = QCoreApplication::applicationDirPath();
    }

    QDir directory(dataDirectory);
    directory.mkpath(QStringLiteral("logs"));
    filePath = directory.filePath(QStringLiteral("logs/water-client.jsonl"));
}

QString AppLogger::typeName(AppLogType type)
{
    switch (type) {
    case AppLogType::Task:
        return QStringLiteral("任务日志");
    case AppLogType::Interface:
        return QStringLiteral("接口日志");
    case AppLogType::Alert:
        return QStringLiteral("告警日志");
    case AppLogType::System:
        return QStringLiteral("系统日志");
    }
    return QStringLiteral("系统日志");
}

QString AppLogger::levelName(AppLogLevel level)
{
    switch (level) {
    case AppLogLevel::Info:
        return QStringLiteral("信息");
    case AppLogLevel::Warning:
        return QStringLiteral("警告");
    case AppLogLevel::Error:
        return QStringLiteral("错误");
    }
    return QStringLiteral("信息");
}

void AppLogger::rotateIfNeeded()
{
    const QFileInfo information(filePath);
    if (!information.exists() || information.size() < kMaximumLogBytes) {
        return;
    }

    const QString backupPath = filePath + QStringLiteral(".1");
    QFile::remove(backupPath);
    QFile::rename(filePath, backupPath);
}

void AppLogger::log(AppLogType type,
                    AppLogLevel level,
                    const QString &module,
                    const QString &message,
                    const QString &status,
                    const QString &reference)
{
    QMutexLocker locker(&mutex);
    rotateIfNeeded();

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    const QJsonObject object{
        {QStringLiteral("timestamp"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("source"), QStringLiteral("客户端运行")},
        {QStringLiteral("type"), typeName(type)},
        {QStringLiteral("module"), module},
        {QStringLiteral("level"), levelName(level)},
        {QStringLiteral("message"), message},
        {QStringLiteral("status"), status},
        {QStringLiteral("reference"), reference}
    };
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    file.write("\n");
}

QVector<AppLogEntry> AppLogger::loadRecent(int limit) const
{
    QVector<AppLogEntry> entries;
    if (limit <= 0) {
        return entries;
    }

    QMutexLocker locker(&mutex);
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return entries;
    }

    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            continue;
        }

        const QJsonObject object = document.object();
        AppLogEntry entry;
        entry.timestamp = parseTimestamp(
            object.value(QStringLiteral("timestamp")).toString());
        entry.source = object.value(QStringLiteral("source")).toString(
            QStringLiteral("客户端运行"));
        entry.type = object.value(QStringLiteral("type")).toString();
        entry.module = object.value(QStringLiteral("module")).toString();
        entry.level = object.value(QStringLiteral("level")).toString();
        entry.message = object.value(QStringLiteral("message")).toString();
        entry.status = object.value(QStringLiteral("status")).toString();
        entry.reference = object.value(QStringLiteral("reference")).toString();
        entries.append(entry);
    }

    if (entries.size() > limit) {
        entries = entries.mid(entries.size() - limit);
    }
    return entries;
}

QString AppLogger::logFilePath() const
{
    QMutexLocker locker(&mutex);
    return filePath;
}

#ifndef APPLOGGER_H
#define APPLOGGER_H

#include <QDateTime>
#include <QMutex>
#include <QString>
#include <QVector>

enum class AppLogType
{
    Task,
    Interface,
    Alert,
    System
};

enum class AppLogLevel
{
    Info,
    Warning,
    Error
};

struct AppLogEntry
{
    QDateTime timestamp;
    QString source;
    QString type;
    QString module;
    QString level;
    QString message;
    QString status;
    QString reference;
};

// Meyers singleton. Since C++11, construction of the function-local static
// object in instance() is guaranteed to be thread-safe.
class AppLogger final
{
public:
    static AppLogger &instance();

    AppLogger(const AppLogger &) = delete;
    AppLogger &operator=(const AppLogger &) = delete;

    void log(AppLogType type,
             AppLogLevel level,
             const QString &module,
             const QString &message,
             const QString &status = QStringLiteral("完成"),
             const QString &reference = QString());

    QVector<AppLogEntry> loadRecent(int limit = 500) const;
    QString logFilePath() const;

    static QString typeName(AppLogType type);
    static QString levelName(AppLogLevel level);

private:
    AppLogger();
    ~AppLogger() = default;

    void rotateIfNeeded();

    mutable QMutex mutex;
    QString filePath;
};

#endif // APPLOGGER_H

#ifndef TRACESERVICE_H
#define TRACESERVICE_H

#include <QObject>
#include <QJsonObject>

class ApiClient;

class TraceService : public QObject
{
    Q_OBJECT

public:
    explicit TraceService(QObject *parent = nullptr);

    void classifySample(qint64 sampleId, const QString &datasetName, qint64 cleaningRunId = 0);

signals:
    void traceReady(const QJsonObject &result);
    void serviceError(const QString &message);

private slots:
    void onRequestSucceeded(const QString &requestKey, const QJsonObject &payload);
    void onRequestFailed(const QString &requestKey, const QString &message);

private:
    ApiClient *apiClient;
};

#endif // TRACESERVICE_H

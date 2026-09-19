#include "traceservice.h"

#include "apiclient.h"

#include <QJsonObject>

namespace {
const char kTraceRequest[] = "traceClassification";
}

TraceService::TraceService(QObject *parent)
    : QObject(parent)
    , apiClient(new ApiClient(this))
{
    connect(apiClient, &ApiClient::requestSucceeded,
            this, &TraceService::onRequestSucceeded);
    connect(apiClient, &ApiClient::requestFailed,
            this, &TraceService::onRequestFailed);
}

void TraceService::classifySample(qint64 sampleId, const QString &datasetName, qint64 cleaningRunId)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("sample_id"), static_cast<double>(sampleId));
    payload.insert(QStringLiteral("dataset"), datasetName);
    payload.insert(QStringLiteral("cleaning_run_id"), static_cast<double>(cleaningRunId));
    apiClient->post(QString::fromLatin1(kTraceRequest),
                    QStringLiteral("/api/v1/inference/classify"), payload);
}

void TraceService::onRequestSucceeded(const QString &requestKey,
                                      const QJsonObject &payload)
{
    if (requestKey != QLatin1String(kTraceRequest)) {
        return;
    }

    const int code = payload.value(QStringLiteral("code")).toInt();
    const QString message = payload.value(QStringLiteral("message")).toString();
    if (code != 200) {
        emit serviceError(message.isEmpty()
                          ? QStringLiteral("溯源接口返回失败") : message);
        return;
    }
    emit traceReady(payload.value(QStringLiteral("data")).toObject());
}

void TraceService::onRequestFailed(const QString &requestKey,
                                   const QString &message)
{
    if (requestKey == QLatin1String(kTraceRequest)) {
        emit serviceError(message);
    }
}

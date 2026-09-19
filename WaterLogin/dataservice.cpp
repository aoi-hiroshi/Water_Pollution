#include "dataservice.h"

#include "apiclient.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>

namespace {
const char kCompanyListRequest[] = "companyList";
const char kOverviewRequest[] = "dataOverview";
}

DataService::DataService(QObject *parent)
    : QObject(parent)
    , apiClient(new ApiClient(this))
{
    connect(apiClient, &ApiClient::requestSucceeded, this, &DataService::onRequestSucceeded);
    connect(apiClient, &ApiClient::requestFailed, this, &DataService::onRequestFailed);
}

void DataService::fetchCompanyList()
{
    apiClient->get(QString::fromLatin1(kCompanyListRequest), QStringLiteral("/api/v1/companies"));
}

void DataService::fetchOverview(int companyId, const QString &datasetName, int limit)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("company_id"), QString::number(companyId));
    query.addQueryItem(QStringLiteral("dataset"), datasetName);
    query.addQueryItem(QStringLiteral("limit"), QString::number(limit));

    activeDataRequest = QString::fromLatin1(kOverviewRequest) + QString::number(++requestSequence);
    apiClient->get(activeDataRequest, QStringLiteral("/api/v1/data/overview"), query);
}

void DataService::fetchClassification(const QString &operation, const QJsonObject &parameters)
{
    activeDataRequest = QStringLiteral("classification:") + QString::number(++requestSequence);
    apiClient->post(activeDataRequest, QStringLiteral("/api/v1/data/classification/") + operation, parameters);
}

void DataService::onRequestSucceeded(const QString &requestKey, const QJsonObject &payload)
{
    if (requestKey != QLatin1String(kCompanyListRequest) && requestKey != activeDataRequest) return;
    const int code = payload.value(QStringLiteral("code")).toInt();
    const QString message = payload.value(QStringLiteral("message")).toString();

    if (code != 200) {
        emit serviceError(message.isEmpty() ? QStringLiteral("接口返回失败") : message);
        return;
    }

    const QJsonValue dataValue = payload.value(QStringLiteral("data"));

    if (requestKey == QLatin1String(kCompanyListRequest)) {
        emit companyListReady(dataValue.toArray());
        return;
    }

    if (requestKey.startsWith(QLatin1String(kOverviewRequest))) {
        emit overviewReady(dataValue.toObject());
    } else if (requestKey.startsWith(QStringLiteral("classification:"))) {
        emit classificationReady(dataValue.toObject());
    }
}

void DataService::onRequestFailed(const QString &requestKey, const QString &message)
{
    if (requestKey == QLatin1String(kCompanyListRequest) || requestKey == activeDataRequest)
        emit serviceError(message);
}

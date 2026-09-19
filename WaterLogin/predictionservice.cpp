#include "predictionservice.h"
#include "apiclient.h"

PredictionService::PredictionService(QObject *parent)
    : QObject(parent), apiClient(new ApiClient(this))
{
    connect(apiClient, &ApiClient::requestSucceeded, this,
            [this](const QString &key, const QJsonObject &payload) {
        const QJsonValue data = payload.value("data");
        if (payload.value("code").toInt() != 200 || !data.isObject()) {
            emit serviceError(key, payload.value("message").toString("预测接口响应格式错误")); return;
        }
        if (key == "forecastModel") emit modelReady(data.toObject());
        else if (key == "forecast") emit forecastReady(data.toObject());
    });
    connect(apiClient, &ApiClient::requestFailed, this, &PredictionService::serviceError);
}
void PredictionService::fetchModel() { apiClient->get("forecastModel", "/api/v1/inference/forecast/model"); }
void PredictionService::forecast(qint64 companyId, const QString &dataset, qint64 endSampleId, int horizon)
{
    QJsonObject body;
    body.insert("company_id", static_cast<double>(companyId)); body.insert("dataset", dataset);
    body.insert("end_sample_id", static_cast<double>(endSampleId)); body.insert("horizon", horizon);
    apiClient->post("forecast", "/api/v1/inference/forecast", body);
}

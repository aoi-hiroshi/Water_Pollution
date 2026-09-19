#ifndef PREDICTIONSERVICE_H
#define PREDICTIONSERVICE_H
#include <QObject>
#include <QJsonObject>
class ApiClient;
class PredictionService : public QObject
{
    Q_OBJECT
public:
    explicit PredictionService(QObject *parent = nullptr);
    void fetchModel();
    void forecast(qint64 companyId, const QString &dataset, qint64 endSampleId, int horizon);
signals:
    void modelReady(const QJsonObject &model);
    void forecastReady(const QJsonObject &result);
    void serviceError(const QString &requestKey, const QString &message);
private:
    ApiClient *apiClient;
};
#endif

#ifndef DATASERVICE_H
#define DATASERVICE_H

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>

class ApiClient;

class DataService : public QObject
{
    Q_OBJECT

public:
    explicit DataService(QObject *parent = nullptr);

    void fetchCompanyList();
    void fetchOverview(int companyId, const QString &datasetName, int limit = 10);
    void fetchClassification(const QString &operation, const QJsonObject &parameters);

signals:
    void companyListReady(const QJsonArray &companies);
    void overviewReady(const QJsonObject &overview);
    void classificationReady(const QJsonObject &result);
    void serviceError(const QString &message);

private slots:
    void onRequestSucceeded(const QString &requestKey, const QJsonObject &payload);
    void onRequestFailed(const QString &requestKey, const QString &message);

private:
    ApiClient *apiClient;
    quint64 requestSequence = 0;
    QString activeDataRequest;
};

#endif // DATASERVICE_H

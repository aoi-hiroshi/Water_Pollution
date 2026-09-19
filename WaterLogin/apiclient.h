#ifndef APICLIENT_H
#define APICLIENT_H

#include <QObject>
#include <QJsonObject>
#include <QUrlQuery>

class QNetworkAccessManager;
class QNetworkReply;

class ApiClient : public QObject
{
    Q_OBJECT

public:
    explicit ApiClient(QObject *parent = nullptr);

    void setBaseUrl(const QString &baseUrl);
    void setRequestTimeout(int milliseconds);
    void get(const QString &requestKey, const QString &path, const QUrlQuery &query = QUrlQuery());
    void post(const QString &requestKey, const QString &path, const QJsonObject &payload);

signals:
    void requestSucceeded(const QString &requestKey, const QJsonObject &payload);
    void requestFailed(const QString &requestKey, const QString &message);

private:
    void watchReply(const QString &requestKey, QNetworkReply *reply);
    void handleReply(const QString &requestKey, QNetworkReply *reply);

private:
    QNetworkAccessManager *networkManager;
    QString baseUrl;
    int requestTimeoutMs;
};

#endif // APICLIENT_H

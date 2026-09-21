#include "apiclient.h"
#include "applogger.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVariant>

ApiClient::ApiClient(QObject *parent)
    : QObject(parent)
    , networkManager(new QNetworkAccessManager(this))
    , baseUrl(QString::fromUtf8(qgetenv("WATER_API_BASE_URL")))
    , requestTimeoutMs(15000)
{
    if (baseUrl.trimmed().isEmpty()) {
        baseUrl = QStringLiteral("http://127.0.0.1:8080");
    }
    while (baseUrl.endsWith(QLatin1Char('/'))) {
        baseUrl.chop(1);
    }
}

void ApiClient::setBaseUrl(const QString &newBaseUrl)
{
    baseUrl = newBaseUrl.trimmed();
    while (baseUrl.endsWith(QLatin1Char('/'))) {
        baseUrl.chop(1);
    }
}

void ApiClient::setRequestTimeout(int milliseconds)
{
    if (milliseconds > 0) {
        requestTimeoutMs = milliseconds;
    }
}

void ApiClient::get(const QString &requestKey, const QString &path, const QUrlQuery &query)
{
    QUrl url(baseUrl + path);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("X-Request-Id", QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());

    QNetworkReply *reply = networkManager->get(request);
    AppLogger::instance().log(
        AppLogType::Interface, AppLogLevel::Info,
        requestKey, QStringLiteral("GET %1").arg(url.toString()),
        QStringLiteral("已发送"));
    watchReply(requestKey, reply);
}

void ApiClient::post(const QString &requestKey, const QString &path, const QJsonObject &payload)
{
    QUrl url(baseUrl + path);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("X-Request-Id", QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());

    QNetworkReply *reply = networkManager->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    AppLogger::instance().log(
        AppLogType::Interface, AppLogLevel::Info,
        requestKey, QStringLiteral("POST %1").arg(url.toString()),
        QStringLiteral("已发送"));
    watchReply(requestKey, reply);
}

void ApiClient::watchReply(const QString &requestKey, QNetworkReply *reply)
{
    QTimer *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [reply]() {
        reply->setProperty("waterRequestTimedOut", true);
        reply->abort();
    });
    timer->start(requestTimeoutMs);

    connect(reply, &QNetworkReply::finished, this, [this, requestKey, reply]() {
        handleReply(requestKey, reply);
    });
}

void ApiClient::handleReply(const QString &requestKey, QNetworkReply *reply)
{
    const QByteArray responseBody = reply->readAll();
    const int httpStatus = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->property("waterRequestTimedOut").toBool()) {
        AppLogger::instance().log(
            AppLogType::Interface, AppLogLevel::Warning,
            requestKey, QStringLiteral("请求超时：%1")
                            .arg(reply->request().url().toString()),
            QStringLiteral("失败"));
        emit requestFailed(requestKey, QStringLiteral("请求超时，请检查 Muduo 服务状态"));
        reply->deleteLater();
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument jsonDocument = QJsonDocument::fromJson(responseBody, &parseError);
    const QJsonObject payload = jsonDocument.isObject()
        ? jsonDocument.object() : QJsonObject();

    if (httpStatus >= 400) {
        QString message = payload.value(QStringLiteral("message")).toString();
        if (message.isEmpty()) {
            message = QStringLiteral("服务端返回 HTTP %1").arg(httpStatus);
        }
        AppLogger::instance().log(
            AppLogType::Interface, AppLogLevel::Error,
            requestKey,
            QStringLiteral("HTTP %1：%2").arg(httpStatus).arg(message),
            QStringLiteral("失败"), reply->request().url().toString());
        emit requestFailed(requestKey, message);
        reply->deleteLater();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        AppLogger::instance().log(
            AppLogType::Interface, AppLogLevel::Error,
            requestKey, reply->errorString(), QStringLiteral("失败"),
            reply->request().url().toString());
        emit requestFailed(requestKey, reply->errorString());
        reply->deleteLater();
        return;
    }

    if (parseError.error != QJsonParseError::NoError || !jsonDocument.isObject()) {
        AppLogger::instance().log(
            AppLogType::Interface, AppLogLevel::Warning,
            requestKey, QStringLiteral("服务端返回的 JSON 格式无效"),
            QStringLiteral("失败"), reply->request().url().toString());
        emit requestFailed(requestKey, QStringLiteral("服务端返回的 JSON 格式无效"));
        reply->deleteLater();
        return;
    }

    AppLogger::instance().log(
        AppLogType::Interface, AppLogLevel::Info,
        requestKey, QStringLiteral("HTTP %1 请求完成").arg(httpStatus),
        QStringLiteral("完成"), reply->request().url().toString());
    emit requestSucceeded(requestKey, payload);
    reply->deleteLater();
}

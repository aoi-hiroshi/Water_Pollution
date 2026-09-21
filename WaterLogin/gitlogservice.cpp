#include "gitlogservice.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

GitLogService::GitLogService(QObject *parent)
    : QObject(parent)
    , networkManager(new QNetworkAccessManager(this))
    , repository(QString::fromUtf8(qgetenv("WATER_GITHUB_REPOSITORY")).trimmed())
    , commitsUrl(QString::fromUtf8(qgetenv("WATER_GITHUB_COMMITS_URL")).trimmed())
{
    if (repository.isEmpty()) {
        repository = QStringLiteral("aoi-hiroshi/Water_Pollution");
    }
    if (commitsUrl.isEmpty()) {
        commitsUrl = QStringLiteral("https://api.github.com/repos/%1/commits?per_page=100")
                         .arg(repository);
    }
}

QString GitLogService::repositoryName() const
{
    return repository;
}

void GitLogService::fetchRecentCommits()
{
    if (loading) {
        return;
    }

    loading = true;
    emit loadingChanged(true);

    QNetworkRequest request{QUrl(commitsUrl)};
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "water-quality-system/1.0");

    const QByteArray token = qgetenv("WATER_GITHUB_TOKEN").trimmed();
    if (!token.isEmpty()) {
        request.setRawHeader("Authorization", QByteArray("Bearer ") + token);
    }

    QNetworkReply *reply = networkManager->get(request);
    QTimer *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [reply]() {
        reply->setProperty("waterGitTimedOut", true);
        reply->abort();
    });
    timer->start(12000);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        loading = false;
        emit loadingChanged(false);

        const QByteArray body = reply->readAll();
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->property("waterGitTimedOut").toBool()) {
            emit fetchFailed(QStringLiteral("GitHub提交记录请求超时"));
            reply->deleteLater();
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (status >= 400 || reply->error() != QNetworkReply::NoError) {
            QString message;
            if (document.isObject()) {
                message = document.object().value(QStringLiteral("message")).toString();
            }
            if (message.isEmpty()) {
                message = reply->errorString();
            }
            emit fetchFailed(QStringLiteral("GitHub提交记录加载失败：%1").arg(message));
            reply->deleteLater();
            return;
        }

        if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
            emit fetchFailed(QStringLiteral("GitHub返回的数据格式无效"));
            reply->deleteLater();
            return;
        }

        emit commitsReady(document.array());
        reply->deleteLater();
    });
}

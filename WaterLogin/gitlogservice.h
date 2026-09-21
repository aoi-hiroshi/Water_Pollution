#ifndef GITLOGSERVICE_H
#define GITLOGSERVICE_H

#include <QJsonArray>
#include <QObject>

class QNetworkAccessManager;

class GitLogService : public QObject
{
    Q_OBJECT

public:
    explicit GitLogService(QObject *parent = nullptr);

    void fetchRecentCommits();
    QString repositoryName() const;

signals:
    void commitsReady(const QJsonArray &commits);
    void fetchFailed(const QString &message);
    void loadingChanged(bool loading);

private:
    QNetworkAccessManager *networkManager;
    QString repository;
    QString commitsUrl;
    bool loading = false;
};

#endif // GITLOGSERVICE_H

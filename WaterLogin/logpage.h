#ifndef LOGPAGE_H
#define LOGPAGE_H

#include "applogger.h"

#include <QJsonArray>
#include <QVector>
#include <QWidget>

class GitLogService;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QTableWidget;
class QTimer;

class LogPage : public QWidget
{
    Q_OBJECT

public:
    explicit LogPage(QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void refreshLogs();
    void applyFilters();
    void exportVisibleLogs();
    void handleGitCommits(const QJsonArray &commits);
    void handleGitError(const QString &message);
    void openCommitLink(int row, int column);

private:
    void initUI();
    void rebuildEntries();
    bool matchesFilters(const AppLogEntry &entry) const;
    void updateStatus(int visibleCount);

    GitLogService *gitLogService;
    QComboBox *sourceBox;
    QComboBox *typeBox;
    QComboBox *levelBox;
    QLineEdit *keywordEdit;
    QPushButton *queryButton;
    QPushButton *refreshButton;
    QPushButton *exportButton;
    QLabel *syncStatusLabel;
    QLabel *filePathLabel;
    QTableWidget *logTable;
    QTimer *autoRefreshTimer;

    QVector<AppLogEntry> runtimeEntries;
    QVector<AppLogEntry> githubEntries;
    QVector<AppLogEntry> entries;
    QString gitError;
    bool gitLoading = false;
};

#endif // LOGPAGE_H

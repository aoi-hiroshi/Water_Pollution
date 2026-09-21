#include "logpage.h"

#include "gitlogservice.h"

#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QShowEvent>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {
QString firstLine(QString text)
{
    const int newline = text.indexOf(QLatin1Char('\n'));
    if (newline >= 0) {
        text.truncate(newline);
    }
    return text.trimmed();
}

QDateTime parseGitDate(const QString &text)
{
    QDateTime timestamp = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!timestamp.isValid()) {
        timestamp = QDateTime::fromString(text, Qt::ISODate);
    }
    return timestamp;
}

QString csvText(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"") + value + QStringLiteral("\"");
}

QColor levelColor(const QString &level)
{
    if (level == QStringLiteral("错误")) {
        return QColor(QStringLiteral("#D14343"));
    }
    if (level == QStringLiteral("警告")) {
        return QColor(QStringLiteral("#B7791F"));
    }
    return QColor(QStringLiteral("#2878D0"));
}
}

LogPage::LogPage(QWidget *parent)
    : QWidget(parent)
    , gitLogService(new GitLogService(this))
    , sourceBox(nullptr)
    , typeBox(nullptr)
    , levelBox(nullptr)
    , keywordEdit(nullptr)
    , queryButton(nullptr)
    , refreshButton(nullptr)
    , exportButton(nullptr)
    , syncStatusLabel(nullptr)
    , filePathLabel(nullptr)
    , logTable(nullptr)
    , autoRefreshTimer(new QTimer(this))
{
    initUI();

    connect(gitLogService, &GitLogService::commitsReady,
            this, &LogPage::handleGitCommits);
    connect(gitLogService, &GitLogService::fetchFailed,
            this, &LogPage::handleGitError);
    connect(gitLogService, &GitLogService::loadingChanged,
            this, [this](bool loading) {
        gitLoading = loading;
        refreshButton->setEnabled(!loading);
        updateStatus(logTable->rowCount());
    });

    AppLogger::instance().log(
        AppLogType::System, AppLogLevel::Info,
        QStringLiteral("日志中心"), QStringLiteral("打开日志中心页面"));

    autoRefreshTimer->setInterval(5 * 60 * 1000);
    connect(autoRefreshTimer, &QTimer::timeout,
            this, &LogPage::refreshLogs);
    autoRefreshTimer->start();
    refreshLogs();
}

void LogPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    runtimeEntries = AppLogger::instance().loadRecent(500);
    rebuildEntries();
}

void LogPage::initUI()
{
    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget *content = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(content);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(12);

    QFrame *summaryCard = new QFrame;
    summaryCard->setObjectName(QStringLiteral("infoCard"));
    QVBoxLayout *summaryLayout = new QVBoxLayout(summaryCard);
    summaryLayout->setContentsMargins(16, 12, 16, 12);
    summaryLayout->setSpacing(6);

    QHBoxLayout *summaryTitleLayout = new QHBoxLayout;
    QLabel *summaryTitle = new QLabel(QStringLiteral("日志数据源"));
    summaryTitle->setObjectName(QStringLiteral("cardTitle"));
    syncStatusLabel = new QLabel;
    syncStatusLabel->setObjectName(QStringLiteral("logStatusLabel"));
    syncStatusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    summaryTitleLayout->addWidget(summaryTitle);
    summaryTitleLayout->addStretch();
    summaryTitleLayout->addWidget(syncStatusLabel);

    QLabel *sourceDescription = new QLabel(
        QStringLiteral("客户端运行日志来自线程安全 AppLogger；开发日志来自 GitHub 提交接口。"
                       "GitHub 每5分钟自动同步，也可在 push 后点击“刷新全部”。"
                       "Muduo 服务端日志尚未提供查询 API，因此不会伪装为已接入。"));
    sourceDescription->setObjectName(QStringLiteral("cardSubText"));
    sourceDescription->setWordWrap(true);

    filePathLabel = new QLabel;
    filePathLabel->setObjectName(QStringLiteral("cardSubText"));
    filePathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    filePathLabel->setText(QStringLiteral("本地日志文件：%1")
                               .arg(AppLogger::instance().logFilePath()));

    summaryLayout->addLayout(summaryTitleLayout);
    summaryLayout->addWidget(sourceDescription);
    summaryLayout->addWidget(filePathLabel);

    QFrame *filterCard = new QFrame;
    filterCard->setObjectName(QStringLiteral("infoCard"));
    QHBoxLayout *filterLayout = new QHBoxLayout(filterCard);
    filterLayout->setContentsMargins(16, 12, 16, 12);
    filterLayout->setSpacing(10);

    QLabel *filterTitle = new QLabel(QStringLiteral("日志筛选"));
    filterTitle->setObjectName(QStringLiteral("cardTitle"));

    sourceBox = new QComboBox;
    sourceBox->addItems(QStringList()
                        << QStringLiteral("全部来源")
                        << QStringLiteral("客户端运行")
                        << QStringLiteral("GitHub提交"));

    typeBox = new QComboBox;
    typeBox->addItems(QStringList()
                      << QStringLiteral("全部类型")
                      << QStringLiteral("任务日志")
                      << QStringLiteral("接口日志")
                      << QStringLiteral("告警日志")
                      << QStringLiteral("系统日志")
                      << QStringLiteral("开发日志"));

    levelBox = new QComboBox;
    levelBox->addItems(QStringList()
                       << QStringLiteral("全部级别")
                       << QStringLiteral("信息")
                       << QStringLiteral("警告")
                       << QStringLiteral("错误"));

    keywordEdit = new QLineEdit;
    keywordEdit->setPlaceholderText(QStringLiteral("搜索模块、提交说明、状态或SHA"));
    keywordEdit->setClearButtonEnabled(true);
    keywordEdit->setMinimumWidth(230);

    queryButton = new QPushButton(QStringLiteral("查询"));
    queryButton->setObjectName(QStringLiteral("primaryButton"));
    queryButton->setFixedHeight(34);

    refreshButton = new QPushButton(QStringLiteral("刷新全部"));
    refreshButton->setObjectName(QStringLiteral("secondaryButton"));
    refreshButton->setFixedHeight(34);

    exportButton = new QPushButton(QStringLiteral("导出当前结果"));
    exportButton->setObjectName(QStringLiteral("secondaryButton"));
    exportButton->setFixedHeight(34);

    filterLayout->addWidget(filterTitle);
    filterLayout->addStretch();
    filterLayout->addWidget(sourceBox);
    filterLayout->addWidget(typeBox);
    filterLayout->addWidget(levelBox);
    filterLayout->addWidget(keywordEdit, 1);
    filterLayout->addWidget(queryButton);
    filterLayout->addWidget(refreshButton);
    filterLayout->addWidget(exportButton);

    QFrame *tableCard = new QFrame;
    tableCard->setObjectName(QStringLiteral("infoCard"));
    QVBoxLayout *tableLayout = new QVBoxLayout(tableCard);
    tableLayout->setContentsMargins(16, 12, 16, 12);
    tableLayout->setSpacing(10);

    QLabel *tableTitle = new QLabel(QStringLiteral("日志记录"));
    tableTitle->setObjectName(QStringLiteral("cardTitle"));

    logTable = new QTableWidget(0, 7);
    logTable->setHorizontalHeaderLabels(QStringList()
                                        << QStringLiteral("时间")
                                        << QStringLiteral("来源")
                                        << QStringLiteral("类型")
                                        << QStringLiteral("模块/提交人")
                                        << QStringLiteral("级别")
                                        << QStringLiteral("内容")
                                        << QStringLiteral("状态"));
    logTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    logTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    logTable->verticalHeader()->setVisible(false);
    logTable->setAlternatingRowColors(true);
    logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    logTable->setSelectionMode(QAbstractItemView::SingleSelection);
    logTable->setMinimumHeight(460);
    logTable->setWordWrap(false);
    logTable->setSortingEnabled(false);

    QLabel *tableHint = new QLabel(
        QStringLiteral("提示：双击 GitHub 提交记录可以打开对应提交页面。"));
    tableHint->setObjectName(QStringLiteral("cardSubText"));

    tableLayout->addWidget(tableTitle);
    tableLayout->addWidget(logTable, 1);
    tableLayout->addWidget(tableHint);

    layout->addWidget(summaryCard);
    layout->addWidget(filterCard);
    layout->addWidget(tableCard, 1);

    scrollArea->setWidget(content);
    rootLayout->addWidget(scrollArea);

    connect(queryButton, &QPushButton::clicked,
            this, &LogPage::applyFilters);
    connect(keywordEdit, &QLineEdit::returnPressed,
            this, &LogPage::applyFilters);
    connect(keywordEdit, &QLineEdit::textChanged,
            this, [this](const QString &text) {
        if (text.isEmpty()) {
            applyFilters();
        }
    });
    connect(sourceBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogPage::applyFilters);
    connect(typeBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogPage::applyFilters);
    connect(levelBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogPage::applyFilters);
    connect(refreshButton, &QPushButton::clicked,
            this, &LogPage::refreshLogs);
    connect(exportButton, &QPushButton::clicked,
            this, &LogPage::exportVisibleLogs);
    connect(logTable, &QTableWidget::cellDoubleClicked,
            this, &LogPage::openCommitLink);
}

void LogPage::refreshLogs()
{
    runtimeEntries = AppLogger::instance().loadRecent(500);
    gitError.clear();
    rebuildEntries();
    gitLogService->fetchRecentCommits();
}

void LogPage::rebuildEntries()
{
    entries = runtimeEntries;
    entries += githubEntries;
    std::stable_sort(entries.begin(), entries.end(),
                     [](const AppLogEntry &left, const AppLogEntry &right) {
        return left.timestamp > right.timestamp;
    });
    applyFilters();
}

bool LogPage::matchesFilters(const AppLogEntry &entry) const
{
    if (sourceBox->currentIndex() > 0 &&
        entry.source != sourceBox->currentText()) {
        return false;
    }
    if (typeBox->currentIndex() > 0 &&
        entry.type != typeBox->currentText()) {
        return false;
    }
    if (levelBox->currentIndex() > 0 &&
        entry.level != levelBox->currentText()) {
        return false;
    }

    const QString keyword = keywordEdit->text().trimmed();
    if (keyword.isEmpty()) {
        return true;
    }
    const QString searchable = QStringLiteral("%1 %2 %3 %4 %5 %6 %7")
                                   .arg(entry.source, entry.type, entry.module,
                                        entry.level, entry.message,
                                        entry.status, entry.reference);
    return searchable.contains(keyword, Qt::CaseInsensitive);
}

void LogPage::applyFilters()
{
    logTable->setUpdatesEnabled(false);
    logTable->setRowCount(0);

    int visibleCount = 0;
    for (const AppLogEntry &entry : entries) {
        if (!matchesFilters(entry)) {
            continue;
        }

        const int row = logTable->rowCount();
        logTable->insertRow(row);
        const QString timeText = entry.timestamp.isValid()
            ? entry.timestamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
            : QStringLiteral("-");
        const QStringList values{
            timeText, entry.source, entry.type, entry.module,
            entry.level, entry.message, entry.status
        };
        for (int column = 0; column < values.size(); ++column) {
            QTableWidgetItem *item = new QTableWidgetItem(values.at(column));
            if (!entry.reference.isEmpty()) {
                item->setData(Qt::UserRole, entry.reference);
            }
            if (column == 4) {
                item->setForeground(levelColor(entry.level));
                item->setTextAlignment(Qt::AlignCenter);
            }
            if (column == 6) {
                item->setTextAlignment(Qt::AlignCenter);
            }
            logTable->setItem(row, column, item);
        }
        ++visibleCount;
    }

    logTable->setUpdatesEnabled(true);
    exportButton->setEnabled(visibleCount > 0);
    updateStatus(visibleCount);
}

void LogPage::updateStatus(int visibleCount)
{
    QString gitState;
    if (gitLoading) {
        gitState = QStringLiteral("GitHub同步中...");
    } else if (!gitError.isEmpty()) {
        gitState = gitError;
    } else {
        gitState = QStringLiteral("GitHub %1 条").arg(githubEntries.size());
    }

    syncStatusLabel->setText(
        QStringLiteral("本地 %1 条 | %2 | 当前显示 %3 条")
            .arg(runtimeEntries.size())
            .arg(gitState)
            .arg(visibleCount));
    syncStatusLabel->setToolTip(gitError);
}

void LogPage::handleGitCommits(const QJsonArray &commits)
{
    githubEntries.clear();
    for (const QJsonValue &value : commits) {
        const QJsonObject root = value.toObject();
        const QJsonObject commit = root.value(QStringLiteral("commit")).toObject();
        const QJsonObject author = commit.value(QStringLiteral("author")).toObject();
        const QJsonObject account = root.value(QStringLiteral("author")).toObject();

        AppLogEntry entry;
        entry.timestamp = parseGitDate(author.value(QStringLiteral("date")).toString());
        entry.source = QStringLiteral("GitHub提交");
        entry.type = QStringLiteral("开发日志");
        entry.module = account.value(QStringLiteral("login")).toString();
        if (entry.module.isEmpty()) {
            entry.module = author.value(QStringLiteral("name")).toString(
                QStringLiteral("未知提交人"));
        }
        entry.level = QStringLiteral("信息");
        const QString sha = root.value(QStringLiteral("sha")).toString().left(7);
        entry.message = QStringLiteral("%1  %2")
                            .arg(sha, firstLine(commit.value(
                                QStringLiteral("message")).toString()));
        entry.status = QStringLiteral("已推送");
        entry.reference = root.value(QStringLiteral("html_url")).toString();
        githubEntries.append(entry);
    }

    gitError.clear();
    AppLogger::instance().log(
        AppLogType::Interface, AppLogLevel::Info,
        QStringLiteral("GitHub"),
        QStringLiteral("同步开发提交记录 %1 条").arg(githubEntries.size()));
    runtimeEntries = AppLogger::instance().loadRecent(500);
    rebuildEntries();
}

void LogPage::handleGitError(const QString &message)
{
    gitError = message;
    AppLogger::instance().log(
        AppLogType::Interface, AppLogLevel::Warning,
        QStringLiteral("GitHub"), message, QStringLiteral("同步失败"));
    runtimeEntries = AppLogger::instance().loadRecent(500);
    rebuildEntries();
}

void LogPage::openCommitLink(int row, int)
{
    QTableWidgetItem *item = logTable->item(row, 5);
    if (!item) {
        return;
    }
    const QUrl url(item->data(Qt::UserRole).toString());
    if (url.isValid() && !url.isEmpty()) {
        QDesktopServices::openUrl(url);
    }
}

void LogPage::exportVisibleLogs()
{
    const QString defaultName = QStringLiteral("water_logs_%1.csv")
                                    .arg(QDateTime::currentDateTime().toString(
                                        QStringLiteral("yyyyMMdd_HHmmss")));
    const QString fileName = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出当前日志"), defaultName,
        QStringLiteral("CSV 文件 (*.csv)"));
    if (fileName.isEmpty()) {
        return;
    }

    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"),
                             QStringLiteral("无法创建导出文件"));
        return;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << QChar(0xFEFF);
    stream << QStringLiteral("时间,来源,类型,模块或提交人,级别,内容,状态,链接\n");
    for (int row = 0; row < logTable->rowCount(); ++row) {
        QStringList columns;
        for (int column = 0; column < logTable->columnCount(); ++column) {
            QTableWidgetItem *item = logTable->item(row, column);
            columns.append(csvText(item ? item->text() : QString()));
        }
        QTableWidgetItem *contentItem = logTable->item(row, 5);
        columns.append(csvText(contentItem
                                   ? contentItem->data(Qt::UserRole).toString()
                                   : QString()));
        stream << columns.join(QLatin1Char(',')) << QLatin1Char('\n');
    }

    if (!file.commit()) {
        QMessageBox::warning(this, QStringLiteral("导出失败"),
                             QStringLiteral("日志文件保存失败"));
        return;
    }

    AppLogger::instance().log(
        AppLogType::Task, AppLogLevel::Info,
        QStringLiteral("日志中心"),
        QStringLiteral("导出筛选日志 %1 条").arg(logTable->rowCount()),
        QStringLiteral("完成"), fileName);
    QMessageBox::information(this, QStringLiteral("导出完成"),
                             QStringLiteral("已导出 %1 条日志")
                                 .arg(logTable->rowCount()));
    runtimeEntries = AppLogger::instance().loadRecent(500);
    rebuildEntries();
}

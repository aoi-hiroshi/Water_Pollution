#include "logpage.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
LogPage::LogPage(QWidget *parent)
    : QWidget(parent)
{
    initUI();
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

    QFrame *filterCard = new QFrame;
    filterCard->setObjectName("infoCard");
    QHBoxLayout *filterLayout = new QHBoxLayout(filterCard);
    filterLayout->setContentsMargins(16, 12, 16, 12);
    filterLayout->setSpacing(10);

    QLabel *filterTitle = new QLabel("日志筛选");
    filterTitle->setObjectName("cardTitle");

    QComboBox *typeBox = new QComboBox;
    typeBox->addItems(QStringList() << "全部类型" << "任务日志" << "接口日志" << "告警日志" << "系统日志");

    QComboBox *levelBox = new QComboBox;
    levelBox->addItems(QStringList() << "全部级别" << "信息" << "警告" << "错误");

    QLineEdit *keywordEdit = new QLineEdit;
    keywordEdit->setPlaceholderText("输入关键字");

    QPushButton *queryBtn = new QPushButton("查询日志");
    queryBtn->setObjectName("primaryButton");
    queryBtn->setFixedHeight(34);

    QPushButton *exportBtn = new QPushButton("导出");
    exportBtn->setObjectName("secondaryButton");
    exportBtn->setFixedHeight(34);

    filterLayout->addWidget(filterTitle);
    filterLayout->addStretch();
    filterLayout->addWidget(typeBox);
    filterLayout->addWidget(levelBox);
    filterLayout->addWidget(keywordEdit);
    filterLayout->addWidget(queryBtn);
    filterLayout->addWidget(exportBtn);

    QFrame *tableCard = new QFrame;
    tableCard->setObjectName("infoCard");
    QVBoxLayout *tableLayout = new QVBoxLayout(tableCard);
    tableLayout->setContentsMargins(16, 12, 16, 12);
    tableLayout->setSpacing(10);

    QLabel *tableTitle = new QLabel("日志记录");
    tableTitle->setObjectName("cardTitle");

    QTableWidget *logTable = new QTableWidget(6, 5);
    logTable->setHorizontalHeaderLabels(QStringList() << "时间" << "模块" << "级别" << "内容" << "状态");
    logTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    logTable->verticalHeader()->setVisible(false);
    logTable->setAlternatingRowColors(true);
    logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    logTable->setMinimumHeight(320);

    const QStringList modules = {"认证", "数据", "溯源", "预测", "接口", "告警"};
    const QStringList levels = {"信息", "信息", "警告", "信息", "错误", "信息"};
    const QStringList messages = {
        "管理员登录成功",
        "已导入 120 条样本数据",
        "溯源任务等待后端处理",
        "预测请求体已准备完成",
        "POST /api/v1/inference/forecast 待接 ONNX",
        "当前没有活动告警"
    };

    for (int row = 0; row < logTable->rowCount(); ++row) {
        logTable->setItem(row, 0, new QTableWidgetItem(QString("2026-04-25 1%1:30").arg(row)));
        logTable->setItem(row, 1, new QTableWidgetItem(modules.at(row)));
        logTable->setItem(row, 2, new QTableWidgetItem(levels.at(row)));
        logTable->setItem(row, 3, new QTableWidgetItem(messages.at(row)));
        logTable->setItem(row, 4, new QTableWidgetItem(row == 4 ? "待处理" : "完成"));
    }

    tableLayout->addWidget(tableTitle);
    tableLayout->addWidget(logTable);

    layout->addWidget(filterCard);
    layout->addWidget(tableCard, 1);

    scrollArea->setWidget(content);
    rootLayout->addWidget(scrollArea);
}

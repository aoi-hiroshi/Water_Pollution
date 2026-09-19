#include "homepage.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
QFrame *createInfoCard(const QString &title,
                       const QString &body,
                       const QString &buttonText = QString(),
                       const QString &buttonObjectName = QStringLiteral("secondaryButton"))
{
    QFrame *card = new QFrame;
    card->setObjectName("infoCard");
    card->setMinimumHeight(140);

    QVBoxLayout *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);

    QLabel *titleLabel = new QLabel(title);
    titleLabel->setObjectName("cardTitle");

    QLabel *bodyLabel = new QLabel(body);
    bodyLabel->setObjectName("cardSubText");
    bodyLabel->setWordWrap(true);

    layout->addWidget(titleLabel);
    layout->addWidget(bodyLabel);
    layout->addStretch();

    if (!buttonText.isEmpty()) {
        QPushButton *button = new QPushButton(buttonText);
        button->setObjectName(buttonObjectName);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(34);
        layout->addWidget(button);
    }

    return card;
}
}

HomePage::HomePage(QWidget *parent)
    : QWidget(parent)
{
    initUI();
}

void HomePage::initUI()
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

    QFrame *heroCard = new QFrame;
    heroCard->setObjectName("displayCard");
    heroCard->setMinimumHeight(320);

    QVBoxLayout *heroLayout = new QVBoxLayout(heroCard);
    heroLayout->setContentsMargins(0, 0, 0, 0);
    heroLayout->setSpacing(0);

    QFrame *noticeBar = new QFrame;
    noticeBar->setObjectName("noticeBar");
    noticeBar->setFixedHeight(42);

    QHBoxLayout *noticeLayout = new QHBoxLayout(noticeBar);
    noticeLayout->setContentsMargins(14, 0, 14, 0);

    QLabel *noticeLabel = new QLabel("当前已切换到 C++ Muduo API，数据与模型能力将通过统一 HTTP 接口提供。");
    noticeLabel->setObjectName("noticeLabel");
    noticeLayout->addWidget(noticeLabel);

    QFrame *overviewArea = new QFrame;
    overviewArea->setObjectName("mapArea");

    QVBoxLayout *overviewLayout = new QVBoxLayout(overviewArea);
    overviewLayout->setContentsMargins(24, 24, 24, 24);
    overviewLayout->setSpacing(14);

    QLabel *overviewTitle = new QLabel("主展示区域");
    overviewTitle->setObjectName("mapPlaceholder");
    overviewTitle->setAlignment(Qt::AlignCenter);

    QLabel *overviewBody = new QLabel(
        "1. 监测点总览\n"
        "2. 溯源结果总览\n"
        "3. 预测趋势图\n"
        "4. 告警与任务状态");
    overviewBody->setObjectName("cardSubText");
    overviewBody->setAlignment(Qt::AlignCenter);
    overviewBody->setWordWrap(true);

    overviewLayout->addStretch();
    overviewLayout->addWidget(overviewTitle);
    overviewLayout->addWidget(overviewBody);
    overviewLayout->addStretch();

    heroLayout->addWidget(noticeBar);
    heroLayout->addWidget(overviewArea, 1);

    QFrame *taskCard = new QFrame;
    taskCard->setObjectName("infoCard");

    QVBoxLayout *taskLayout = new QVBoxLayout(taskCard);
    taskLayout->setContentsMargins(16, 12, 16, 12);
    taskLayout->setSpacing(8);

    QLabel *taskTitle = new QLabel("系统进度");
    taskTitle->setObjectName("cardTitle");

    QLabel *taskDesc = new QLabel("数据概览、污染溯源与趋势预测接口代码已完成；待在 Linux 导出并部署两类 ONNX 模型，完成实机联调和日志持久化。");
    taskDesc->setObjectName("cardSubText");
    taskDesc->setWordWrap(true);

    QProgressBar *progressBar = new QProgressBar;
    progressBar->setObjectName("taskProgress");
    progressBar->setRange(0, 100);
    progressBar->setValue(55);
    progressBar->setTextVisible(false);
    progressBar->setFixedHeight(10);

    QPushButton *startButton = new QPushButton("继续开发");
    startButton->setObjectName("primaryButton");
    startButton->setCursor(Qt::PointingHandCursor);
    startButton->setFixedHeight(34);

    taskLayout->addWidget(taskTitle);
    taskLayout->addWidget(progressBar);
    taskLayout->addWidget(taskDesc);
    taskLayout->addWidget(startButton);

    QHBoxLayout *bottomCards = new QHBoxLayout;
    bottomCards->setSpacing(12);
    bottomCards->addWidget(createInfoCard("数据管线",
                                          "本地 CSV 导入、预处理结果预览与数据库写回将在数据页面统一管理。",
                                          "查看数据计划"));
    bottomCards->addWidget(createInfoCard("模型服务",
                                          "溯源使用 Random Forest，预测使用120条历史的 Attention-LSTM；需导出模型后进行 Linux 实机验证。",
                                          "查看模型计划"));
    bottomCards->addWidget(createInfoCard("测试与部署",
                                          "接口测试、压测、部署验证可以在功能跑通后逐步补齐。",
                                          "查看清单"));

    layout->addWidget(heroCard, 1);
    layout->addWidget(taskCard);
    layout->addLayout(bottomCards);

    scrollArea->setWidget(content);
    rootLayout->addWidget(scrollArea);
}

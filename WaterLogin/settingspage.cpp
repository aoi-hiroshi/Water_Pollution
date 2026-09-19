#include "settingspage.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
QFrame *createInfoCard(const QString &title,
                       const QString &body,
                       const QString &buttonText = QString())
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
        button->setObjectName("secondaryButton");
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(34);
        layout->addWidget(button);
    }

    return card;
}
}

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
{
    initUI();
}

void SettingsPage::initUI()
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

    QHBoxLayout *topRow = new QHBoxLayout;
    topRow->setSpacing(12);
    topRow->addWidget(createInfoCard("接口地址",
                                     "基础地址：\nhttp://127.0.0.1:8080\n\n可通过 WATER_API_BASE_URL 配置 Muduo 服务地址。",
                                     "编辑地址"));
    topRow->addWidget(createInfoCard("数据库配置",
                                     "本地数据库：\nMySQL / MariaDB\n\n后续保存样本、任务、结果与日志数据。",
                                     "查看表设计"));
    topRow->addWidget(createInfoCard("模型资源",
                                     "分类、溯源、预测模型由 ONNX Runtime 加载，并在这里管理版本信息。",
                                     "打开注册表"));

    QFrame *notesCard = new QFrame;
    notesCard->setObjectName("infoCard");
    QVBoxLayout *notesLayout = new QVBoxLayout(notesCard);
    notesLayout->setContentsMargins(16, 12, 16, 12);
    notesLayout->setSpacing(10);

    QLabel *notesTitle = new QLabel("下一步接线说明");
    notesTitle->setObjectName("cardTitle");

    QLabel *notesBody = new QLabel(
        "1. 数据概览、溯源与预测请求已接到 Muduo API。\n"
        "2. 在 Linux 配置 WATER_TRACE_MODEL_PATH 并加载 ONNX。\n"
        "3. 配置 WATER_FORECAST_MODEL_PATH 加载预测 ONNX。\n"
        "4. 将任务、结果与日志写入数据库。");
    notesBody->setObjectName("cardSubText");
    notesBody->setWordWrap(true);

    QPushButton *saveBtn = new QPushButton("保留此清单");
    saveBtn->setObjectName("primaryButton");
    saveBtn->setFixedHeight(34);

    notesLayout->addWidget(notesTitle);
    notesLayout->addWidget(notesBody);
    notesLayout->addStretch();
    notesLayout->addWidget(saveBtn);

    layout->addLayout(topRow);
    layout->addWidget(notesCard);

    scrollArea->setWidget(content);
    rootLayout->addWidget(scrollArea);
}

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "datapage.h"
#include "applogger.h"
#include "homepage.h"
#include "logpage.h"
#include "predictionpage.h"
#include "settingspage.h"
#include "tracepage.h"

#include <QButtonGroup>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QDebug>

namespace {
struct PageMeta {
    const char *title;
    const char *subtitle;
    const char *status;
};

const PageMeta kPageMeta[] = {
    {"系统首页", "Qt 客户端 | Muduo API | MySQL 数据服务", "系统已就绪 | 首页已加载"},
    {"数据中心", "数据导入、预处理、预览与提交", "数据模块待接入后端接口"},
    {"污染溯源", "读取数据库样本并查看候选污染源概率", "溯源接口已完成 | 需加载 ONNX 模型"},
    {"趋势预测", "读取120条历史并预测四项水质指标", "预测接口已完成 | 需加载 ONNX 模型"},
    {"日志中心", "查询客户端运行日志与 GitHub 开发记录", "本地日志已接入 | GitHub提交可刷新"},
    {"系统设置", "管理接口地址、数据库与模型路径", "设置模块待补充配置读写能力"}
};
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , stackedWidget(nullptr)
    , pageTitleLabel(nullptr)
    , pageSubtitleLabel(nullptr)
    , statusLabel(nullptr)
{
    ui->setupUi(this);
    initUI();

    QFile file(":/mainstyle.qss");
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        setStyleSheet(QString::fromUtf8(file.readAll()));
        file.close();
    } else {
        qDebug() << "Failed to load mainstyle.qss";
        AppLogger::instance().log(
            AppLogType::System, AppLogLevel::Warning,
            QStringLiteral("主窗口"), QStringLiteral("主界面样式表加载失败"),
            QStringLiteral("失败"));
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

QPushButton *MainWindow::createNavButton(const QString &text)
{
    QPushButton *button = new QPushButton(text);
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setObjectName("navButton");
    button->setFixedSize(72, 58);
    return button;
}

void MainWindow::switchPage(int pageIndex)
{
    const int pageCount = static_cast<int>(sizeof(kPageMeta) / sizeof(kPageMeta[0]));
    if (!stackedWidget || pageIndex < 0 || pageIndex >= pageCount) {
        return;
    }

    stackedWidget->setCurrentIndex(pageIndex);
    pageTitleLabel->setText(QString::fromUtf8(kPageMeta[pageIndex].title));
    pageSubtitleLabel->setText(QString::fromUtf8(kPageMeta[pageIndex].subtitle));
    statusLabel->setText(QString::fromUtf8(kPageMeta[pageIndex].status));
}

void MainWindow::initPages()
{
    stackedWidget->addWidget(new HomePage(this));
    stackedWidget->addWidget(new DataPage(this));
    stackedWidget->addWidget(new TracePage(this));
    stackedWidget->addWidget(new PredictionPage(this));
    stackedWidget->addWidget(new LogPage(this));
    stackedWidget->addWidget(new SettingsPage(this));
}

void MainWindow::initUI()
{
    setWindowTitle("水污染溯源与预测系统");
    resize(1400, 900);
    setMinimumSize(1100, 720);

    QWidget *central = new QWidget(this);
    central->setObjectName("centralWidgetRoot");
    setCentralWidget(central);

    QHBoxLayout *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    QFrame *leftNav = new QFrame;
    leftNav->setObjectName("leftNav");
    leftNav->setFixedWidth(96);
    leftNav->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    QVBoxLayout *leftLayout = new QVBoxLayout(leftNav);
    leftLayout->setContentsMargins(12, 14, 12, 14);
    leftLayout->setSpacing(14);

    QLabel *logoLabel = new QLabel("水");
    logoLabel->setObjectName("logoLabel");
    logoLabel->setAlignment(Qt::AlignCenter);
    logoLabel->setFixedSize(48, 48);

    QPushButton *homeBtn = createNavButton("首页");
    QPushButton *dataBtn = createNavButton("数据");
    QPushButton *traceBtn = createNavButton("溯源");
    QPushButton *predictBtn = createNavButton("预测");
    QPushButton *logBtn = createNavButton("日志");
    QPushButton *settingBtn = createNavButton("设置");

    QButtonGroup *navGroup = new QButtonGroup(this);
    navGroup->setExclusive(true);
    navGroup->addButton(homeBtn, 0);
    navGroup->addButton(dataBtn, 1);
    navGroup->addButton(traceBtn, 2);
    navGroup->addButton(predictBtn, 3);
    navGroup->addButton(logBtn, 4);
    navGroup->addButton(settingBtn, 5);
    homeBtn->setChecked(true);

    leftLayout->addWidget(logoLabel, 0, Qt::AlignHCenter);
    leftLayout->addSpacing(8);
    leftLayout->addWidget(homeBtn, 0, Qt::AlignHCenter);
    leftLayout->addWidget(dataBtn, 0, Qt::AlignHCenter);
    leftLayout->addWidget(traceBtn, 0, Qt::AlignHCenter);
    leftLayout->addWidget(predictBtn, 0, Qt::AlignHCenter);
    leftLayout->addWidget(logBtn, 0, Qt::AlignHCenter);
    leftLayout->addStretch();
    leftLayout->addWidget(settingBtn, 0, Qt::AlignHCenter);

    QWidget *mainArea = new QWidget;
    mainArea->setObjectName("mainArea");
    mainArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QVBoxLayout *mainLayout = new QVBoxLayout(mainArea);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    QFrame *topBar = new QFrame;
    topBar->setObjectName("topBar");
    topBar->setFixedHeight(66);

    QHBoxLayout *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(18, 10, 18, 10);
    topLayout->setSpacing(14);

    pageTitleLabel = new QLabel;
    pageTitleLabel->setObjectName("titleLabel");

    pageSubtitleLabel = new QLabel;
    pageSubtitleLabel->setObjectName("subInfoLabel");

    statusLabel = new QLabel;
    statusLabel->setObjectName("statusLabel");
    statusLabel->setFixedHeight(32);

    topLayout->addWidget(pageTitleLabel);
    topLayout->addSpacing(18);
    topLayout->addWidget(pageSubtitleLabel);
    topLayout->addStretch();
    topLayout->addWidget(statusLabel);

    stackedWidget = new QStackedWidget;
    stackedWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    initPages();

    mainLayout->addWidget(topBar);
    mainLayout->addWidget(stackedWidget, 1);

    rootLayout->addWidget(leftNav);
    rootLayout->addWidget(mainArea, 1);

    connect(navGroup, QOverload<int>::of(&QButtonGroup::buttonClicked), this, [this](int id) {
        switchPage(id);
    });

    switchPage(0);
}

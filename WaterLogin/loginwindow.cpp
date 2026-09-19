#include "loginwindow.h"
#include "mainwindow.h"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

LoginWindow::LoginWindow(QWidget *parent)
    : QWidget(parent)
{
    initUI();
    setQssStyle();
}

void LoginWindow::initUI()
{
    setWindowTitle("水污染溯源与预测系统 - 登录");
    resize(900, 600);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setAlignment(Qt::AlignCenter);

    cardFrame = new QFrame(this);
    cardFrame->setObjectName("cardFrame");
    cardFrame->setFixedSize(420, 430);

    QVBoxLayout *cardLayout = new QVBoxLayout(cardFrame);
    cardLayout->setContentsMargins(40, 35, 40, 35);
    cardLayout->setSpacing(18);

    titleLabel = new QLabel("水污染溯源与预测系统", cardFrame);
    titleLabel->setObjectName("titleLabel");
    titleLabel->setAlignment(Qt::AlignCenter);

    subTitleLabel = new QLabel("请输入账号密码后进入系统", cardFrame);
    subTitleLabel->setObjectName("subTitleLabel");
    subTitleLabel->setAlignment(Qt::AlignCenter);

    userIconLabel = new QLabel("账号", cardFrame);
    userIconLabel->setObjectName("iconLabel");
    userIconLabel->setFixedWidth(36);
    userIconLabel->setAlignment(Qt::AlignCenter);

    userEdit = new QLineEdit(cardFrame);
    userEdit->setPlaceholderText("请输入账号");

    QHBoxLayout *userLayout = new QHBoxLayout;
    userLayout->setSpacing(10);
    userLayout->addWidget(userIconLabel);
    userLayout->addWidget(userEdit);

    pwdIconLabel = new QLabel("密码", cardFrame);
    pwdIconLabel->setObjectName("iconLabel");
    pwdIconLabel->setFixedWidth(36);
    pwdIconLabel->setAlignment(Qt::AlignCenter);

    pwdEdit = new QLineEdit(cardFrame);
    pwdEdit->setPlaceholderText("请输入密码");
    pwdEdit->setEchoMode(QLineEdit::Password);

    QHBoxLayout *pwdLayout = new QHBoxLayout;
    pwdLayout->setSpacing(10);
    pwdLayout->addWidget(pwdIconLabel);
    pwdLayout->addWidget(pwdEdit);

    loginBtn = new QPushButton("登录系统", cardFrame);
    loginBtn->setCursor(Qt::PointingHandCursor);

    exitBtn = new QPushButton("退出", cardFrame);
    exitBtn->setObjectName("exitBtn");
    exitBtn->setCursor(Qt::PointingHandCursor);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->setSpacing(15);
    btnLayout->addWidget(loginBtn);
    btnLayout->addWidget(exitBtn);

    tipLabel = new QLabel("默认账号：admin  默认密码：123456", cardFrame);
    tipLabel->setObjectName("tipLabel");
    tipLabel->setAlignment(Qt::AlignCenter);

    cardLayout->addStretch();
    cardLayout->addWidget(titleLabel);
    cardLayout->addWidget(subTitleLabel);
    cardLayout->addSpacing(15);
    cardLayout->addLayout(userLayout);
    cardLayout->addLayout(pwdLayout);
    cardLayout->addSpacing(10);
    cardLayout->addLayout(btnLayout);
    cardLayout->addSpacing(8);
    cardLayout->addWidget(tipLabel);
    cardLayout->addStretch();

    mainLayout->addWidget(cardFrame, 0, Qt::AlignCenter);

    connect(loginBtn, &QPushButton::clicked, this, &LoginWindow::onLoginClicked);
    connect(exitBtn, &QPushButton::clicked, this, &LoginWindow::onExitClicked);
}

void LoginWindow::setQssStyle()
{
    QFile file(":/loginstyle.qss");
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        const QString style = QString::fromUtf8(file.readAll());
        setStyleSheet(style);
        file.close();
    } else {
        qDebug() << "Failed to load loginstyle.qss";
    }
}

void LoginWindow::onLoginClicked()
{
    const QString username = userEdit->text().trimmed();
    const QString password = pwdEdit->text().trimmed();

    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, "提示", "账号或密码不能为空。");
        return;
    }

    if (username == "admin" && password == "123456") {
        MainWindow *mainWindow = new MainWindow;
        mainWindow->show();
        close();
    } else {
        QMessageBox::critical(this, "登录失败", "账号或密码错误。");
    }
}

void LoginWindow::onExitClicked()
{
    QApplication::quit();
}

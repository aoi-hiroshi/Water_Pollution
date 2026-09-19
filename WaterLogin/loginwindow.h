#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H
#include <QFile>
#include <QDebug>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QFrame;

class LoginWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LoginWindow(QWidget *parent = nullptr);

private slots:
    void onLoginClicked();
    void onExitClicked();

private:
    void initUI();
    void setQssStyle();

private:
    QFrame *cardFrame;

    QLabel *titleLabel;
    QLabel *subTitleLabel;
    QLabel *userIconLabel;
    QLabel *pwdIconLabel;
    QLabel *tipLabel;

    QLineEdit *userEdit;
    QLineEdit *pwdEdit;

    QPushButton *loginBtn;
    QPushButton *exitBtn;
};

#endif // LOGINWINDOW_H

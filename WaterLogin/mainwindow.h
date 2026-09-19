#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>

class QLabel;
class QPushButton;
class QStackedWidget;
class QWidget;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    void initUI();
    void initPages();
    QPushButton *createNavButton(const QString &text);
    void switchPage(int pageIndex);

private:
    Ui::MainWindow *ui;
    QStackedWidget *stackedWidget;
    QLabel *pageTitleLabel;
    QLabel *pageSubtitleLabel;
    QLabel *statusLabel;
};

#endif // MAINWINDOW_H

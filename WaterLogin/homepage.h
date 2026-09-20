#ifndef HOMEPAGE_H
#define HOMEPAGE_H

#include <QWidget>

class QLabel;
class QStackedWidget;
class QTimer;

class HomePage : public QWidget
{
    Q_OBJECT

public:
    explicit HomePage(QWidget *parent = nullptr);

private:
    void initUI();
    void showSlide(int index);

    QStackedWidget *slideStack = nullptr;
    QLabel *slideIndicator = nullptr;
    QLabel *slideCounter = nullptr;
    QTimer *slideTimer = nullptr;
    int currentSlide = 0;
};

#endif // HOMEPAGE_H

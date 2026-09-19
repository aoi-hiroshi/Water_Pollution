#ifndef LOGPAGE_H
#define LOGPAGE_H

#include <QWidget>

class LogPage : public QWidget
{
    Q_OBJECT

public:
    explicit LogPage(QWidget *parent = nullptr);

private:
    void initUI();
};

#endif // LOGPAGE_H

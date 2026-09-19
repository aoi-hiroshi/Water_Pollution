#ifndef TRACEPAGE_H
#define TRACEPAGE_H

#include <QJsonObject>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class TraceService;
class QSpinBox;

class TracePage : public QWidget
{
    Q_OBJECT

public:
    explicit TracePage(QWidget *parent = nullptr);

private slots:
    void startTrace();
    void handleTraceReady(const QJsonObject &result);
    void handleServiceError(const QString &message);
    void exportReport();

private:
    void initUI();
    void setBusy(bool busy);

private:
    TraceService *traceService;
    QComboBox *datasetComboBox;
    QLineEdit *sampleInput;
    QSpinBox *cleaningRunInput = nullptr;
    QPushButton *startTraceButton;
    QPushButton *exportButton;
    QLabel *resultBody;
    QLabel *statusLabel;
    QTableWidget *rankingTable;
    QJsonObject lastResult;
};

#endif // TRACEPAGE_H

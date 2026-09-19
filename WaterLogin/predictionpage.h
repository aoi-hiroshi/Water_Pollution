#ifndef PREDICTIONPAGE_H
#define PREDICTIONPAGE_H

#include <QWidget>
#include <QJsonObject>

class QComboBox;
class QLineEdit;
class QSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;
class ForecastChart;
class PredictionService;
class DataService;

class PredictionPage : public QWidget
{
    Q_OBJECT

public:
    explicit PredictionPage(QWidget *parent = nullptr);

private:
    void refreshResources();
    void startForecast();
    void showResult(const QJsonObject &result);
    void exportResult();
    void updateControls();
    PredictionService *predictionService;
    DataService *dataService;
    QComboBox *companyBox, *datasetBox, *featureBox;
    QLineEdit *endSampleEdit;
    QSpinBox *horizonSpin;
    QLabel *modelLabel, *resultLabel;
    QPushButton *predictButton, *refreshButton, *exportButton;
    QTableWidget *resultTable;
    ForecastChart *chart;
    QJsonObject lastResult;
    bool modelAvailable = false;
    bool loading = false;
    bool modelPending = false;
    bool companiesPending = false;
};

#endif // PREDICTIONPAGE_H

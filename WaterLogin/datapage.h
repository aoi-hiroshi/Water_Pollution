#ifndef DATAPAGE_H
#define DATAPAGE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QPixmap>
#include <QString>
#include <QWidget>

class DataService;
class QComboBox;
class QLabel;
class QPushButton;
class QResizeEvent;
class QStackedWidget;
class QTableWidget;
class QSpinBox;
class QDoubleSpinBox;

class DataPage : public QWidget
{
    Q_OBJECT

public:
    explicit DataPage(QWidget *parent = nullptr);

private slots:
    void loadCompanyList();
    void loadOverviewData();
    void handleCompanyListReady(const QJsonArray &companies);
    void handleOverviewReady(const QJsonObject &overview);
    void handleClassificationReady(const QJsonObject &result);
    void handleForecastReady(const QJsonObject &result);
    void handleServiceError(const QString &message);

private:
    void initUI();
    QWidget *createFunctionCard(const QString &title,
                                const QString &description,
                                const QString &buttonText,
                                QWidget *extraWidget = nullptr,
                                QPushButton **actionButton = nullptr);
    QWidget *createTraceModeView();
    QWidget *createPredictionModeView();
    void updateModeDescription(int modeIndex);
    void populatePreviewTable(const QJsonArray &previewRows);
    void renderOverviewSummary(const QJsonObject &overview);
    void renderOverviewChart(
        const QJsonArray &previewRows,
        const QString &title = QStringLiteral("十项水质指标预览趋势"),
        const QString &seriesLabel = QStringLiteral("原始数据"));
    void updateChartPixmap();
    void runClassification(QString operation, bool persist = false);
    void runForecastAnalysis(const QString &operation);
    void configureModeSelectors(int modeIndex);
    void setBusy(bool busy);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    DataService *dataService;
    QComboBox *companyComboBox;
    QComboBox *datasetComboBox;
    QComboBox *taskModeComboBox;
    QComboBox *distributionTypeComboBox;
    QComboBox *distributionFeatureComboBox;
    QComboBox *correlationMethodComboBox;
    QComboBox *sequenceFeatureComboBox;
    QComboBox *sequenceWindowComboBox;
    QComboBox *forecastFeatureComboBox;
    QComboBox *forecastStepComboBox;
    QComboBox *forecastCheckComboBox;
    QTableWidget *previewTable;
    QLabel *modeDescriptionLabel;
    QLabel *chartPlaceholderLabel;
    QLabel *resultSummaryLabel;
    QStackedWidget *modeStackedWidget;
    QPushButton *refreshButton;
    QPushButton *overviewButton;
    QPushButton *missingButton = nullptr;
    QPushButton *cleanButton = nullptr;
    QPushButton *distributionButton = nullptr;
    QPushButton *correlationButton = nullptr;
    QPushButton *sequenceButton = nullptr;
    QPushButton *forecastMissingButton = nullptr;
    QPushButton *smoothButton = nullptr;
    QPushButton *windowPreviewButton = nullptr;
    QPushButton *diagnosisButton = nullptr;
    QPushButton *saveVersionButton = nullptr;
    QPushButton *saveChartButton = nullptr;
    QSpinBox *cleaningRunInput = nullptr;
    QSpinBox *windowInput = nullptr;
    QDoubleSpinBox *thresholdInput = nullptr;
    QJsonObject previewRequest;
    QJsonArray companies;
    QString previewOperation;
    bool busy = false;
    QPixmap currentChartPixmap;
};

#endif // DATAPAGE_H

#include "tracepage.h"

#include "applogger.h"
#include "traceservice.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QScrollArea>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>

#include <climits>

namespace {
QString csvValue(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"") + value + QStringLiteral("\"");
}
}

TracePage::TracePage(QWidget *parent)
    : QWidget(parent)
    , traceService(new TraceService(this))
    , datasetComboBox(nullptr)
    , sampleInput(nullptr)
    , startTraceButton(nullptr)
    , exportButton(nullptr)
    , resultBody(nullptr)
    , statusLabel(nullptr)
    , rankingTable(nullptr)
{
    initUI();

    connect(traceService, &TraceService::traceReady,
            this, &TracePage::handleTraceReady);
    connect(traceService, &TraceService::serviceError,
            this, &TracePage::handleServiceError);
}

void TracePage::initUI()
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

    QFrame *controlCard = new QFrame;
    controlCard->setObjectName("infoCard");
    QVBoxLayout *controlLayout = new QVBoxLayout(controlCard);
    controlLayout->setContentsMargins(16, 12, 16, 12);
    controlLayout->setSpacing(10);

    QLabel *controlTitle = new QLabel(QStringLiteral("溯源输入"));
    controlTitle->setObjectName("cardTitle");

    QLabel *controlDescription = new QLabel(
        QStringLiteral("从数据库读取指定样本的十项水质特征，并提交给 Random Forest ONNX 模型。"));
    controlDescription->setObjectName("cardSubText");
    controlDescription->setWordWrap(true);

    QFormLayout *formLayout = new QFormLayout;
    formLayout->setHorizontalSpacing(12);
    formLayout->setVerticalSpacing(10);

    datasetComboBox = new QComboBox;
    datasetComboBox->addItem(QStringLiteral("测试数据 test_data"),
                             QStringLiteral("test_data"));
    datasetComboBox->addItem(QStringLiteral("训练数据 train_data"),
                             QStringLiteral("train_data"));

    sampleInput = new QLineEdit;
    sampleInput->setPlaceholderText(QStringLiteral("输入数据库样本 ID，例如 1"));
    sampleInput->setText(QStringLiteral("1"));
    sampleInput->setValidator(new QIntValidator(1, INT_MAX, sampleInput));

    formLayout->addRow(QStringLiteral("数据集"), datasetComboBox);
    formLayout->addRow(QStringLiteral("样本 ID"), sampleInput);
    cleaningRunInput = new QSpinBox;
    cleaningRunInput->setRange(0, INT_MAX);
    cleaningRunInput->setSpecialValueText(QStringLiteral("原始数据（0）"));
    cleaningRunInput->setToolTip(QStringLiteral("填写数据呈现页面保存的版本编号；样本ID仍是原始表中的ID。"));
    formLayout->addRow(QStringLiteral("清洗版本"), cleaningRunInput);

    startTraceButton = new QPushButton(QStringLiteral("发起溯源"));
    startTraceButton->setObjectName("primaryButton");
    startTraceButton->setFixedHeight(34);

    exportButton = new QPushButton(QStringLiteral("导出结果"));
    exportButton->setObjectName("secondaryButton");
    exportButton->setFixedHeight(34);
    exportButton->setEnabled(false);

    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(startTraceButton);
    buttonLayout->addWidget(exportButton);

    controlLayout->addWidget(controlTitle);
    controlLayout->addWidget(controlDescription);
    controlLayout->addLayout(formLayout);
    controlLayout->addStretch();
    controlLayout->addLayout(buttonLayout);

    QFrame *resultCard = new QFrame;
    resultCard->setObjectName("displayCard");
    QVBoxLayout *resultLayout = new QVBoxLayout(resultCard);
    resultLayout->setContentsMargins(24, 24, 24, 24);
    resultLayout->setSpacing(12);

    QLabel *resultTitle = new QLabel(QStringLiteral("溯源结果概览"));
    resultTitle->setObjectName("cardTitle");

    resultBody = new QLabel(
        QStringLiteral("尚未执行溯源。\n请选择数据集并输入有效样本 ID。"));
    resultBody->setObjectName("cardSubText");
    resultBody->setWordWrap(true);
    resultBody->setTextInteractionFlags(Qt::TextSelectableByMouse);

    statusLabel = new QLabel(
        QStringLiteral("结果来自数据库样本与 ONNX 模型，不再使用静态演示概率。"));
    statusLabel->setObjectName("cardSubText");
    statusLabel->setWordWrap(true);

    resultLayout->addWidget(resultTitle);
    resultLayout->addWidget(resultBody);
    resultLayout->addStretch();
    resultLayout->addWidget(statusLabel);

    topRow->addWidget(controlCard, 1);
    topRow->addWidget(resultCard, 2);

    QFrame *rankingCard = new QFrame;
    rankingCard->setObjectName("infoCard");
    QVBoxLayout *rankingLayout = new QVBoxLayout(rankingCard);
    rankingLayout->setContentsMargins(16, 12, 16, 12);
    rankingLayout->setSpacing(10);

    QLabel *rankingTitle = new QLabel(QStringLiteral("候选污染源排序"));
    rankingTitle->setObjectName("cardTitle");

    rankingTable = new QTableWidget(0, 5);
    rankingTable->setHorizontalHeaderLabels(
        QStringList() << QStringLiteral("排名")
                      << QStringLiteral("公司 ID")
                      << QStringLiteral("污染源")
                      << QStringLiteral("公司编码")
                      << QStringLiteral("概率"));
    rankingTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    rankingTable->verticalHeader()->setVisible(false);
    rankingTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rankingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    rankingTable->setAlternatingRowColors(true);
    rankingTable->setMinimumHeight(260);

    rankingLayout->addWidget(rankingTitle);
    rankingLayout->addWidget(rankingTable);

    layout->addLayout(topRow);
    layout->addWidget(rankingCard);

    scrollArea->setWidget(content);
    rootLayout->addWidget(scrollArea);

    connect(startTraceButton, &QPushButton::clicked,
            this, &TracePage::startTrace);
    connect(exportButton, &QPushButton::clicked,
            this, &TracePage::exportReport);
    connect(sampleInput, &QLineEdit::returnPressed,
            this, &TracePage::startTrace);
}

void TracePage::setBusy(bool busy)
{
    startTraceButton->setEnabled(!busy);
    datasetComboBox->setEnabled(!busy);
    sampleInput->setEnabled(!busy);
    cleaningRunInput->setEnabled(!busy);
    startTraceButton->setText(busy
        ? QStringLiteral("正在溯源...") : QStringLiteral("发起溯源"));
}

void TracePage::startTrace()
{
    bool valid = false;
    const qint64 sampleId = sampleInput->text().toLongLong(&valid);
    if (!valid || sampleId <= 0) {
        QMessageBox::warning(this, QStringLiteral("溯源输入"),
                             QStringLiteral("请输入正整数样本 ID。"));
        return;
    }

    lastResult = QJsonObject();
    rankingTable->setRowCount(0);
    exportButton->setEnabled(false);
    resultBody->setText(QStringLiteral("正在读取样本并执行模型推理..."));
    statusLabel->setText(QStringLiteral("请求已提交，请稍候。"));
    setBusy(true);

    AppLogger::instance().log(
        AppLogType::Task, AppLogLevel::Info,
        QStringLiteral("污染溯源"),
        QStringLiteral("提交样本 %1，数据集 %2，清洗版本 %3")
            .arg(sampleId)
            .arg(datasetComboBox->currentData().toString())
            .arg(cleaningRunInput->value()),
        QStringLiteral("执行中"));

    traceService->classifySample(
        sampleId, datasetComboBox->currentData().toString(), cleaningRunInput->value());
}

void TracePage::handleTraceReady(const QJsonObject &result)
{
    setBusy(false);
    lastResult = result;

    const QJsonObject predicted =
        result.value(QStringLiteral("predicted")).toObject();
    const QJsonObject model = result.value(QStringLiteral("model")).toObject();
    statusLabel->setText(QStringLiteral("使用清洗版本 %1（0为原始数据）；结果是候选污染源概率，不是因果证明。")
        .arg(result.value("cleaning_run_id").toDouble(),0,'f',0));
    const double probability =
        predicted.value(QStringLiteral("probability")).toDouble();

    resultBody->setText(
        QStringLiteral("最可能污染源：%1\n"
                       "公司 ID：%2\n"
                       "置信度：%3%\n"
                       "样本：%4 / %5\n"
                       "模型：%6（版本 %7）")
            .arg(predicted.value(QStringLiteral("company_name")).toString())
            .arg(predicted.value(QStringLiteral("company_id")).toInt())
            .arg(probability * 100.0, 0, 'f', 2)
            .arg(result.value(QStringLiteral("dataset")).toString())
            .arg(result.value(QStringLiteral("sample_id")).toInt())
            .arg(model.value(QStringLiteral("name")).toString())
            .arg(model.value(QStringLiteral("version")).toString()));

    const QJsonArray candidates =
        result.value(QStringLiteral("candidates")).toArray();
    rankingTable->setRowCount(candidates.size());
    for (int row = 0; row < candidates.size(); ++row) {
        const QJsonObject candidate = candidates.at(row).toObject();
        const QStringList values = {
            QString::number(candidate.value(QStringLiteral("rank")).toInt()),
            QString::number(candidate.value(QStringLiteral("company_id")).toInt()),
            candidate.value(QStringLiteral("company_name")).toString(),
            candidate.value(QStringLiteral("company_code")).toString(),
            QStringLiteral("%1%").arg(
                candidate.value(QStringLiteral("probability")).toDouble()
                    * 100.0, 0, 'f', 2)
        };
        for (int column = 0; column < values.size(); ++column) {
            rankingTable->setItem(
                row, column, new QTableWidgetItem(values.at(column)));
        }
    }

    exportButton->setEnabled(true);
    AppLogger::instance().log(
        AppLogType::Task, AppLogLevel::Info,
        QStringLiteral("污染溯源"),
        QStringLiteral("样本 %1 溯源完成，预测公司 %2，置信度 %3%")
            .arg(result.value(QStringLiteral("sample_id")).toVariant().toLongLong())
            .arg(predicted.value(QStringLiteral("company_name")).toString())
            .arg(probability * 100.0, 0, 'f', 2));
}

void TracePage::handleServiceError(const QString &message)
{
    setBusy(false);
    lastResult = QJsonObject();
    exportButton->setEnabled(false);
    resultBody->setText(QStringLiteral("溯源失败\n%1").arg(message));
    statusLabel->setText(
        QStringLiteral("请检查样本 ID、Muduo 服务、数据库和 ONNX 模型配置。"));
    AppLogger::instance().log(
        AppLogType::Task, AppLogLevel::Error,
        QStringLiteral("污染溯源"), message, QStringLiteral("失败"));
    QMessageBox::warning(this, QStringLiteral("污染溯源"), message);
}

void TracePage::exportReport()
{
    if (lastResult.isEmpty()) {
        return;
    }

    const QString defaultName = QStringLiteral("trace_%1_%2.csv")
        .arg(lastResult.value(QStringLiteral("dataset")).toString())
        .arg(lastResult.value(QStringLiteral("sample_id")).toInt());
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出溯源结果"), defaultName,
        QStringLiteral("CSV 文件 (*.csv)"));
    if (path.isEmpty()) {
        return;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, QStringLiteral("导出失败"),
                              QStringLiteral("无法创建结果文件。"));
        return;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream.setGenerateByteOrderMark(true);
    stream << QStringLiteral("数据集,样本ID,清洗版本,模型版本,排名,公司ID,污染源,公司编码,概率\n");

    const QJsonArray candidates =
        lastResult.value(QStringLiteral("candidates")).toArray();
    for (const QJsonValue &value : candidates) {
        const QJsonObject candidate = value.toObject();
        stream << csvValue(lastResult.value("dataset").toString()) << ','
               << QString::number(lastResult.value("sample_id").toDouble(),'f',0) << ','
               << QString::number(lastResult.value("cleaning_run_id").toDouble(),'f',0) << ','
               << csvValue(lastResult.value("model").toObject().value("version").toString()) << ','
               << candidate.value(QStringLiteral("rank")).toInt() << ','
               << candidate.value(QStringLiteral("company_id")).toInt() << ','
               << csvValue(candidate.value(QStringLiteral("company_name")).toString()) << ','
               << csvValue(candidate.value(QStringLiteral("company_code")).toString()) << ','
               << QString::number(
                      candidate.value(QStringLiteral("probability")).toDouble(),
                      'f', 8)
               << '\n';
    }

    if (!file.commit()) {
        QMessageBox::critical(this, QStringLiteral("导出失败"),
                              QStringLiteral("写入结果文件失败。"));
        return;
    }
    QMessageBox::information(this, QStringLiteral("导出完成"),
                             QStringLiteral("溯源候选结果已保存。"));
}

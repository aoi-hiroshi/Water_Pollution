#include "predictionpage.h"
#include "applogger.h"
#include "predictionservice.h"
#include "dataservice.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QIntValidator>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
const QStringList kFields = QStringList() << "cod" << "nh3n" << "tp" << "turbidity";
const QStringList kLabels = QStringList() << "COD (mg/L)" << "氨氮 (mg/L)" << "总磷 (mg/L)" << "浊度";
bool validPoints(const QJsonArray &points, int expectedCount)
{
    if (points.size() != expectedCount) return false;
    for (int index=0; index<points.size(); ++index) {
        if (!points[index].isObject()) return false;
        const QJsonObject point=points[index].toObject();
        if (point.value("step").toInt() != index+1) return false;
        for (const QString &field : kFields) {
            const QJsonValue value=point.value(field);
            if (!value.isDouble() || !std::isfinite(value.toDouble())) return false;
        }
    }
    return true;
}
QString csvCell(QString text)
{
    text.replace('"', "\"\"");
    return '"' + text + '"';
}
}

// Draw numeric API results directly, without a Qt Charts dependency.
class ForecastChart : public QWidget
{
public:
    explicit ForecastChart(QWidget *parent=nullptr) : QWidget(parent) { setMinimumHeight(280); }
    void setResult(const QJsonObject &value) { result=value; update(); }
    void setField(const QString &value) { field=value; update(); }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(),Qt::white);
        const QJsonArray history=result.value("history").toArray(), predictions=result.value("predictions").toArray();
        if (history.isEmpty() || predictions.isEmpty()) {
            painter.drawText(rect(),Qt::AlignCenter,"预测成功后显示历史值（蓝）和预测值（橙）"); return;
        }
        const QRectF area(68,32,qMax(1,width()-100),qMax(1,height()-82));
        double minimum=std::numeric_limits<double>::max(), maximum=std::numeric_limits<double>::lowest();
        for (const QJsonArray &series : {history,predictions}) {
            for (const QJsonValue &value : series) {
                const double y=value.toObject().value(field).toDouble();
                minimum=std::min(minimum,y); maximum=std::max(maximum,y);
            }
        }
        const double padding=std::max((maximum-minimum)*0.1,0.01);
        minimum-=padding; maximum+=padding;
        const int total=history.size()+predictions.size();
        const auto position=[&](int index,double value) {
            return QPointF(area.left()+area.width()*index/(total-1),
                           area.bottom()-area.height()*(value-minimum)/(maximum-minimum));
        };
        painter.setPen(QColor("#dce3eb"));
        for (int tick=0; tick<=4; ++tick) {
            const double y=area.top()+area.height()*tick/4.0;
            painter.drawLine(QPointF(area.left(),y),QPointF(area.right(),y));
            painter.drawText(QRectF(0,y-10,62,20),Qt::AlignRight|Qt::AlignVCenter,
                             QString::number(maximum-(maximum-minimum)*tick/4.0,'g',5));
        }
        painter.setPen(QColor("#475569"));
        painter.drawText(QRectF(area.left(),4,area.width(),24),"蓝色：历史值    橙色：未来预测（尚无真实值，不计算在线误差）");
        painter.drawText(QRectF(area.left(),area.bottom()+8,area.width(),24),Qt::AlignCenter,
                         QString("按采样顺序：历史 %1 条 → 未来 %2 步（非小时）").arg(history.size()).arg(predictions.size()));
        const auto drawSeries=[&](const QJsonArray &series,int offset,QColor color,bool connectHistory) {
            QPainterPath path;
            if (connectHistory) path.moveTo(position(history.size()-1,history.last().toObject().value(field).toDouble()));
            for (int index=0; index<series.size(); ++index) {
                const QPointF point=position(index+offset,series[index].toObject().value(field).toDouble());
                if (index==0 && !connectHistory) path.moveTo(point); else path.lineTo(point);
            }
            painter.setPen(QPen(color,2)); painter.drawPath(path);
        };
        drawSeries(history,0,QColor("#2563eb"),false);
        drawSeries(predictions,history.size(),QColor("#ea580c"),true);
        painter.setPen(QPen(QColor("#94a3b8"),1,Qt::DashLine));
        const double boundary=position(history.size()-1,minimum).x();
        painter.drawLine(QPointF(boundary,area.top()),QPointF(boundary,area.bottom()));
    }
private:
    QJsonObject result;
    QString field="cod";
};

PredictionPage::PredictionPage(QWidget *parent) : QWidget(parent)
{
    predictionService=new PredictionService(this);
    dataService=new DataService(this);
    QVBoxLayout *layout=new QVBoxLayout(this);
    modelLabel=new QLabel("正在读取预测模型与公司列表…");
    modelLabel->setWordWrap(true); layout->addWidget(modelLabel);
    QHBoxLayout *controls=new QHBoxLayout;
    companyBox=new QComboBox; datasetBox=new QComboBox;
    datasetBox->addItem("测试数据","test_data"); datasetBox->addItem("训练数据","train_data");
    endSampleEdit=new QLineEdit("0");
    endSampleEdit->setValidator(new QIntValidator(0,std::numeric_limits<int>::max(),endSampleEdit));
    endSampleEdit->setMaximumWidth(110);
    endSampleEdit->setToolTip("0：使用最新120条；其他：截至该样本ID，必须属于所选公司");
    horizonSpin=new QSpinBox; horizonSpin->setRange(1,10); horizonSpin->setValue(10); horizonSpin->setSuffix(" 步");
    predictButton=new QPushButton("开始预测"); predictButton->setObjectName("primaryButton");
    refreshButton=new QPushButton("刷新资源");
    controls->addWidget(new QLabel("公司")); controls->addWidget(companyBox,1); controls->addWidget(datasetBox);
    controls->addWidget(new QLabel("截止ID")); controls->addWidget(endSampleEdit); controls->addWidget(horizonSpin);
    controls->addWidget(predictButton); controls->addWidget(refreshButton); layout->addLayout(controls);
    QLabel *notice=new QLabel("输入必须是同一公司、按采样顺序导入的120条清洗后数据。0表示最新窗口；标准化由ONNX模型完成。");
    notice->setWordWrap(true); layout->addWidget(notice);
    featureBox=new QComboBox; featureBox->addItems(kLabels); layout->addWidget(featureBox);
    chart=new ForecastChart(this); layout->addWidget(chart,1);
    resultLabel=new QLabel("尚未预测。未来真实值未到达，不显示虚构的 MAE、RMSE 或 R²。");
    resultLabel->setWordWrap(true); layout->addWidget(resultLabel);
    resultTable=new QTableWidget(0,5);
    resultTable->setHorizontalHeaderLabels(QStringList() << "未来步数" << kLabels);
    resultTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers); layout->addWidget(resultTable);
    exportButton=new QPushButton("导出预测CSV"); layout->addWidget(exportButton);
    connect(predictButton,&QPushButton::clicked,this,&PredictionPage::startForecast);
    connect(refreshButton,&QPushButton::clicked,this,&PredictionPage::refreshResources);
    connect(exportButton,&QPushButton::clicked,this,&PredictionPage::exportResult);
    connect(featureBox,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int index) { chart->setField(kFields[index]); });
    connect(companyBox,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int) { updateControls(); });
    connect(predictionService,&PredictionService::forecastReady,this,&PredictionPage::showResult);
    connect(predictionService,&PredictionService::modelReady,this,[this](const QJsonObject &model) {
        modelPending=false;
        const int maximum=model.value("max_horizon").toInt();
        modelAvailable=model.value("available").toBool() && model.value("lookback").toInt()==120 && maximum==10;
        modelLabel->setText(QString("模型：%1 / %2 | 历史%3条 → 最多%4步 | %5")
            .arg(model.value("name").toString(),model.value("version").toString())
            .arg(model.value("lookback").toInt()).arg(maximum)
            .arg(modelAvailable ? "已加载" : "未加载或不兼容，请配置 WATER_FORECAST_MODEL_PATH"));
        updateControls();
    });
    connect(predictionService,&PredictionService::serviceError,this,[this](const QString &key,const QString &message) {
        if (key=="forecast") { loading=false; resultLabel->setText("预测失败："+message); }
        else { modelPending=false; modelAvailable=false; modelLabel->setText("模型信息读取失败："+message); }
        AppLogger::instance().log(
            key=="forecast" ? AppLogType::Task : AppLogType::Interface,
            AppLogLevel::Error, QStringLiteral("趋势预测"), message,
            QStringLiteral("失败"));
        updateControls();
    });
    connect(dataService,&DataService::companyListReady,this,[this](const QJsonArray &companies) {
        companiesPending=false; companyBox->clear();
        for (const QJsonValue &item : companies) {
            const QJsonObject company=item.toObject();
            companyBox->addItem(company.value("company_name").toString(),company.value("company_id").toVariant());
        }
        if (companyBox->count()==0) resultLabel->setText("公司列表为空，请先准备预测公司的时序数据。");
        updateControls();
    });
    connect(dataService,&DataService::serviceError,this,[this](const QString &message) {
        companiesPending=false; companyBox->clear(); resultLabel->setText("公司列表读取失败："+message); updateControls();
    });
    updateControls(); QTimer::singleShot(0,this,&PredictionPage::refreshResources);
}

void PredictionPage::refreshResources()
{
    if (loading || modelPending || companiesPending) return;
    modelPending=companiesPending=true; modelAvailable=false; updateControls();
    predictionService->fetchModel(); dataService->fetchCompanyList();
}

void PredictionPage::updateControls()
{
    const bool idle=!loading && !modelPending && !companiesPending;
    predictButton->setEnabled(idle && modelAvailable && companyBox->count()>0);
    refreshButton->setEnabled(idle); companyBox->setEnabled(idle); datasetBox->setEnabled(idle);
    endSampleEdit->setEnabled(idle); horizonSpin->setEnabled(idle);
    exportButton->setEnabled(idle && !lastResult.isEmpty());
    predictButton->setText(loading ? "正在预测…" : "开始预测");
}

void PredictionPage::startForecast()
{
    bool valid=false;
    const qint64 end=endSampleEdit->text().toLongLong(&valid);
    if (!valid || end<0) { QMessageBox::warning(this,"输入错误","截止ID必须为非负整数，0表示最新窗口。"); return; }
    loading=true; lastResult=QJsonObject(); chart->setResult(lastResult); resultTable->setRowCount(0);
    resultLabel->setText("正在读取历史窗口并执行ONNX推理…"); updateControls();
    AppLogger::instance().log(
        AppLogType::Task, AppLogLevel::Info,
        QStringLiteral("趋势预测"),
        QStringLiteral("提交公司 %1，数据集 %2，截止ID %3，预测 %4 步")
            .arg(companyBox->currentData().toLongLong())
            .arg(datasetBox->currentData().toString())
            .arg(end)
            .arg(horizonSpin->value()),
        QStringLiteral("执行中"));
    predictionService->forecast(companyBox->currentData().toLongLong(),datasetBox->currentData().toString(),end,horizonSpin->value());
}

void PredictionPage::showResult(const QJsonObject &result)
{
    loading=false;
    const QJsonArray history=result.value("history").toArray(), predictions=result.value("predictions").toArray();
    const int horizon=result.value("horizon").toInt();
    if (result.value("lookback").toInt()!=120 || horizon<1 || horizon>10 ||
        !validPoints(history,120) || !validPoints(predictions,horizon)) {
        AppLogger::instance().log(
            AppLogType::Task, AppLogLevel::Error,
            QStringLiteral("趋势预测"),
            QStringLiteral("预测响应数据不完整或存在非法数值"),
            QStringLiteral("失败"));
        resultLabel->setText("预测响应数据不完整或存在非法数值。"); updateControls(); return;
    }
    lastResult=result; chart->setResult(result);
    const QJsonObject model=result.value("model").toObject();
    resultLabel->setText(QString("%1 | %2 | 历史样本ID %3 → %4 | 未来%5步 | %6 / %7")
        .arg(result.value("company_name").toString(),result.value("dataset").toString())
        .arg(result.value("start_sample_id").toVariant().toLongLong()).arg(result.value("end_sample_id").toVariant().toLongLong())
        .arg(horizon).arg(model.value("name").toString(),model.value("version").toString()));
    resultTable->setRowCount(predictions.size());
    for (int row=0; row<predictions.size(); ++row) {
        resultTable->setItem(row,0,new QTableWidgetItem(QString::number(row+1)));
        for (int feature=0; feature<4; ++feature) resultTable->setItem(row,feature+1,
            new QTableWidgetItem(QString::number(predictions[row].toObject().value(kFields[feature]).toDouble(),'g',8)));
    }
    AppLogger::instance().log(
        AppLogType::Task, AppLogLevel::Info,
        QStringLiteral("趋势预测"),
        QStringLiteral("公司 %1 预测完成，历史120条，输出 %2 步")
            .arg(result.value("company_name").toString())
            .arg(horizon));
    updateControls();
}

void PredictionPage::exportResult()
{
    if (lastResult.isEmpty()) return;
    const QString path=QFileDialog::getSaveFileName(this,"导出预测结果","forecast.csv","CSV (*.csv)");
    if (path.isEmpty()) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly|QIODevice::Text)) { QMessageBox::warning(this,"导出失败",file.errorString()); return; }
    QTextStream stream(&file); stream.setCodec("UTF-8"); stream.setGenerateByteOrderMark(true);
    stream << "company_id,dataset,end_sample_id,model,version,step,cod,nh3n,tp,turbidity\n";
    const QJsonObject model=lastResult.value("model").toObject();
    for (const QJsonValue &item : lastResult.value("predictions").toArray()) {
        const QJsonObject point=item.toObject();
        stream << lastResult.value("company_id").toVariant().toLongLong() << ','
               << csvCell(lastResult.value("dataset").toString()) << ','
               << lastResult.value("end_sample_id").toVariant().toLongLong() << ','
               << csvCell(model.value("name").toString()) << ',' << csvCell(model.value("version").toString()) << ',' << point.value("step").toInt();
        for (const QString &field : kFields) stream << ',' << QString::number(point.value(field).toDouble(),'g',9);
        stream << '\n';
    }
    stream.flush();
    if (stream.status()!=QTextStream::Ok) { file.cancelWriting(); QMessageBox::warning(this,"导出失败","写入失败。"); return; }
    if (!file.commit()) QMessageBox::warning(this,"导出失败",file.errorString());
    else QMessageBox::information(this,"导出完成","已保存预测结果与模型版本信息。");
}

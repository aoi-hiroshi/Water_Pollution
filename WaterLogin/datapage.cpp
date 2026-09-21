#include "datapage.h"

#include "dataservice.h"
#include "classificationcharts.h"
#include "applogger.h"

#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>
#include <QFrame>
#include <QFileDialog>
#include <QFont>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <climits>
#include <initializer_list>

namespace {
QFrame *createInfoCardFrame()
{
    QFrame *card = new QFrame;
    card->setObjectName("infoCard");
    return card;
}

QString formatChartAxisText(double value, double span)
{
    const double magnitude = std::abs(value);
    if (magnitude >= 10000.0) {
        return QString::number(value / 1000.0, 'f', 1) + QLatin1String("k");
    }
    if (magnitude >= 1000.0) {
        return QString::number(value, 'f', 0);
    }
    if (span < 1.0) {
        return QString::number(value, 'f', 2);
    }
    if (span < 20.0) {
        return QString::number(value, 'f', 1);
    }
    return QString::number(value, 'f', 0);
}
}

DataPage::DataPage(QWidget *parent)
    : QWidget(parent)
    , dataService(new DataService(this))
    , companyComboBox(nullptr)
    , datasetComboBox(nullptr)
    , taskModeComboBox(nullptr)
    , distributionTypeComboBox(nullptr)
    , distributionFeatureComboBox(nullptr)
    , correlationMethodComboBox(nullptr)
    , sequenceFeatureComboBox(nullptr)
    , sequenceWindowComboBox(nullptr)
    , forecastFeatureComboBox(nullptr)
    , forecastStepComboBox(nullptr)
    , forecastCheckComboBox(nullptr)
    , previewTable(nullptr)
    , modeDescriptionLabel(nullptr)
    , chartPlaceholderLabel(nullptr)
    , resultSummaryLabel(nullptr)
    , modeStackedWidget(nullptr)
    , refreshButton(nullptr)
    , overviewButton(nullptr)
{
    initUI();

    connect(dataService, &DataService::companyListReady, this, &DataPage::handleCompanyListReady);
    connect(dataService, &DataService::overviewReady, this, &DataPage::handleOverviewReady);
    connect(dataService, &DataService::classificationReady, this, &DataPage::handleClassificationReady);
    connect(dataService, &DataService::forecastAnalysisReady, this, &DataPage::handleForecastReady);
    connect(dataService, &DataService::serviceError, this, &DataPage::handleServiceError);

    loadCompanyList();
}

QWidget *DataPage::createFunctionCard(const QString &title,
                                      const QString &description,
                                      const QString &buttonText,
                                      QWidget *extraWidget,
                                      QPushButton **actionButton)
{
    QFrame *card = createInfoCardFrame();
    card->setMinimumHeight(178);

    QVBoxLayout *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);

    QLabel *titleLabel = new QLabel(title);
    titleLabel->setObjectName("cardTitle");

    QLabel *descLabel = new QLabel(description);
    descLabel->setObjectName("cardSubText");
    descLabel->setWordWrap(true);

    layout->addWidget(titleLabel);
    layout->addWidget(descLabel);

    if (extraWidget != nullptr) {
        layout->addWidget(extraWidget);
    }

    layout->addStretch();

    QPushButton *button = new QPushButton(buttonText);
    button->setObjectName("secondaryButton");
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(34);
    layout->addWidget(button);

    if (actionButton != nullptr) {
        *actionButton = button;
    }

    return card;
}

QWidget *DataPage::createTraceModeView()
{
    QWidget *page = new QWidget;
    QGridLayout *layout = new QGridLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(12);

    QWidget *function1Card = createFunctionCard(
        QStringLiteral("功能一：统计信息与原始图"),
        QStringLiteral("从数据库读取当前公司的样本数据，返回十个特征的统计描述与原始折线图，并同步刷新下方预览表。"),
        QStringLiteral("查看统计信息与原始图"),
        nullptr,
        &overviewButton);

    QWidget *function2Card = createFunctionCard(
        QStringLiteral("功能二：空值检测与修复"),
        QStringLiteral("检测十项特征的空值，用本次公司数据的中位数修复；整列为空时保留空值。先预览，再保存新版本。"),
        QStringLiteral("空值检测与修复"), nullptr, &missingButton);

    QWidget *cleanConfig = new QWidget;
    QHBoxLayout *cleanLayout = new QHBoxLayout(cleanConfig);
    cleanLayout->setContentsMargins(0, 0, 0, 0);
    windowInput = new QSpinBox;
    windowInput->setRange(2, 500); windowInput->setValue(50);
    windowInput->setPrefix(QStringLiteral("窗口 "));
    thresholdInput = new QDoubleSpinBox;
    thresholdInput->setRange(0.01, 20); thresholdInput->setValue(3);
    thresholdInput->setPrefix(QStringLiteral("阈值 "));
    cleanLayout->addWidget(windowInput); cleanLayout->addWidget(thresholdInput);

    QWidget *function3Card = createFunctionCard(
        QStringLiteral("功能三：滑动 Z-score 异常修复"),
        QStringLiteral("先修复空值，再按前 N 个样本识别异常，异常点取前一个已修复值。兼容 Notebook 的简化规则，不是完整卡尔曼滤波。"),
        QStringLiteral("预览异常值清洗"), cleanConfig, &cleanButton);

    QWidget *distributionConfig = new QWidget;
    QHBoxLayout *distributionConfigLayout = new QHBoxLayout(distributionConfig);
    distributionConfigLayout->setContentsMargins(0, 0, 0, 0);
    distributionConfigLayout->setSpacing(8);

    distributionTypeComboBox = new QComboBox;
    distributionTypeComboBox->addItems(QStringList() << QStringLiteral("箱型图分布")
                                                     << QStringLiteral("直方图分布")
                                                     << QStringLiteral("各公司分布总述"));

    distributionFeatureComboBox = new QComboBox;
    distributionFeatureComboBox->addItems(QStringList() << QStringLiteral("水温")
                                                        << QStringLiteral("pH")
                                                        << QStringLiteral("COD")
                                                        << QStringLiteral("氨氮")
                                                        << QStringLiteral("总磷")
                                                        << QStringLiteral("液位")
                                                        << QStringLiteral("ORP")
                                                        << QStringLiteral("电导率")
                                                        << QStringLiteral("溶解氧")
                                                        << QStringLiteral("浊度"));

    distributionConfigLayout->addWidget(distributionTypeComboBox, 1);
    distributionConfigLayout->addWidget(distributionFeatureComboBox, 1);

    QWidget *function4Card = createFunctionCard(
        QStringLiteral("功能四：分布分析"),
        QStringLiteral("C++ 计算分位数、箱线统计及直方图。公司对比使用原始数据和统一分箱边界。"),
        QStringLiteral("生成分布图"),
        distributionConfig, &distributionButton);

    QWidget *correlationConfig = new QWidget;
    QHBoxLayout *correlationConfigLayout = new QHBoxLayout(correlationConfig);
    correlationConfigLayout->setContentsMargins(0, 0, 0, 0);
    correlationConfigLayout->setSpacing(8);

    QLabel *methodLabel = new QLabel(QStringLiteral("分析方式"));
    methodLabel->setObjectName("cardSubText");

    correlationMethodComboBox = new QComboBox;
    correlationMethodComboBox->addItems(QStringList() << QStringLiteral("皮尔逊相关性分析")
                                                      << QStringLiteral("斯皮尔曼相关性分析"));

    correlationConfigLayout->addWidget(methodLabel);
    correlationConfigLayout->addWidget(correlationMethodComboBox, 1);

    QWidget *function5Card = createFunctionCard(
        QStringLiteral("功能五：相关性分析"),
        QStringLiteral("十项特征的 Pearson / Spearman 热力图；每对使用共同非空样本，常量或样本不足显示 NA。"),
        QStringLiteral("生成相关性图"),
        correlationConfig, &correlationButton);

    layout->addWidget(function1Card, 0, 0);
    layout->addWidget(function2Card, 0, 1);
    layout->addWidget(function3Card, 0, 2);
    layout->addWidget(function4Card, 1, 0, 1, 2);
    layout->addWidget(function5Card, 1, 2);

    return page;
}

QWidget *DataPage::createPredictionModeView()
{
    QWidget *page = new QWidget;
    QGridLayout *layout = new QGridLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(12);

    QWidget *sequenceConfig = new QWidget;
    QHBoxLayout *sequenceConfigLayout = new QHBoxLayout(sequenceConfig);
    sequenceConfigLayout->setContentsMargins(0, 0, 0, 0);
    sequenceConfigLayout->setSpacing(8);

    sequenceFeatureComboBox = new QComboBox;
    sequenceFeatureComboBox->addItems(QStringList() << QStringLiteral("COD")
                                                    << QStringLiteral("氨氮")
                                                    << QStringLiteral("总磷")
                                                    << QStringLiteral("溶解氧")
                                                    << QStringLiteral("浊度"));

    sequenceWindowComboBox = new QComboBox;
    sequenceWindowComboBox->addItems(QStringList() << QStringLiteral("窗口 12")
                                                   << QStringLiteral("窗口 24")
                                                   << QStringLiteral("窗口 48"));

    sequenceConfigLayout->addWidget(sequenceFeatureComboBox, 1);
    sequenceConfigLayout->addWidget(sequenceWindowComboBox, 1);

    QWidget *function1Card = createFunctionCard(
        QStringLiteral("功能一：时序概览与序列检查"),
        QStringLiteral("检查采样索引连续性、时间戳覆盖率与目标指标趋势。"),
        QStringLiteral("查看时序概览"),
        sequenceConfig,
        &sequenceButton);

    QWidget *function2Card = createFunctionCard(
        QStringLiteral("功能二：空值检测与插值修复"),
        QStringLiteral("检测十项水质特征的缺失值，内部缺口线性插值，序列边缘使用最近有效值。"),
        QStringLiteral("检测并修复空值"),
        nullptr,
        &forecastMissingButton);

    QWidget *function3Card = createFunctionCard(
        QStringLiteral("功能三：异常值平滑与滤波"),
        QStringLiteral("先插值修复，再使用只依赖当前及历史样本的因果移动平均完成平滑。"),
        QStringLiteral("执行平滑处理"),
        nullptr,
        &smoothButton);

    QWidget *forecastConfig = new QWidget;
    QHBoxLayout *forecastConfigLayout = new QHBoxLayout(forecastConfig);
    forecastConfigLayout->setContentsMargins(0, 0, 0, 0);
    forecastConfigLayout->setSpacing(8);

    forecastFeatureComboBox = new QComboBox;
    forecastFeatureComboBox->addItems(QStringList() << QStringLiteral("COD")
                                                    << QStringLiteral("氨氮")
                                                    << QStringLiteral("总磷")
                                                    << QStringLiteral("浊度"));

    forecastStepComboBox = new QComboBox;
    forecastStepComboBox->addItems(QStringList() << QStringLiteral("预测 6 步")
                                                 << QStringLiteral("预测 12 步")
                                                 << QStringLiteral("预测 24 步"));

    forecastConfigLayout->addWidget(forecastFeatureComboBox, 1);
    forecastConfigLayout->addWidget(forecastStepComboBox, 1);

    QWidget *function4Card = createFunctionCard(
        QStringLiteral("功能四：预测数据分布与窗口预览"),
        QStringLiteral("按 Attention-LSTM 的 120 步输入长度计算可构造窗口，并预览目标指标分布。"),
        QStringLiteral("查看分布与窗口"),
        forecastConfig,
        &windowPreviewButton);

    QWidget *forecastCheckConfig = new QWidget;
    QHBoxLayout *forecastCheckLayout = new QHBoxLayout(forecastCheckConfig);
    forecastCheckLayout->setContentsMargins(0, 0, 0, 0);
    forecastCheckLayout->setSpacing(8);

    QLabel *checkLabel = new QLabel(QStringLiteral("分析项"));
    checkLabel->setObjectName("cardSubText");

    forecastCheckComboBox = new QComboBox;
    forecastCheckComboBox->addItems(QStringList() << QStringLiteral("趋势性检查")
                                                  << QStringLiteral("周期性检查")
                                                  << QStringLiteral("平稳性检查"));

    forecastCheckLayout->addWidget(checkLabel);
    forecastCheckLayout->addWidget(forecastCheckComboBox, 1);

    QWidget *function5Card = createFunctionCard(
        QStringLiteral("功能五：预测前诊断分析"),
        QStringLiteral("输出趋势斜率、指定滞后自相关或前后半段均值/方差变化，作为建模预检查。"),
        QStringLiteral("生成诊断结果"),
        forecastCheckConfig,
        &diagnosisButton);

    layout->addWidget(function1Card, 0, 0);
    layout->addWidget(function2Card, 0, 1);
    layout->addWidget(function3Card, 0, 2);
    layout->addWidget(function4Card, 1, 0, 1, 2);
    layout->addWidget(function5Card, 1, 2);

    return page;
}

void DataPage::updateModeDescription(int modeIndex)
{
    if (modeIndex == 0) {
        modeDescriptionLabel->setText(QStringLiteral("分类预处理：C++ 清洗与分析 → Qt 绘图 → 保存独立版本 → 溯源页面分类。原始数据不覆盖。"));
        chartPlaceholderLabel->setText(QStringLiteral("图像展示区\n\n当前用于显示十特征原始折线图"));
        resultSummaryLabel->setText(QStringLiteral("等待加载公司统计摘要。"));
        return;
    }

    modeDescriptionLabel->setText(QStringLiteral("预测预处理：锁定公司7 → train/val/test → C++时序检查、插值、平滑、窗口与诊断。原始数据不覆盖。"));
    chartPlaceholderLabel->setText(QStringLiteral("预测模式图像展示区\n\n请选择功能执行分析"));
    resultSummaryLabel->setText(QStringLiteral("等待执行预测数据预处理。"));
}

void DataPage::populatePreviewTable(const QJsonArray &previewRows)
{
    const QStringList headers = {
        QStringLiteral("ID"),
        QStringLiteral("序列号"),
        QStringLiteral("采样时间"),
        QStringLiteral("水温"),
        QStringLiteral("pH"),
        QStringLiteral("COD"),
        QStringLiteral("氨氮"),
        QStringLiteral("总磷"),
        QStringLiteral("液位"),
        QStringLiteral("ORP"),
        QStringLiteral("电导率"),
        QStringLiteral("溶解氧"),
        QStringLiteral("浊度")
    };

    previewTable->clear();
    previewTable->setColumnCount(headers.size());
    previewTable->setHorizontalHeaderLabels(headers);
    previewTable->setRowCount(previewRows.size());

    for (int row = 0; row < previewRows.size(); ++row) {
        const QJsonObject item = previewRows.at(row).toObject();
        QStringList values;
        values << QString::number(item.value(QStringLiteral("id")).toDouble(), 'f', 0);
        values << (item.value(QStringLiteral("sample_index")).isDouble()
                       ? QString::number(item.value(QStringLiteral("sample_index")).toDouble(), 'f', 0)
                       : QStringLiteral("-"));
        values << item.value(QStringLiteral("sampled_at")).toString(QStringLiteral("-"));
        const QStringList keys = {"temperature","ph","cod","nh3n","tp","water_level","orp",
                                  "conductivity","dissolved_oxygen","turbidity"};
        for (const auto &key : keys) {
            const auto cell = item.value(key);
            values << (cell.isDouble() ? QString::number(cell.toDouble(),'f',4) : QStringLiteral("空值"));
        }

        for (int column = 0; column < values.size(); ++column) {
            previewTable->setItem(row, column, new QTableWidgetItem(values.at(column)));
        }
    }
}

void DataPage::renderOverviewSummary(const QJsonObject &overview)
{
    const QJsonObject company = overview.value(QStringLiteral("company")).toObject();
    const QJsonArray summary = overview.value(QStringLiteral("summary")).toArray();
    const auto text=[](const QJsonValue &v) { return v.isDouble() ? QString::number(v.toDouble(),'f',4) : QStringLiteral("NA"); };

    QStringList lines;
    lines << QStringLiteral("公司：%1").arg(company.value(QStringLiteral("company_name")).toString());
    lines << QStringLiteral("公司编码：%1").arg(company.value(QStringLiteral("company_code")).toString());
    lines << QStringLiteral("数据集：%1").arg(overview.value(QStringLiteral("dataset")).toString());
    lines << QStringLiteral("样本总数：%1").arg(overview.value(QStringLiteral("total_rows")).toInt());
    lines << QString();

    for (const QJsonValue &value : summary) {
        const QJsonObject item = value.toObject();
        lines << QStringLiteral("%1：均值 %2，标准差 %3，最小值 %4，最大值 %5")
                     .arg(item.value(QStringLiteral("label")).toString())
                     .arg(text(item.value(QStringLiteral("mean"))))
                     .arg(text(item.value(QStringLiteral("std"))))
                     .arg(text(item.value(QStringLiteral("min"))))
                     .arg(text(item.value(QStringLiteral("max"))));
    }

    resultSummaryLabel->setText(lines.join(QStringLiteral("\n")));
}

void DataPage::renderOverviewChart(const QJsonArray &previewRows,
                                   const QString &title,
                                   const QString &seriesLabel)
{
    if (previewRows.isEmpty()) {
        currentChartPixmap = QPixmap();
        chartPlaceholderLabel->setPixmap(QPixmap());
        chartPlaceholderLabel->setText(QStringLiteral("没有可绘制的样本数据"));
        return;
    }

    struct Feature {
        const char *field;
        const char *label;
        const char *unit;
    };
    const Feature features[] = {
        {"temperature", "水温", "°C"}, {"ph", "pH", "-"},
        {"cod", "COD", "mg/L"}, {"nh3n", "氨氮", "mg/L"},
        {"tp", "总磷", "mg/L"}, {"water_level", "液位", "-"},
        {"orp", "ORP", "mV"}, {"conductivity", "电导率", "μS/cm"},
        {"dissolved_oxygen", "溶解氧", "mg/L"},
        {"turbidity", "浊度", "-"}
    };

    QPixmap canvas(1800, 720);
    canvas.fill(Qt::white);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QFont titleFont(QStringLiteral("Microsoft YaHei"), 17, QFont::DemiBold);
    QFont featureFont(QStringLiteral("Microsoft YaHei"), 11, QFont::DemiBold);
    QFont axisFont(QStringLiteral("Microsoft YaHei"), 8);
    painter.setFont(titleFont);
    painter.setPen(QColor(QStringLiteral("#172033")));
    painter.drawText(QRect(32, 10, canvas.width() - 64, 34),
                     Qt::AlignCenter,
                     title);

    painter.setFont(axisFont);
    painter.setPen(QColor(QStringLiteral("#64748B")));
    painter.drawText(QRect(34, 40, canvas.width() - 68, 22),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("当前结果 %1 个绘图点 · 横轴为数据库样本 ID")
                         .arg(previewRows.size()));
    const QRectF legendLine(canvas.width() - 170, 50, 30, 0);
    painter.setPen(QPen(QColor(QStringLiteral("#2F80ED")), 3));
    painter.drawLine(legendLine.topLeft(), legendLine.topRight());
    painter.setPen(QColor(QStringLiteral("#475569")));
    painter.drawText(QRect(canvas.width() - 132, 38, 100, 24),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     seriesLabel);

    const int columns = 5;
    const int rows = 2;
    const int outerMargin = 28;
    const int horizontalGap = 16;
    const int verticalGap = 18;
    const int contentTop = 70;
    const int cellWidth = (canvas.width() - outerMargin * 2
                           - horizontalGap * (columns - 1)) / columns;
    const int cellHeight = (canvas.height() - contentTop - outerMargin
                            - verticalGap * (rows - 1)) / rows;

    const QJsonObject firstRow = previewRows.first().toObject();
    const QJsonObject lastRow = previewRows.last().toObject();
    const QString firstId = QString::number(
        firstRow.value(QStringLiteral("id")).toVariant().toLongLong());
    const QString lastId = QString::number(
        lastRow.value(QStringLiteral("id")).toVariant().toLongLong());

    for (int featureIndex = 0; featureIndex < 10; ++featureIndex) {
        QVector<double> values;
        for (const QJsonValue &rowValue : previewRows) {
            const QJsonValue value = rowValue.toObject().value(
                QLatin1String(features[featureIndex].field));
            if (value.isDouble()) {
                values.append(value.toDouble());
            }
        }

        const int column = featureIndex % columns;
        const int row = featureIndex / columns;
        const QRect cell(
            outerMargin + column * (cellWidth + horizontalGap),
            contentTop + row * (cellHeight + verticalGap),
            cellWidth, cellHeight);
        const QRectF plot = QRectF(cell).adjusted(58, 48, -16, -42);

        painter.setPen(QPen(QColor(QStringLiteral("#D7E0EA")), 1));
        painter.setBrush(QColor(QStringLiteral("#FFFFFF")));
        painter.drawRoundedRect(QRectF(cell).adjusted(0.5, 0.5, -0.5, -0.5),
                                8, 8);

        painter.setFont(featureFont);
        painter.setPen(QColor(QStringLiteral("#1E293B")));
        painter.drawText(QRect(cell.left() + 12, cell.top() + 9,
                               cell.width() - 24, 27),
                         Qt::AlignCenter,
                         QStringLiteral("%1 (%2)")
                             .arg(QString::fromUtf8(features[featureIndex].label),
                                  QString::fromUtf8(features[featureIndex].unit)));

        if (values.isEmpty()) {
            painter.setFont(axisFont);
            painter.setPen(QColor(QStringLiteral("#94A3B8")));
            painter.drawText(plot, Qt::AlignCenter, QStringLiteral("暂无数据"));
            continue;
        }

        const auto range = std::minmax_element(values.constBegin(), values.constEnd());
        double minimum = *range.first;
        double maximum = *range.second;
        if (minimum == maximum) {
            minimum -= 0.5;
            maximum += 0.5;
        }
        const double rawSpan = maximum - minimum;
        const double padding = std::max(rawSpan * 0.08, 0.001);
        minimum -= padding;
        maximum += padding;
        const double span = maximum - minimum;

        painter.setFont(axisFont);
        for (int tick = 0; tick <= 4; ++tick) {
            const double ratio = tick / 4.0;
            const double y = plot.bottom() - ratio * plot.height();
            painter.setPen(QPen(QColor(QStringLiteral("#E7ECF2")), 1));
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            painter.setPen(QColor(QStringLiteral("#64748B")));
            painter.drawText(QRectF(cell.left() + 3, y - 9, 49, 18),
                             Qt::AlignRight | Qt::AlignVCenter,
                             formatChartAxisText(minimum + ratio * span, span));
        }
        for (int tick = 0; tick <= 4; ++tick) {
            const double x = plot.left() + tick * plot.width() / 4.0;
            painter.setPen(QPen(QColor(QStringLiteral("#EEF2F6")), 1));
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }
        painter.setPen(QPen(QColor(QStringLiteral("#AAB7C5")), 1));
        painter.drawLine(plot.bottomLeft(), plot.bottomRight());
        painter.drawLine(plot.topLeft(), plot.bottomLeft());

        QPainterPath path;
        bool connected = false;
        for (int index = 0; index < previewRows.size(); ++index) {
            const auto value = previewRows.at(index).toObject().value(
                QLatin1String(features[featureIndex].field));
            if (!value.isDouble()) {
                connected = false;
                continue;
            }
            const double xRatio = previewRows.size() == 1
                ? 0.5 : static_cast<double>(index) / (previewRows.size() - 1);
            const double yRatio = (value.toDouble() - minimum) / span;
            const QPointF point(plot.left() + xRatio * plot.width(),
                                plot.bottom() - yRatio * plot.height());
            if (connected) {
                path.lineTo(point);
            } else {
                path.moveTo(point);
            }
            connected = true;
        }

        painter.save();
        painter.setClipRect(plot.adjusted(-2, -2, 2, 2));
        painter.setPen(QPen(QColor(QStringLiteral("#2F80ED")), 2.4,
                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
        painter.restore();

        painter.setFont(axisFont);
        painter.setPen(QColor(QStringLiteral("#64748B")));
        painter.drawText(QRectF(plot.left() - 8, plot.bottom() + 5,
                                plot.width() + 16, 18),
                         Qt::AlignLeft | Qt::AlignVCenter, firstId);
        painter.drawText(QRectF(plot.left() - 8, plot.bottom() + 5,
                                plot.width() + 16, 18),
                         Qt::AlignRight | Qt::AlignVCenter, lastId);
        painter.drawText(QRectF(plot.left(), plot.bottom() + 21,
                                plot.width(), 17),
                         Qt::AlignCenter, QStringLiteral("样本 ID"));
    }
    painter.end();

    currentChartPixmap = canvas;
    updateChartPixmap();
}

void DataPage::updateChartPixmap()
{
    if (currentChartPixmap.isNull()) {
        return;
    }

    const QSize targetSize = chartPlaceholderLabel->size() - QSize(12, 12);
    if (targetSize.width() <= 0 || targetSize.height() <= 0) {
        return;
    }

    chartPlaceholderLabel->setText(QString());
    chartPlaceholderLabel->setPixmap(currentChartPixmap.scaled(
        targetSize,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation));
}

void DataPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateChartPixmap();
}

void DataPage::initUI()
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

    QFrame *controlCard = createInfoCardFrame();
    QVBoxLayout *controlCardLayout = new QVBoxLayout(controlCard);
    controlCardLayout->setContentsMargins(16, 14, 16, 14);
    controlCardLayout->setSpacing(10);

    QHBoxLayout *titleLayout = new QHBoxLayout;
    QLabel *titleLabel = new QLabel(QStringLiteral("数据功能工作台"));
    titleLabel->setObjectName("cardTitle");

    modeDescriptionLabel = new QLabel;
    modeDescriptionLabel->setObjectName("cardSubText");
    modeDescriptionLabel->setWordWrap(true);

    titleLayout->addWidget(titleLabel);
    titleLayout->addSpacing(14);
    titleLayout->addWidget(modeDescriptionLabel, 1);

    QHBoxLayout *selectorLayout = new QHBoxLayout;
    selectorLayout->setSpacing(10);

    QLabel *taskModeLabel = new QLabel(QStringLiteral("任务模式"));
    taskModeLabel->setObjectName("cardSubText");
    taskModeComboBox = new QComboBox;
    taskModeComboBox->addItems(QStringList() << QStringLiteral("溯源 / 分类数据预处理")
                                             << QStringLiteral("预测数据预处理"));

    QLabel *companyLabel = new QLabel(QStringLiteral("公司选择"));
    companyLabel->setObjectName("cardSubText");
    companyComboBox = new QComboBox;
    companyComboBox->setMinimumWidth(220);

    QLabel *datasetLabel = new QLabel(QStringLiteral("数据源"));
    datasetLabel->setObjectName("cardSubText");
    datasetComboBox = new QComboBox;
    datasetComboBox->addItem(QStringLiteral("训练数据 train_data"), QStringLiteral("train_data"));
    datasetComboBox->addItem(QStringLiteral("测试数据 test_data"), QStringLiteral("test_data"));

    refreshButton = new QPushButton(QStringLiteral("刷新数据呈现"));
    refreshButton->setObjectName("primaryButton");
    refreshButton->setCursor(Qt::PointingHandCursor);
    refreshButton->setFixedHeight(34);

    selectorLayout->addWidget(taskModeLabel);
    selectorLayout->addWidget(taskModeComboBox, 1);
    selectorLayout->addSpacing(6);
    selectorLayout->addWidget(companyLabel);
    selectorLayout->addWidget(companyComboBox, 1);
    selectorLayout->addSpacing(6);
    selectorLayout->addWidget(datasetLabel);
    selectorLayout->addWidget(datasetComboBox, 1);
    selectorLayout->addStretch();
    selectorLayout->addWidget(refreshButton);

    controlCardLayout->addLayout(titleLayout);
    controlCardLayout->addLayout(selectorLayout);
    QHBoxLayout *processingLayout = new QHBoxLayout;
    cleaningRunInput = new QSpinBox;
    cleaningRunInput->setRange(0, INT_MAX);
    cleaningRunInput->setSpecialValueText(QStringLiteral("原始数据（0）"));
    cleaningRunInput->setToolTip(QStringLiteral("输入保存后的版本编号；概览按钮始终读取原始数据。公司/数据集切换时重置。"));
    saveVersionButton = new QPushButton(QStringLiteral("确认并保存清洗新版本"));
    saveVersionButton->setEnabled(false);
    saveChartButton = new QPushButton(QStringLiteral("导出当前图 PNG"));
    saveChartButton->setEnabled(false);
    processingLayout->addWidget(new QLabel(QStringLiteral("分析数据版本")));
    processingLayout->addWidget(cleaningRunInput);
    processingLayout->addWidget(saveVersionButton);
    processingLayout->addWidget(saveChartButton);
    processingLayout->addStretch();
    controlCardLayout->addLayout(processingLayout);

    QFrame *previewCard = createInfoCardFrame();
    QVBoxLayout *previewLayout = new QVBoxLayout(previewCard);
    previewLayout->setContentsMargins(16, 12, 16, 12);
    previewLayout->setSpacing(10);

    QLabel *previewTitle = new QLabel(QStringLiteral("样本数据预览"));
    previewTitle->setObjectName("cardTitle");

    previewTable = new QTableWidget(0, 11);
    previewTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    previewTable->verticalHeader()->setVisible(false);
    previewTable->setAlternatingRowColors(true);
    previewTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    previewTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    previewTable->setMinimumHeight(240);

    previewLayout->addWidget(previewTitle);
    previewLayout->addWidget(previewTable);

    QFrame *modeCard = createInfoCardFrame();
    QVBoxLayout *modeLayout = new QVBoxLayout(modeCard);
    modeLayout->setContentsMargins(16, 12, 16, 12);
    modeLayout->setSpacing(10);

    QLabel *modeTitle = new QLabel(QStringLiteral("预处理功能布局"));
    modeTitle->setObjectName("cardTitle");

    modeStackedWidget = new QStackedWidget;
    modeStackedWidget->addWidget(createTraceModeView());
    modeStackedWidget->addWidget(createPredictionModeView());

    modeLayout->addWidget(modeTitle);
    modeLayout->addWidget(modeStackedWidget);

    QVBoxLayout *bottomLayout = new QVBoxLayout;
    bottomLayout->setSpacing(12);

    QFrame *chartCard = new QFrame;
    chartCard->setObjectName("displayCard");
    chartCard->setMinimumHeight(680);

    QVBoxLayout *chartLayout = new QVBoxLayout(chartCard);
    chartLayout->setContentsMargins(0, 0, 0, 0);
    chartLayout->setSpacing(0);

    QFrame *noticeBar = new QFrame;
    noticeBar->setObjectName("noticeBar");
    noticeBar->setFixedHeight(40);

    QHBoxLayout *noticeLayout = new QHBoxLayout(noticeBar);
    noticeLayout->setContentsMargins(14, 0, 14, 0);

    QLabel *noticeLabel = new QLabel(QStringLiteral("图像展示区：Qt 根据 Muduo 返回的 50 条预览序列，绘制十项水质指标趋势。"));
    noticeLabel->setObjectName("noticeLabel");
    noticeLayout->addWidget(noticeLabel);

    QFrame *chartArea = new QFrame;
    chartArea->setObjectName("mapArea");
    QVBoxLayout *chartAreaLayout = new QVBoxLayout(chartArea);
    chartAreaLayout->setContentsMargins(14, 14, 14, 14);

    chartPlaceholderLabel = new QLabel;
    chartPlaceholderLabel->setObjectName("mapPlaceholder");
    chartPlaceholderLabel->setAlignment(Qt::AlignCenter);
    chartPlaceholderLabel->setWordWrap(true);
    chartPlaceholderLabel->setMinimumSize(900, 600);

    chartAreaLayout->addWidget(chartPlaceholderLabel, 1);

    chartLayout->addWidget(noticeBar);
    chartLayout->addWidget(chartArea, 1);

    QFrame *resultCard = createInfoCardFrame();
    resultCard->setMinimumHeight(150);
    QVBoxLayout *resultLayout = new QVBoxLayout(resultCard);
    resultLayout->setContentsMargins(16, 14, 16, 14);
    resultLayout->setSpacing(8);

    QLabel *resultTitle = new QLabel(QStringLiteral("结果摘要"));
    resultTitle->setObjectName("cardTitle");

    resultSummaryLabel = new QLabel;
    resultSummaryLabel->setObjectName("cardSubText");
    resultSummaryLabel->setWordWrap(true);
    resultSummaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    resultLayout->addWidget(resultTitle);
    resultLayout->addWidget(resultSummaryLabel);
    resultLayout->addStretch();

    bottomLayout->addWidget(chartCard);
    bottomLayout->addWidget(resultCard);

    layout->addWidget(controlCard);
    layout->addWidget(previewCard);
    layout->addWidget(modeCard);
    layout->addLayout(bottomLayout);

    scrollArea->setWidget(content);
    rootLayout->addWidget(scrollArea);

    connect(taskModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        cleaningRunInput->setValue(0);
        previewOperation.clear(); previewRequest = QJsonObject(); currentChartPixmap = QPixmap();
        saveVersionButton->setEnabled(false); saveChartButton->setEnabled(false);
        chartPlaceholderLabel->setPixmap(QPixmap());
        modeStackedWidget->setCurrentIndex(index);
        updateModeDescription(index);
        configureModeSelectors(index);
        loadOverviewData();
    });
    connect(refreshButton, &QPushButton::clicked, this, &DataPage::loadOverviewData);
    connect(overviewButton, &QPushButton::clicked, this, &DataPage::loadOverviewData);
    connect(missingButton, &QPushButton::clicked, this, [this] { runClassification(QStringLiteral("missing")); });
    connect(cleanButton, &QPushButton::clicked, this, [this] { runClassification(QStringLiteral("clean")); });
    connect(distributionButton, &QPushButton::clicked, this, [this] { runClassification(QStringLiteral("distribution")); });
    connect(correlationButton, &QPushButton::clicked, this, [this] { runClassification(QStringLiteral("correlation")); });
    connect(sequenceButton, &QPushButton::clicked, this, [this] { runForecastAnalysis(QStringLiteral("sequence")); });
    connect(forecastMissingButton, &QPushButton::clicked, this, [this] { runForecastAnalysis(QStringLiteral("missing")); });
    connect(smoothButton, &QPushButton::clicked, this, [this] { runForecastAnalysis(QStringLiteral("smooth")); });
    connect(windowPreviewButton, &QPushButton::clicked, this, [this] { runForecastAnalysis(QStringLiteral("window")); });
    connect(diagnosisButton, &QPushButton::clicked, this, [this] { runForecastAnalysis(QStringLiteral("diagnosis")); });
    connect(saveVersionButton, &QPushButton::clicked, this, [this] {
        if (previewOperation.isEmpty() || busy) return;
        QDialog confirmation(this);
        confirmation.setObjectName(QStringLiteral("classificationSaveConfirmation"));
        confirmation.setWindowTitle(QStringLiteral("保存清洗版本"));
        QVBoxLayout *confirmationLayout=new QVBoxLayout(&confirmation);
        QLabel *notice=new QLabel(QStringLiteral("将按刚才预览的参数重新读取并清洗数据，保存独立版本，不覆盖原始表。若数据已变化，结果可能与预览不同。是否继续？"));
        notice->setWordWrap(true);notice->setMaximumWidth(560);
        QDialogButtonBox *buttons=new QDialogButtonBox(QDialogButtonBox::Yes|QDialogButtonBox::No);
        buttons->button(QDialogButtonBox::No)->setDefault(true);
        connect(buttons->button(QDialogButtonBox::Yes),&QPushButton::clicked,&confirmation,&QDialog::accept);
        connect(buttons->button(QDialogButtonBox::No),&QPushButton::clicked,&confirmation,&QDialog::reject);
        confirmationLayout->addWidget(notice);confirmationLayout->addWidget(buttons);
        if (confirmation.exec()==QDialog::Accepted)
            runClassification(previewOperation,true);
    });
    connect(saveChartButton, &QPushButton::clicked, this, [this] {
        if (currentChartPixmap.isNull()) return;
        const QString defaultName = taskModeComboBox->currentIndex() == 1
            ? QStringLiteral("forecast_preprocessing.png")
            : QStringLiteral("classification.png");
        const QString path=QFileDialog::getSaveFileName(this,QStringLiteral("保存图像"),defaultName,QStringLiteral("PNG (*.png)"));
        if (!path.isEmpty() && !currentChartPixmap.save(path,"PNG"))
            QMessageBox::warning(this,QStringLiteral("图像导出"),QStringLiteral("保存失败，请检查路径权限。"));
    });
    connect(companyComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0) {
            cleaningRunInput->setValue(0);
            loadOverviewData();
        }
    });
    connect(datasetComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0) {
            cleaningRunInput->setValue(0);
            loadOverviewData();
        }
    });

    updateModeDescription(0);
}

void DataPage::loadCompanyList()
{
    setBusy(true);
    companyComboBox->clear();
    companyComboBox->addItem(QStringLiteral("正在加载公司列表..."), -1);
    dataService->fetchCompanyList();
}

void DataPage::loadOverviewData()
{
    const int companyId = companyComboBox->currentData().toInt();
    if (companyId <= 0) {
        return;
    }

    previewOperation.clear(); previewRequest = QJsonObject();
    setBusy(true);
    chartPlaceholderLabel->setPixmap(QPixmap());
    chartPlaceholderLabel->setText(QStringLiteral("正在从 Muduo 加载数据概览..."));
    currentChartPixmap = QPixmap();
    resultSummaryLabel->setText(QStringLiteral("正在加载统计摘要..."));

    dataService->fetchOverview(companyId, datasetComboBox->currentData().toString(), 50);
}

void DataPage::handleCompanyListReady(const QJsonArray &companies)
{
    setBusy(false);
    this->companies = companies;
    configureModeSelectors(taskModeComboBox->currentIndex());

    if (companyComboBox->currentData().toInt() > 0) {
        loadOverviewData();
    }
}

void DataPage::configureModeSelectors(int modeIndex)
{
    const bool forecastMode = modeIndex == 1;
    companyComboBox->blockSignals(true);
    datasetComboBox->blockSignals(true);

    companyComboBox->clear();
    for (const QJsonValue &value : companies) {
        const QJsonObject company = value.toObject();
        const int companyId = company.value(QStringLiteral("company_id")).toInt();
        const QString taskType = company.value(QStringLiteral("task_type")).toString();
        const bool include = forecastMode
            ? companyId == 7
            : (taskType == QStringLiteral("trace") ||
               (taskType.isEmpty() && companyId != 7));
        if (include) {
            companyComboBox->addItem(
                company.value(QStringLiteral("company_name")).toString(), companyId);
        }
    }
    if (companyComboBox->count() == 0) {
        companyComboBox->addItem(
            forecastMode
                ? QStringLiteral("未找到公司7：浙江海正药业股份有限公司岩头")
                : QStringLiteral("暂无溯源公司数据"),
            -1);
    }

    datasetComboBox->clear();
    datasetComboBox->addItem(QStringLiteral("训练数据 train_data"), QStringLiteral("train_data"));
    if (forecastMode) {
        datasetComboBox->addItem(QStringLiteral("验证数据 val_data"), QStringLiteral("val_data"));
    }
    datasetComboBox->addItem(QStringLiteral("测试数据 test_data"), QStringLiteral("test_data"));

    companyComboBox->setCurrentIndex(0);
    datasetComboBox->setCurrentIndex(0);
    companyComboBox->setEnabled(!busy && !forecastMode);
    datasetComboBox->setEnabled(!busy);
    cleaningRunInput->setEnabled(!busy && !forecastMode);
    saveVersionButton->setEnabled(false);

    companyComboBox->blockSignals(false);
    datasetComboBox->blockSignals(false);
}

void DataPage::handleOverviewReady(const QJsonObject &overview)
{
    setBusy(false);
    const QJsonArray previewRows = overview.value(QStringLiteral("preview_rows")).toArray();
    populatePreviewTable(previewRows);
    renderOverviewSummary(overview);
    renderOverviewChart(previewRows);
    saveChartButton->setEnabled(!currentChartPixmap.isNull());
    AppLogger::instance().log(
        AppLogType::Task, AppLogLevel::Info,
        QStringLiteral("数据中心"),
        QStringLiteral("数据概览加载完成，共 %1 条，预览 %2 条")
            .arg(overview.value(QStringLiteral("total_rows")).toVariant().toLongLong())
            .arg(previewRows.size()));
}

void DataPage::handleServiceError(const QString &message)
{
    previewOperation.clear(); previewRequest = QJsonObject();
    setBusy(false); saveChartButton->setEnabled(false);
    currentChartPixmap = QPixmap();
    chartPlaceholderLabel->setPixmap(QPixmap());
    chartPlaceholderLabel->setText(QStringLiteral("加载失败"));
    populatePreviewTable(QJsonArray());
    resultSummaryLabel->setText(message);
    AppLogger::instance().log(
        AppLogType::Task, AppLogLevel::Error,
        QStringLiteral("数据中心"), message, QStringLiteral("失败"));
    QMessageBox::warning(this, QStringLiteral("数据模块"), message);
}

void DataPage::setBusy(bool value)
{
    busy=value;
    const bool forecastMode = taskModeComboBox->currentIndex() == 1;
    for (QWidget *widget : std::initializer_list<QWidget*>{datasetComboBox,taskModeComboBox,
         refreshButton,overviewButton,missingButton,cleanButton,distributionButton,correlationButton,
         sequenceButton,forecastMissingButton,smoothButton,windowPreviewButton,diagnosisButton,
         windowInput,thresholdInput,distributionTypeComboBox,distributionFeatureComboBox,correlationMethodComboBox,
         sequenceFeatureComboBox,sequenceWindowComboBox,forecastFeatureComboBox,forecastStepComboBox,forecastCheckComboBox})
        widget->setEnabled(!value);
    companyComboBox->setEnabled(!value && !forecastMode);
    cleaningRunInput->setEnabled(!value && !forecastMode);
    overviewButton->setEnabled(!value && !forecastMode);
    missingButton->setEnabled(!value && !forecastMode);
    cleanButton->setEnabled(!value && !forecastMode);
    distributionButton->setEnabled(!value && !forecastMode);
    correlationButton->setEnabled(!value && !forecastMode);
    sequenceButton->setEnabled(!value && forecastMode);
    forecastMissingButton->setEnabled(!value && forecastMode);
    smoothButton->setEnabled(!value && forecastMode);
    windowPreviewButton->setEnabled(!value && forecastMode);
    diagnosisButton->setEnabled(!value && forecastMode);
    saveVersionButton->setEnabled(!value && !forecastMode && !previewOperation.isEmpty());
    saveChartButton->setEnabled(!value && !currentChartPixmap.isNull());
}

void DataPage::runClassification(QString operation, bool persist)
{
    if (busy || taskModeComboBox->currentIndex()!=0 || companyComboBox->currentData().toInt()<=0) return;
    QJsonObject request;
    if (persist) request=previewRequest;
    else {
        request.insert("company_id",companyComboBox->currentData().toInt());
        request.insert("dataset",datasetComboBox->currentData().toString());
        request.insert("cleaning_run_id",cleaningRunInput->value());
        request.insert("window",windowInput->value()); request.insert("threshold",thresholdInput->value());
        const QStringList fields={"temperature","ph","cod","nh3n","tp","water_level","orp","conductivity","dissolved_oxygen","turbidity"};
        request.insert("feature",fields.at(distributionFeatureComboBox->currentIndex()));
        const QStringList views={"boxplot","histogram","comparison"};
        request.insert("view",views.at(distributionTypeComboBox->currentIndex()));
        request.insert("method",correlationMethodComboBox->currentIndex()==0 ? "pearson" : "spearman");
        if (operation=="distribution" && request.value("view").toString()=="comparison") request.insert("cleaning_run_id",0);
    }
    request.insert("persist",persist);
    previewOperation.clear(); previewRequest = QJsonObject();
    if (!persist && (operation=="missing" || operation=="clean")) {
        previewRequest=request; previewOperation=operation;
    }
    setBusy(true);
    resultSummaryLabel->setText(persist ? QStringLiteral("正在清洗并保存独立版本...") : QStringLiteral("正在执行 C++ 分类数据处理..."));
    AppLogger::instance().log(
        AppLogType::Task, AppLogLevel::Info,
        QStringLiteral("数据处理"),
        QStringLiteral("执行 %1，公司 %2，数据集 %3，保存版本：%4")
            .arg(operation)
            .arg(request.value(QStringLiteral("company_id")).toInt())
            .arg(request.value(QStringLiteral("dataset")).toString())
            .arg(persist ? QStringLiteral("是") : QStringLiteral("否")),
        QStringLiteral("执行中"));
    dataService->fetchClassification(operation,request);
}

void DataPage::handleClassificationReady(const QJsonObject &result)
{
    const bool persisted=result.value("persisted").toBool();
    if (persisted) { previewOperation.clear(); previewRequest = QJsonObject(); }
    setBusy(false);
    populatePreviewTable(result.value("preview_after").toArray());
    currentChartPixmap=renderClassificationChart(result); updateChartPixmap();
    saveChartButton->setEnabled(true);
    QStringList lines;
    lines << QStringLiteral("%1 / %2；处理全部 %3 条；表格前50条，曲线最多300点")
        .arg(result.value("company_name").toString()).arg(result.value("dataset").toString())
        .arg(result.value("sample_count").toInt());
    lines << QStringLiteral("输入版本：%1（0为原始）；规则：%2")
        .arg(result.value("input_cleaning_run_id").toDouble(),0,'f',0).arg(result.value("rule").toString());
    if (persisted) {
        const double run=result.value("cleaning_run_id").toDouble();
        lines << QStringLiteral("已保存版本 %1。溯源页面输入同一数据集、样本ID和版本编号进行分类。").arg(run,0,'f',0);
        if (run<=INT_MAX) cleaningRunInput->setValue(static_cast<int>(run));
        else lines << QStringLiteral("版本超出 Qt 输入范围，请通过 HTTP API 使用该版本。");
    } else lines << QStringLiteral("本次仅预览/分析，没有修改原始数据库。");
    const auto text=[](const QJsonValue &v) { return v.isDouble() ? QString::number(v.toDouble(),'g',5) : QStringLiteral("NA"); };
    for (const auto &value : result.value("features").toArray()) {
        const auto f=value.toObject();
        lines << QStringLiteral("%1：空值 %2→%3；填充 %4；异常 %5；均值 %6；中位数 %7")
            .arg(f.value("label").toString()).arg(f.value("missing_before").toInt()).arg(f.value("missing_after").toInt())
            .arg(f.value("filled_count").toInt()).arg(f.value("outlier_count").toInt()).arg(text(f.value("mean"))).arg(text(f.value("median")));
    }
    for (const auto &value : result.value("groups").toArray()) {
        const auto g=value.toObject();
        lines << QStringLiteral("%1：Q10/Q25/中位/Q75/Q90 = %2/%3/%4/%5/%6；箱外异常 %7")
            .arg(g.value("company_name").toString()).arg(text(g.value("q10"))).arg(text(g.value("q25")))
            .arg(text(g.value("median"))).arg(text(g.value("q75"))).arg(text(g.value("q90"))).arg(g.value("outlier_count").toInt());
    }
    for (const auto &value : result.value("high_correlations").toArray()) {
        const auto pair=value.toObject();
        lines << QStringLiteral("高相关 %1 / %2：%3（共同样本 %4）")
            .arg(pair.value("first").toString()).arg(pair.value("second").toString())
            .arg(text(pair.value("coefficient"))).arg(pair.value("sample_count").toInt());
    }
    for (const auto &warning : result.value("warnings").toArray()) lines << QStringLiteral("提示：")+warning.toString();
    resultSummaryLabel->setText(lines.join('\n'));
    AppLogger::instance().log(
        AppLogType::Task, AppLogLevel::Info,
        QStringLiteral("数据处理"),
        QStringLiteral("规则 %1 处理完成，共 %2 条，保存版本：%3")
            .arg(result.value(QStringLiteral("rule")).toString())
            .arg(result.value(QStringLiteral("sample_count")).toInt())
            .arg(persisted ? QStringLiteral("是") : QStringLiteral("否")));
}

void DataPage::runForecastAnalysis(const QString &operation)
{
    if (busy || taskModeComboBox->currentIndex() != 1 ||
        companyComboBox->currentData().toInt() != 7) {
        return;
    }

    const QStringList sequenceFields = {
        QStringLiteral("cod"), QStringLiteral("nh3n"), QStringLiteral("tp"),
        QStringLiteral("dissolved_oxygen"), QStringLiteral("turbidity")
    };
    const QStringList forecastFields = {
        QStringLiteral("cod"), QStringLiteral("nh3n"),
        QStringLiteral("tp"), QStringLiteral("turbidity")
    };
    const QList<int> windows = {12, 24, 48};
    const QList<int> horizons = {6, 12, 24};
    const QStringList diagnostics = {
        QStringLiteral("trend"), QStringLiteral("seasonality"),
        QStringLiteral("stationarity")
    };

    QJsonObject request;
    request.insert(QStringLiteral("company_id"), 7);
    request.insert(QStringLiteral("dataset"), datasetComboBox->currentData().toString());
    request.insert(QStringLiteral("window"), windows.at(sequenceWindowComboBox->currentIndex()));
    request.insert(QStringLiteral("horizon"), horizons.at(forecastStepComboBox->currentIndex()));
    request.insert(QStringLiteral("diagnostic"), diagnostics.at(forecastCheckComboBox->currentIndex()));
    request.insert(QStringLiteral("feature"),
                   operation == QStringLiteral("sequence") ||
                           operation == QStringLiteral("missing") ||
                           operation == QStringLiteral("smooth")
                       ? sequenceFields.at(sequenceFeatureComboBox->currentIndex())
                       : forecastFields.at(forecastFeatureComboBox->currentIndex()));

    setBusy(true);
    chartPlaceholderLabel->setPixmap(QPixmap());
    chartPlaceholderLabel->setText(QStringLiteral("正在执行预测数据预处理..."));
    resultSummaryLabel->setText(
        QStringLiteral("正在从 Muduo 请求 /api/v1/data/forecast/%1").arg(operation));
    AppLogger::instance().log(
        AppLogType::Task, AppLogLevel::Info,
        QStringLiteral("预测数据预处理"),
        QStringLiteral("执行 %1，公司7，数据集 %2")
            .arg(operation, request.value(QStringLiteral("dataset")).toString()),
        QStringLiteral("执行中"));
    dataService->fetchForecastAnalysis(operation, request);
}

void DataPage::handleForecastReady(const QJsonObject &result)
{
    setBusy(false);
    const QJsonArray preview = result.value(QStringLiteral("preview_after")).toArray();
    QJsonArray chart = result.value(QStringLiteral("chart_after")).toArray();
    if (chart.isEmpty()) {
        chart = preview;
    }
    populatePreviewTable(preview);

    const QString operation = result.value(QStringLiteral("operation")).toString();
    const QMap<QString, QString> titles = {
        {QStringLiteral("sequence"), QStringLiteral("预测数据时序连续性与指标趋势")},
        {QStringLiteral("missing"), QStringLiteral("预测数据插值修复后趋势")},
        {QStringLiteral("smooth"), QStringLiteral("预测数据因果移动平均后趋势")},
        {QStringLiteral("window"), QStringLiteral("Attention-LSTM 滑动窗口数据趋势")},
        {QStringLiteral("diagnosis"), QStringLiteral("预测前诊断指标趋势")}
    };
    const bool transformed = operation == QStringLiteral("missing") ||
                             operation == QStringLiteral("smooth");
    renderOverviewChart(chart, titles.value(operation, QStringLiteral("预测数据预处理结果")),
                        transformed ? QStringLiteral("处理后序列")
                                    : QStringLiteral("分析序列"));
    saveChartButton->setEnabled(!currentChartPixmap.isNull());

    const auto numberText = [](const QJsonValue &value) {
        return value.isDouble() ? QString::number(value.toDouble(), 'g', 8)
                                : QStringLiteral("NA");
    };
    QStringList lines;
    lines << QStringLiteral("公司：%1（ID 7）")
                 .arg(result.value(QStringLiteral("company_name")).toString());
    lines << QStringLiteral("数据集：%1；操作：%2；样本：%3 条；规则：%4")
                 .arg(result.value(QStringLiteral("dataset")).toString())
                 .arg(operation)
                 .arg(result.value(QStringLiteral("sample_count")).toVariant().toLongLong())
                 .arg(result.value(QStringLiteral("rule")).toString());
    for (const QJsonValue &value : result.value(QStringLiteral("metrics")).toArray()) {
        const QJsonObject metric = value.toObject();
        lines << QStringLiteral("%1：%2 %3")
                     .arg(metric.value(QStringLiteral("label")).toString())
                     .arg(numberText(metric.value(QStringLiteral("value"))))
                     .arg(metric.value(QStringLiteral("text")).toString());
    }
    for (const QJsonValue &value : result.value(QStringLiteral("features")).toArray()) {
        const QJsonObject feature = value.toObject();
        const int missingBefore = feature.value(QStringLiteral("missing_before")).toInt();
        const int changed = feature.value(QStringLiteral("changed_count")).toInt();
        if (missingBefore > 0 || changed > 0) {
            lines << QStringLiteral("%1：空值 %2→%3；修改 %4；均值 %5；标准差 %6")
                         .arg(feature.value(QStringLiteral("label")).toString())
                         .arg(missingBefore)
                         .arg(feature.value(QStringLiteral("missing_after")).toInt())
                         .arg(changed)
                         .arg(numberText(feature.value(QStringLiteral("mean"))))
                         .arg(numberText(feature.value(QStringLiteral("std"))));
        }
    }
    for (const QJsonValue &warning : result.value(QStringLiteral("warnings")).toArray()) {
        lines << QStringLiteral("提示：%1").arg(warning.toString());
    }
    lines << QStringLiteral("本次结果仅用于预览和诊断，没有覆盖数据库原始数据。");
    resultSummaryLabel->setText(lines.join(QLatin1Char('\n')));

    AppLogger::instance().log(
        AppLogType::Task, AppLogLevel::Info,
        QStringLiteral("预测数据预处理"),
        QStringLiteral("%1 完成，数据集 %2，共 %3 条")
            .arg(operation)
            .arg(result.value(QStringLiteral("dataset")).toString())
            .arg(result.value(QStringLiteral("sample_count")).toVariant().toLongLong()));
}

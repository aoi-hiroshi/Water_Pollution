#include "classificationcharts.h"
#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
QString number(const QJsonValue &value) {
    return value.isDouble() ? QString::number(value.toDouble(), 'g', 5) : QStringLiteral("NA");
}
void axes(QPainter &p, const QRectF &area, double low, double high) {
    p.setPen(QColor("#94a3b8")); p.drawRect(area);
    p.setPen(QColor("#334155"));
    p.drawText(QRectF(area.left()-68,area.top()-8,63,20),Qt::AlignRight,QString::number(high,'g',4));
    p.drawText(QRectF(area.left()-68,area.bottom()-10,63,20),Qt::AlignRight,QString::number(low,'g',4));
}
void trends(QPainter &p, const QJsonObject &result) {
    const auto fields=result.value("features").toArray();
    const auto before=result.value("chart_before").toArray(), after=result.value("chart_after").toArray();
    p.drawText(QRect(20,12,1160,26),Qt::AlignCenter,
               QStringLiteral("清洗前后对比：灰色原始 / 蓝色修复；横轴为样本顺序（最多300个抽样点）"));
    for (int index=0; index<fields.size() && index<10; ++index) {
        const auto feature=fields.at(index).toObject(); const auto key=feature.value("field").toString();
        const QRectF area(90+(index%2)*590,85+(index/2)*160,490,110);
        double low=std::numeric_limits<double>::infinity(), high=-low;
        for (const auto &rows : {before,after}) for (const auto &value : rows) {
            const auto cell=value.toObject().value(key);
            if (cell.isDouble() && std::isfinite(cell.toDouble())) {
                low=std::min(low,cell.toDouble()); high=std::max(high,cell.toDouble());
            }
        }
        p.setPen(QColor("#0f172a"));
        p.drawText(QRectF(area.left(),area.top()-30,area.width(),24),Qt::AlignLeft,
                   feature.value("label").toString()+" ("+feature.value("unit").toString()+")");
        if (!std::isfinite(low)) { p.drawText(area,Qt::AlignCenter,QStringLiteral("整列为空")); continue; }
        if (low==high) { low-=std::max(1.0,std::abs(low)*.05); high+=std::max(1.0,std::abs(high)*.05); }
        axes(p,area,low,high);
        for (int series=0; series<2; ++series) {
            const auto rows=series==0 ? before : after;
            p.setPen(QPen(series==0 ? QColor("#94a3b8") : QColor("#2563eb"),series==0 ? 1.5 : 2));
            QPainterPath path; bool connected=false;
            for (int i=0; i<rows.size(); ++i) {
                const auto cell=rows.at(i).toObject().value(key);
                if (!cell.isDouble()) { connected=false; continue; }
                const QPointF point(area.left()+area.width()*i/std::max(1,rows.size()-1),
                    area.bottom()-area.height()*(cell.toDouble()-low)/(high-low));
                if (connected) path.lineTo(point); else path.moveTo(point);
                connected=true; p.drawEllipse(point,1.8,1.8);
            }
            p.drawPath(path);
        }
    }
}
void heatmap(QPainter &p, const QJsonObject &result) {
    const auto fields=result.value("features").toArray(), matrix=result.value("correlation").toArray();
    p.drawText(QRect(20,12,1160,28),Qt::AlignCenter,
        result.value("parameters").toObject().value("method").toString()+
        QStringLiteral(" 相关性；红色正相关 / 蓝色负相关 / 灰色 NA（不是零相关）"));
    for (int i=0; i<10 && i<fields.size(); ++i) {
        const QString label=fields.at(i).toObject().value("label").toString();
        p.setPen(QColor("#0f172a"));
        p.drawText(QRect(30,100+i*65,160,65),Qt::AlignRight|Qt::AlignVCenter,label);
        p.drawText(QRect(200+i*95,55,95,40),Qt::AlignCenter,label);
        for (int j=0; j<10; ++j) {
            const auto value=matrix.at(i).toArray().at(j); QColor color("#e2e8f0");
            if (value.isDouble()) {
                const double v=value.toDouble(), strength=std::abs(v);
                color=v>=0 ? QColor(255,255-int(150*strength),255-int(150*strength))
                           : QColor(255-int(150*strength),255-int(110*strength),255);
            }
            const QRect cell(200+j*95,100+i*65,95,65);
            p.fillRect(cell,color); p.setPen(Qt::white); p.drawRect(cell);
            p.setPen(QColor("#0f172a")); p.drawText(cell,Qt::AlignCenter,number(value));
        }
    }
}
void distributions(QPainter &p, const QJsonObject &result) {
    const auto groups=result.value("groups").toArray();
    const auto params=result.value("parameters").toObject();
    const bool box=params.value("view").toString()=="boxplot";
    const int shown=std::min(12,groups.size());
    p.drawText(QRect(20,12,1160,28),Qt::AlignCenter,
        params.value("feature").toString()+(box ? QStringLiteral(" 箱线图（须内范围，异常值数量见摘要）")
        : QStringLiteral(" 直方图（频数，各公司使用相同边界）"))+
        (groups.size()>12 ? QStringLiteral("；图中仅展示前12家公司，完整统计见摘要") : QString()));
    for (int i=0; i<shown; ++i) {
        const auto group=groups.at(i).toObject(); const auto bins=group.value("histogram").toArray();
        const QRectF area(90+(i%2)*590,95+(i/2)*180,490,120);
        p.setPen(QColor("#0f172a"));
        p.drawText(QRectF(area.left(),area.top()-30,area.width(),24),Qt::AlignLeft,
            group.value("company_name").toString()+QStringLiteral(" 有效 %1 / 空值 %2")
            .arg(group.value("valid_count").toInt()).arg(group.value("missing_count").toInt()));
        if (!group.value("median").isDouble()) { p.drawText(area,Qt::AlignCenter,QStringLiteral("无有效数据")); continue; }
        if (box) {
            double low=group.value("min").toDouble(),high=group.value("max").toDouble();
            if (low==high) { low-=1; high+=1; }
            axes(p,area,low,high);
            auto y=[&](const char *key) { return area.bottom()-area.height()*(group.value(key).toDouble()-low)/(high-low); };
            const double center=area.center().x();
            p.setPen(QPen(QColor("#2563eb"),2));
            p.drawLine(QPointF(center,y("lower_whisker")),QPointF(center,y("upper_whisker")));
            p.fillRect(QRectF(center-55,y("q75"),110,y("q25")-y("q75")),QColor("#bfdbfe"));
            p.drawRect(QRectF(center-55,y("q75"),110,y("q25")-y("q75")));
            for (const char *key : {"lower_whisker","upper_whisker","median"})
                p.drawLine(QPointF(center-55,y(key)),QPointF(center+55,y(key)));
        } else {
            double maxCount=1;
            for (const auto &bin : bins) maxCount=std::max(maxCount,bin.toObject().value("count").toDouble());
            axes(p,area,0,maxCount); p.setPen(Qt::NoPen); p.setBrush(QColor("#3b82f6"));
            for (int b=0; b<bins.size(); ++b) {
                const double height=area.height()*bins.at(b).toObject().value("count").toDouble()/maxCount;
                p.drawRect(QRectF(area.left()+area.width()*b/bins.size(),area.bottom()-height,
                                 area.width()/bins.size(),height));
            }
            p.setPen(QColor("#334155"));
            if (!bins.isEmpty()) p.drawText(QRectF(area.left(),area.bottom()+4,area.width(),22),Qt::AlignCenter,
                number(bins.first().toObject().value("lower"))+" — "+number(bins.last().toObject().value("upper")));
        }
    }
}
}
QPixmap renderClassificationChart(const QJsonObject &result) {
    const QString operation=result.value("operation").toString();
    const int groups=std::min(12,result.value("groups").toArray().size());
    const int height=operation=="distribution" ? std::max(340,80+((groups+1)/2)*180) : 900;
    QPixmap pixmap(1200,height); pixmap.fill(Qt::white);
    QPainter painter(&pixmap); painter.setRenderHint(QPainter::Antialiasing);
    if (operation=="correlation") heatmap(painter,result);
    else if (operation=="distribution") distributions(painter,result);
    else trends(painter,result);
    return pixmap;
}

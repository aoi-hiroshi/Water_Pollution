// A local mock HTTP backend exercises the real Qt widgets/services. This is
// NOT a replacement for Linux Muduo/MySQL end-to-end deployment validation.
#include "datapage.h"
#include "tracepage.h"
#include "classificationcharts.h"
#include <QApplication>
#include <QAbstractButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
const QStringList fields={"temperature","ph","cod","nh3n","tp","water_level","orp","conductivity","dissolved_oxygen","turbidity"};
const QStringList labels={QStringLiteral("水温"),"pH","COD",QStringLiteral("氨氮"),QStringLiteral("总磷"),
    QStringLiteral("液位"),"ORP",QStringLiteral("电导率"),QStringLiteral("溶解氧"),QStringLiteral("浊度")};
QJsonObject fixture(const QString &operation,QJsonObject parameters=QJsonObject()) {
    QJsonArray features,before,after,matrix;
    for(int i=0;i<10;++i) {
        features.append(QJsonObject{{"field",fields[i]},{"label",labels[i]},{"unit","test"},
            {"missing_before",i==2 ? 1 : 0},{"missing_after",0},{"filled_count",i==2 ? 1 : 0},
            {"outlier_count",0},{"mean",2.5},{"std",1.118},{"min",1},{"max",4},{"median",2.5}});
        QJsonArray row;
        for(int j=0;j<10;++j)row.append(i==4 || j==4 ? QJsonValue(QJsonValue::Null) : QJsonValue(i==j ? 1.0 : .5));
        matrix.append(row);
    }
    for(int i=0;i<4;++i) {
        QJsonObject row{{"id",i+1},{"company_id",1}};
        for(const auto &field:fields)row.insert(field,i+1);
        after.append(row);if(i==0)row.insert("cod",QJsonValue(QJsonValue::Null));before.append(row);
    }
    QJsonArray bins;for(int i=0;i<5;++i)bins.append(QJsonObject{{"lower",1+i*.6},{"upper",1+(i+1)*.6},{"count",i==2 ? 0 : 1}});
    QJsonObject group{{"company_id",1},{"company_name","Fixture One"},{"valid_count",4},{"missing_count",0},
        {"outlier_count",0},{"min",1},{"max",4},{"q10",1.3},{"q25",1.75},{"median",2.5},{"q75",3.25},
        {"q90",3.7},{"lower_whisker",1},{"upper_whisker",4},{"histogram",bins}};
    if(!parameters.contains("method"))parameters.insert("method","pearson");
    if(!parameters.contains("view"))parameters.insert("view","histogram");
    if(!parameters.contains("feature"))parameters.insert("feature","cod");
    const bool persisted=parameters.value("persist").toBool();
    return {{"operation",operation},{"company_id",1},{"company_name","Fixture One"},
        {"dataset",parameters.value("dataset").toString("train_data")},{"sample_count",4},
        {"features",features},{"preview_before",before},{"preview_after",after},{"chart_before",before},
        {"chart_after",after},{"groups",QJsonArray{group}},{"correlation",matrix},{"high_correlations",QJsonArray()},
        {"warnings",QJsonArray{QStringLiteral("模拟接口测试数据，不代表真实业务分析。")}},
        {"parameters",parameters},{"rule","mock_rule"},{"input_cleaning_run_id",parameters.value("cleaning_run_id").toInt()},
        {"persisted",persisted},{"cleaning_run_id",persisted ? 100 : 0}};
}
void waitFor(const std::function<bool()> &condition) {
    QElapsedTimer timer;timer.start();
    while(!condition() && timer.elapsed()<5000) {QApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}
    CHECK(condition());
}
QPushButton *button(QWidget &page,const QString &text) {
    for(auto *candidate:page.findChildren<QPushButton*>())if(candidate->text()==text)return candidate;
    throw std::runtime_error("button not found");
}
struct MockBackend {
    QTcpServer server;
    int requests=0;
    QString lastPath;
    QJsonObject lastBody;
    MockBackend() {
        CHECK(server.listen(QHostAddress::LocalHost,0));
        QObject::connect(&server,&QTcpServer::newConnection,[this] {
            auto *socket=server.nextPendingConnection();
            QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            QObject::connect(socket,&QTcpSocket::readyRead,[this,socket] {
                QByteArray bytes=socket->property("buffer").toByteArray()+socket->readAll();socket->setProperty("buffer",bytes);
                const int headerEnd=bytes.indexOf("\r\n\r\n");if(headerEnd<0)return;
                int contentLength=0;
                for(const auto &line:bytes.left(headerEnd).split('\n'))
                    if(line.toLower().startsWith("content-length:"))contentLength=line.mid(15).trimmed().toInt();
                if(bytes.size()<headerEnd+4+contentLength)return;
                lastPath=QString::fromUtf8(bytes.left(bytes.indexOf("\r\n")).split(' ')[1].split('?')[0]);
                lastBody=QJsonDocument::fromJson(bytes.mid(headerEnd+4,contentLength)).object();++requests;
                QJsonValue data;
                if(lastPath=="/api/v1/companies")data=QJsonArray{QJsonObject{{"company_id",1},{"company_name","Fixture One"},{"company_code","F1"}}};
                else if(lastPath=="/api/v1/data/overview") {
                    const auto f=fixture("missing");data=QJsonObject{{"company",QJsonObject{{"company_name","Fixture One"},{"company_code","F1"}}},
                        {"dataset","train_data"},{"total_rows",4},{"preview_rows",f.value("preview_before")},{"summary",f.value("features")}};
                } else if(lastPath=="/api/v1/inference/classify") {
                    const QJsonObject candidate{{"rank",1},{"company_id",1},{"company_name","Fixture One"},{"company_code","F1"},{"probability",1}};
                    data=QJsonObject{{"sample_id",1},{"dataset",lastBody.value("dataset")},{"cleaning_run_id",lastBody.value("cleaning_run_id")},
                        {"predicted",candidate},{"candidates",QJsonArray{candidate}},{"model",QJsonObject{{"name","mock"},{"version","test"}}}};
                } else data=fixture(lastPath.section('/',-1),lastBody);
                const auto body=QJsonDocument(QJsonObject{{"code",200},{"message","mock success"},{"data",data}}).toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body);
                socket->disconnectFromHost();
            });
        });
    }
};
}
int main(int argc,char** argv) {
    std::cout<<std::unitbuf;
    std::cout<<"[INFO] Qt smoke startup\n";
    QApplication app(argc,argv);
    // Use a stable style for CI chart snapshots.
    app.setStyle(QStringLiteral("Fusion"));
    QTimer watchdog;
    QObject::connect(&watchdog,&QTimer::timeout,[] {
        for(auto *widget:QApplication::topLevelWidgets()) {
            if(auto *dialog=qobject_cast<QMessageBox*>(widget)) {
                if(dialog->isVisible() && !dialog->text().contains(QStringLiteral("按刚才预览"))) {
                    std::cerr<<"[ERROR] unexpected dialog: "<<dialog->text().toStdString()<<'\n';
                    dialog->done(QMessageBox::Ok);
                }
            }
        }
    });
    watchdog.start(200);
    try {
        MockBackend backend;
        qputenv("WATER_API_BASE_URL",("http://127.0.0.1:"+QString::number(backend.server.serverPort())).toUtf8());
        DataPage page;page.resize(1500,1000);page.show();
        auto *clean=button(page,QStringLiteral("预览异常值清洗"));
        waitFor([&]{return backend.requests>=2 && clean->isEnabled();});
        auto *table=page.findChild<QTableWidget*>();CHECK(table && table->item(0,3)->text()==QStringLiteral("空值"));
        std::cout<<"[PASS] Qt company/overview HTTP and NULL display\n";
        for(const auto &text: {QStringLiteral("空值检测与修复"),QStringLiteral("预览异常值清洗"),
            QStringLiteral("生成分布图"),QStringLiteral("生成相关性图")}) {
            const int before=backend.requests;auto *action=button(page,text);action->click();
            waitFor([&]{return backend.requests>before && action->isEnabled();});
            CHECK(backend.lastPath.startsWith("/api/v1/data/classification/"));
            CHECK(backend.lastBody.value("company_id").toInt()==1 && !backend.lastBody.value("persist").toBool());
        }
        std::cout<<"[PASS] four Qt analysis buttons submit HTTP JSON\n";
        std::cout<<"[INFO] prepare cleaning save preview\n";
        clean->click();waitFor([&]{return clean->isEnabled();});
        auto *save=button(page,QStringLiteral("确认并保存清洗新版本"));CHECK(save->isEnabled());
        QTimer::singleShot(100,[] {
            for(auto *widget:QApplication::topLevelWidgets())
                if(auto *dialog=qobject_cast<QDialog*>(widget)) {
                    if(!dialog->isVisible() || dialog->objectName()!="classificationSaveConfirmation")continue;
                    std::cout<<"[INFO] accepting save confirmation\n";
                    if(auto *buttons=dialog->findChild<QDialogButtonBox*>())buttons->button(QDialogButtonBox::Yes)->click();
                }
        });
        std::cout<<"[INFO] open save confirmation\n";
        const int count=backend.requests;save->click();
        waitFor([&]{return backend.requests>count && clean->isEnabled();});
        CHECK(backend.lastPath=="/api/v1/data/classification/clean");
        CHECK(backend.lastBody.value("persist").toBool() && !save->isEnabled());
        bool selected=false;for(auto *spin:page.findChildren<QSpinBox*>())if(spin->minimum()==0)selected=spin->value()==100;CHECK(selected);
        std::cout<<"[PASS] Qt confirms save and selects returned version\n";
        TracePage trace;
        trace.findChild<QComboBox*>()->setCurrentIndex(1);
        trace.findChild<QSpinBox*>()->setValue(100);
        auto *start=button(trace,QStringLiteral("发起溯源"));const int previous=backend.requests;start->click();
        waitFor([&]{return backend.requests>previous && start->isEnabled();});
        CHECK(backend.lastBody.value("cleaning_run_id").toInt()==100 && backend.lastBody.value("dataset").toString()=="train_data");
        CHECK(trace.findChild<QTableWidget*>()->rowCount()==1);
        std::cout<<"[PASS] Qt classification sends selected cleaning version\n";
        for(const auto &operation: {QString("missing"),QString("clean"),QString("correlation")}) {
            const auto pixmap=renderClassificationChart(fixture(operation));CHECK(!pixmap.isNull());
            CHECK(pixmap.save(operation+".png","PNG"));
        }
        for(const auto &view: {QString("boxplot"),QString("histogram"),QString("comparison")}) {
            const auto pixmap=renderClassificationChart(fixture("distribution",{{"view",view}}));
            CHECK(!pixmap.isNull() && pixmap.save(view+".png","PNG"));
        }
        std::cout<<"[PASS] six Qt chart variants render and export PNG\n";return 0;
    }catch(const std::exception& e){std::cerr<<"[FAIL] "<<e.what()<<'\n';return 1;}
}

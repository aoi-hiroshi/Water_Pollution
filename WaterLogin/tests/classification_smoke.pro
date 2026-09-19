QT += core gui widgets network
CONFIG += console c++11 utf8_source
CONFIG -= app_bundle
TEMPLATE = app
TARGET = classification_qt_smoke
INCLUDEPATH += ..
SOURCES += classification_smoke.cpp ../apiclient.cpp ../dataservice.cpp ../datapage.cpp \
           ../classificationcharts.cpp ../traceservice.cpp ../tracepage.cpp
HEADERS += ../apiclient.h ../dataservice.h ../datapage.h ../classificationcharts.h \
           ../traceservice.h ../tracepage.h

QT += core gui quick quickcontrols2 testlib
CONFIG += c++17 testcase
TARGET = oma_spend_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/theme.h ../src/translator.h
SOURCES += oma_spend_tests.cpp ../src/backend.cpp ../src/theme.cpp ../src/translator.cpp

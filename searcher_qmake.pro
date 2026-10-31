QT += core gui widgets sql
CONFIG += c++17
TEMPLATE = app
TARGET = Searcher

SOURCES += main.cpp mainwindow.cpp
HEADERS += mainwindow.h
FORMS   += mainwindow.ui

msvc {
    QMAKE_CXXFLAGS += /utf-8
}

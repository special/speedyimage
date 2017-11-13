TEMPLATE = lib
CONFIG += qt plugin
QT += qml quick

DESTDIR = imports/SpeedyImage
TARGET = qmlspeedyimageplugin
SOURCES += plugin.cpp \
    speedyimage.cpp
HEADERS += speedyimage.h \
    speedyimage_p.h

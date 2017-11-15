TEMPLATE = lib
CONFIG += qt plugin
QT += qml quick

DESTDIR = imports/SpeedyImage
TARGET = qmlspeedyimageplugin
SOURCES += plugin.cpp \
    speedyimage.cpp \
    imageloader.cpp \
    imagetexturecache.cpp
HEADERS += speedyimage.h \
    speedyimage_p.h \
    imageloader.h \
    imageloader_p.h \
    imagetexturecache.h \
    imagetexturecache_p.h

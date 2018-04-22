TEMPLATE = lib
CONFIG += qt plugin
QT += qml quick

TARGET = qmlspeedyimageplugin
TARGETPATH = SpeedyImage

QML_FILES = qmldir

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

load(qml_plugin)

QT += core gui qml quick quickcontrols2

CONFIG += c++17 release
TARGET = keyglow
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/device.h \
    src/keyinput.h \
    src/sceneimages.h \
    src/lighting.h \
    src/streamer.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/device.cpp \
    src/keyinput.cpp \
    src/sceneimages.cpp \
    src/lighting.cpp \
    src/streamer.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc

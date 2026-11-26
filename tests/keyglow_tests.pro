QT += core gui quick quickcontrols2 testlib
CONFIG += c++17 testcase
TARGET = keyglow_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/device.h ../src/keyinput.h ../src/lighting.h ../src/sceneimages.h ../src/theme.h ../src/translator.h
SOURCES += keyglow_tests.cpp ../src/backend.cpp ../src/device.cpp ../src/keyinput.cpp ../src/lighting.cpp ../src/sceneimages.cpp ../src/theme.cpp ../src/translator.cpp

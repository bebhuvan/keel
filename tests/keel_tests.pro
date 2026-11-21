QT += core gui qml quick dbus network testlib
CONFIG += c++17 testcase
QMAKE_CXXFLAGS += -Wall -Wextra -Werror
TARGET = keel_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/vitals.h ../src/apps.h ../src/controls.h ../src/theme.h ../src/hyprland.h ../src/storage.h
SOURCES += keel_tests.cpp ../src/vitals.cpp ../src/apps.cpp ../src/controls.cpp ../src/theme.cpp \
    ../src/hyprland.cpp ../src/storage.cpp

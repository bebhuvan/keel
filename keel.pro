QT += core gui qml quick dbus network

CONFIG += c++17 release qtquickcompiler
QMAKE_CXXFLAGS += -Wall -Wextra -Werror
# makepkg's hardening flags (fortify, stack protection, RELRO, PIE) when the
# package is built; empty in a plain bin/build.
QMAKE_CXXFLAGS += $$(CPPFLAGS) $$(CXXFLAGS)
QMAKE_LFLAGS += $$(LDFLAGS)
TARGET = keel
TEMPLATE = app

HEADERS += \
    src/vitals.h \
    src/apps.h \
    src/controls.h \
    src/backend.h \
    src/theme.h \
    src/hyprland.h \
    src/storage.h

SOURCES += \
    src/main.cpp \
    src/vitals.cpp \
    src/apps.cpp \
    src/controls.cpp \
    src/backend.cpp \
    src/theme.cpp \
    src/hyprland.cpp \
    src/storage.cpp

RESOURCES += src/resources.qrc

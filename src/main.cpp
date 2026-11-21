// keel — a calm command center for an Omarchy laptop: what the machine is
// doing in plain words, the switches you reach for, and the apps using it.

#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QUrl>

#include "backend.h"
#include "storage.h"
#include "theme.h"

int main(int argc, char *argv[]) {
    // Speed and memory, measured: the GTK platform theme costs about 250 ms
    // and 40 MB at start, and the input-method module another 50 ms. Keel
    // has no text fields and no file dialogs, so it needs neither.
    qputenv("QT_QPA_PLATFORMTHEME", "");
    qunsetenv("QT_IM_MODULE");
    // A flat window of text and rectangles draws as fast in software, and
    // skipping the GPU driver saves about 40 MB.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);

    QGuiApplication app(argc, argv);
    app.setApplicationName("keel");
    app.setApplicationVersion("0.1.0");

    // Matches the window to keel.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("keel");
    app.setWindowIcon(QIcon::fromTheme("keel"));
    app.setFont(QFont("JetBrainsMono Nerd Font", 10));

    Theme theme;
    Backend backend;
    Storage storage;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("storage", &storage);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}

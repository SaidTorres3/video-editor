#include "app_settings.h"
#include "qt_mainwindow.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

#include <curl/curl.h>

// Build as a GUI subsystem app (WIN32). We provide WinMain to avoid a symbol
// clash with the vendored RNNoise static library, which also contains a `main`.
#include <windows.h>

static void applyDarkTheme(QApplication& app)
{
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette p;
    p.setColor(QPalette::Window, QColor(24, 26, 30));
    p.setColor(QPalette::WindowText, QColor(235, 235, 235));
    p.setColor(QPalette::Base, QColor(16, 18, 20));
    p.setColor(QPalette::AlternateBase, QColor(26, 29, 34));
    p.setColor(QPalette::ToolTipBase, QColor(235, 235, 235));
    p.setColor(QPalette::ToolTipText, QColor(24, 26, 30));
    p.setColor(QPalette::Text, QColor(235, 235, 235));
    p.setColor(QPalette::Button, QColor(30, 33, 38));
    p.setColor(QPalette::ButtonText, QColor(235, 235, 235));
    p.setColor(QPalette::BrightText, QColor(255, 0, 0));
    p.setColor(QPalette::Highlight, QColor(70, 130, 180));
    p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    app.setPalette(p);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    LoadSettings();
    curl_global_init(CURL_GLOBAL_DEFAULT);

    int argc = __argc;
    char** argv = __argv;
    QApplication app(argc, argv);
    applyDarkTheme(app);

    MainWindow w;
    w.resize(1200, 760);
    w.show();

    if (argc > 1)
        w.loadVideoFile(QString::fromLocal8Bit(argv[1]));

    const int code = app.exec();
    curl_global_cleanup();
    return code;
}

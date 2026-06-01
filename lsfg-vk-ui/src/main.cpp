/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <QIcon>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>

#include "backend.hpp"

using namespace lsfgvk::ui;

int main(int argc, char* argv[]) {
    // Follow the desktop GTK theme: load the gtk3 platform theme so Qt's palette is
    // populated from the user's GTK colors, and use Fusion, which honors that palette.
    // The custom QML binds to SystemPalette, so the UI tracks the GTK theme live.
    if (!qEnvironmentVariableIsSet("LSFGVK_UI_KEEP_PLATFORMTHEME"))
        qputenv("QT_QPA_PLATFORMTHEME", "gtk3");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");

    const QGuiApplication app(argc, argv);
    QGuiApplication::setWindowIcon(QIcon(":/rsc/gay.pancake.lsfg-vk-ui.png"));
    QGuiApplication::setApplicationName("lsfg-vk-ui");
    QGuiApplication::setApplicationDisplayName("lsfg-vk-ui");

    QQmlApplicationEngine engine;
    Backend backend;

    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load("qrc:/rsc/UI.qml");

    return QGuiApplication::exec();
}

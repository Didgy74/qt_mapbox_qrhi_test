#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include <QLoggingCategory>

#include <rhi/qrhi.h>
#include <QFile>

#include "tileloader.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    qmlRegisterType<TileLoader>("com.example", 1, 0, "TileLoader");

#if defined(Q_OS_ANDROID)
    qputenv("QSG_RHI_BACKEND", "vulkan");
#elif defined(Q_OS_WINDOWS)
    qputenv("QSG_RHI_BACKEND", "d3d12");
#endif
    QLoggingCategory::setFilterRules(QStringLiteral("qt.scenegraph.general=true"));

    QQmlApplicationEngine engine;

    engine.load("qrc:/Main.qml");

    return app.exec();
}

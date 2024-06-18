#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include <QLoggingCategory>

#include <rhi/qrhi.h>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    //qputenv("QSG_RHI_BACKEND", "d3d12");
    //QLoggingCategory::setFilterRules(QStringLiteral("qt.scenegraph.general=true"));

    QQmlApplicationEngine engine;

    const QUrl url(QStringLiteral("qrc:/Main.qml"));

    /*
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);
    */
    engine.load(url);





    return app.exec();
}

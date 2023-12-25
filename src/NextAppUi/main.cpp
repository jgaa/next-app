#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQmlContext>

#include "ServerComm.h"
#include "MainTreeModel.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    nextapp::pb::Nextapp::Client cli{&app};
    auto info = cli.GetServerInfo({});

    qRegisterProtobufTypes();

    ServerComm sc;
    sc.start();

    MainTreeModel tree_model;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("sc", &sc);
    engine.rootContext()->setContextProperty("treeModel", &tree_model);
    const QUrl url(u"qrc:/NextAppUi/Main.qml"_qs);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}

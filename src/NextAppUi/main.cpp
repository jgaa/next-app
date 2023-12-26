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

    {
        ::nextapp::pb::Node root, root2, child1, child2, child3;
        root.setUuid(QUuid::createUuid().toString(QUuid::StringFormat::WithoutBraces));
        root.setName("root");

        tree_model.addNode(root, {}, {});

        root2.setUuid(QUuid::createUuid().toString(QUuid::StringFormat::WithoutBraces));
        root2.setName("root2");
        tree_model.addNode(root2, {}, {});


        child1.setUuid(QUuid::createUuid().toString(QUuid::StringFormat::WithoutBraces));
        child1.setName("child2");
        tree_model.addNode(child1, QUuid{root.uuid()}, {});

        child2.setUuid(QUuid::createUuid().toString(QUuid::StringFormat::WithoutBraces));
        child2.setName("child1");

        tree_model.addNode(child2, {}, QUuid{child1.uuid()});

        child2.setUuid(QUuid::createUuid().toString(QUuid::StringFormat::WithoutBraces));
        child2.setName("child3");
        tree_model.addNode(child2, QUuid{child1.uuid()}, {});
    }

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

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QIcon>
#include <QAbstractItemModelTester>

#include "ServerComm.h"
#include "MainTreeModel.h"
#include "nextapp.qpb.h"

#include "logging.h"

extern void qml_register_types_nextapp_pb();

using namespace std;

namespace {
optional<logfault::LogLevel> toLogLevel(string_view name) {
    if (name.empty() || name == "off" || name == "false") {
        return {};
    }

    if (name == "debug") {
        return logfault::LogLevel::DEBUGGING;
    }

    if (name == "trace") {
        return logfault::LogLevel::TRACE;
    }

    return logfault::LogLevel::INFO;
}
} // anon ns

int main(int argc, char *argv[])
{
    qRegisterProtobufTypes();
    volatile auto registration = &qml_register_types_nextapp_pb;
    Q_UNUSED(registration);
    std::string log_level_qt = "trace";

    QGuiApplication app(argc, argv);

    QGuiApplication::setOrganizationName("TheLastViking");
#ifdef DEVEL_SETTINGS
    QGuiApplication::setApplicationName("nextapp-devel");
#else
    QGuiApplication::setApplicationName("nextapp");
#endif
    QGuiApplication::setApplicationVersion(NEXTAPP_VERSION);
    QGuiApplication::setWindowIcon(QIcon(":/qt/qml/NextAppUi/icons/nextapp.svg"));

    QCommandLineParser parser;
    parser.setApplicationDescription("Personal organizer");
    parser.addHelpOption();
    parser.addVersionOption();
    //parser.addPositionalArgument("", QGuiApplication::translate("main", "Initial directory"),"[path]");
    parser.process(app);
    //const auto args = parser.positionalArguments();

    if (auto level = toLogLevel(log_level_qt)) {
        logfault::LogManager::Instance().AddHandler(
            make_unique<logfault::QtHandler>(*level));
    }

    nextapp::pb::Nextapp::Client cli{&app};
    auto info = cli.GetServerInfo({});

    QQmlApplicationEngine engine;
    engine.loadFromModule("NextAppUi", "Main");
    if (engine.rootObjects().isEmpty()) {
        qWarning() << "Failed to initialize engine!";
        return -1;
    }

    {
        auto server_comm =  engine.singletonInstance<ServerComm*>(
            "NextAppUi","ServerComm");
        assert(server_comm);
        server_comm->start();
    }

    {
        auto tree = engine.singletonInstance<MainTreeModel*>("NextAppUi","MainTreeModel");
        assert(tree);

#ifdef WITH_TREE_MODEL_TESTING
        new QAbstractItemModelTester{tree, QAbstractItemModelTester::FailureReportingMode::Fatal, &engine};
#endif

        tree->start();

        // auto rscope = tree->resetScope();

        // ::nextapp::pb::Node first, second, child1, child2, child3;
        // first.setUuid(QUuid::createUuid().toString(QUuid::StringFormat::WithoutBraces));
        // first.setName("first");

        // tree->addNode(first, {}, {});

        // second.setUuid(QUuid::createUuid().toString(QUuid::StringFormat::WithoutBraces));
        // second.setName("second");
        // tree->addNode(second, {}, {});


        // child1.setUuid(QUuid::createUuid().toString(QUuid::StringFormat::WithoutBraces));
        // child1.setName("child2");
        // tree->addNode(child1, QUuid{first.uuid()}, {});

        // child2.setUuid(QUuid::createUuid().toString(QUuid::StringFormat::WithoutBraces));
        // child2.setName("child1");

        // tree->addNode(child2, {}, QUuid{child1.uuid()});

        // child2.setUuid(QUuid::createUuid().toString(QUuid::StringFormat::WithoutBraces));
        // child2.setName("child3");
        // tree->addNode(child2, QUuid{child1.uuid()}, {});

        // tree->dump();
    }

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    auto ret = app.exec();


    if (auto server_comm =  engine.singletonInstance<ServerComm*>(
            "NextAppUi","ServerComm")) {
        LOG_DEBUG_N << "Shutting down gRPC";
        server_comm->stop();
    }

    LOG_DEBUG_N << "Exiting the app";
    return ret;
}

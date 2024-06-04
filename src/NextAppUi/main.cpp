#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QIcon>
#include <QAbstractItemModelTester>

#include "ServerComm.h"
#include "MainTreeModel.h"
#include "DaysModel.h"
#include "DayColorModel.h"
#include "ActionsModel.h"
#include "WorkSessionsModel.h"
#include "NextAppCore.h"
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
    //qRegisterProtobufTypes();
    volatile auto registration = &qml_register_types_nextapp_pb;
    Q_UNUSED(registration);
    std::string log_level_qt =
#ifdef _DEBUG
        "trace";
#else
        "info";
#endif

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
    parser.addOption({{"L", "log-level"}, "Set the log level for the log-file to one of: off, debug, trace, info",
        "log-level", "info"});
    parser.addOption({{"C", "log-level-console"}, "Set the log level to the console to one of: off, debug, trace, info",
        "log-level-console", "info"});
    parser.addOption({"log-file", "Path to the log file", "log-file"});

    //parser.addPositionalArgument("", QGuiApplication::translate("main", "Initial directory"),"[path]");
    parser.process(app);
    //const auto args = parser.positionalArguments();
    if (parser.isSet("log-level-console")) {
        log_level_qt = parser.value("log-level-console").toStdString();
    }

    if (auto level = toLogLevel(log_level_qt)) {
        logfault::LogManager::Instance().AddHandler(
            make_unique<logfault::QtHandler>(*level));
    }

    if (parser.isSet("log-file")) {
        if (auto path = parser.value("log-file").toStdString(); !path.empty()) {
            if (const auto level = toLogLevel(parser.value("log-level").toStdString()); level.has_value()) {
                logfault::LogManager::Instance().AddHandler(make_unique<logfault::StreamHandler>(path,*level, true));
            }
        }
    }

    nextapp::pb::Nextapp::Client cli{&app};
    auto info = cli.GetServerInfo({});

    NextAppCore core;
    ServerComm comms;

    auto& engine = NextAppCore::engine();

    qmlRegisterSingletonInstance<NextAppCore>("Nextapp.Models", 1, 0, "NaCore", &core);
    qmlRegisterSingletonInstance<ServerComm>("Nextapp.Models", 1, 0, "NaComm", &comms);

    engine.loadFromModule("NextAppUi", "Main");
    if (engine.rootObjects().isEmpty()) {
        qWarning() << "Failed to initialize engine!";
        return -1;
    }

    {
        auto tree = engine.singletonInstance<MainTreeModel*>("NextAppUi","MainTreeModel");
        assert(tree);

#ifdef WITH_TREE_MODEL_TESTING
        new QAbstractItemModelTester{tree, QAbstractItemModelTester::FailureReportingMode::Fatal, &engine};
#endif

        tree->start();
    }

    {
        auto days = engine.singletonInstance<DaysModel*>("NextAppUi","DaysModel");
        assert(days);
        days->start();
    }

    {
        auto actions = engine.singletonInstance<ActionsModel*>("NextAppUi","ActionsModel");
        assert(actions);
        actions->start();
    }

    {
        auto work_sessions = engine.singletonInstance<WorkSessionsModel*>("NextAppUi","WorkSessionsModel");
        assert(work_sessions);
        work_sessions->start();
    }

    {
        auto colors = engine.singletonInstance<DayColorModel*>("NextAppUi","DayColorModel");
        assert(colors);
        colors->start();
    }

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    NextAppCore::instance()->modelsAreCreated();

    auto ret = app.exec();

    if (auto server_comm =  engine.singletonInstance<ServerComm*>(
            "NextAppUi","ServerComm")) {
        LOG_DEBUG_N << "Shutting down gRPC";
        server_comm->stop();
    }

    LOG_DEBUG_N << "Exiting the app";
    return ret;
}

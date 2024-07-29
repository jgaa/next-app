#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QIcon>
#include <QAbstractItemModelTester>
#include <QQuickStyle>
#include <QSslSocket>

#include "ServerComm.h"
#include "MainTreeModel.h"
#include "DaysModel.h"
#include "DayColorModel.h"
#include "ActionsModel.h"
#include "WorkSessionsModel.h"
#include "NextAppCore.h"
#include "ActionInfoCache.h"
#include "ActionCategoriesModel.h"
#include "TimeBoxActionsModel.h"
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

// Must match uiStyle in PrefSettings.qml
constexpr auto styles = to_array<string_view>(
    {"", "Basic", "Imagine", "Fusion", "Material",
//        "MacOS", "iOS",
     "Universal"});

// Must match uiScale in PrefSettings.qml
constexpr auto scales = to_array<string_view>(
    {
        "",
        "0.5",
        "0.75",
        "0.88",
        "1.0",
        "1.1",
        "1.2",
        "1.3",
        "1.5",
        "1.75",
        "2.0",
    });

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
    qDebug() << "Device supports OpenSSL: " << QSslSocket::supportsSsl();

    QGuiApplication app(argc, argv);

    // Allow us to use an alternative config-file for testing
    if (const auto* org = getenv("NEXTAPP_ORG")){
         QGuiApplication::setOrganizationName(org);
    } else {
        QGuiApplication::setOrganizationName("TheLastViking");
    }
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

// #ifdef __ANDROID__
//         logfault::LogManager::Instance().AddHandler(
//             make_unique<logfault::AndroidHandler>("next-app", *level));
// #endif

    }

    if (parser.isSet("log-file")) {
        if (auto path = parser.value("log-file").toStdString(); !path.empty()) {
            if (const auto level = toLogLevel(parser.value("log-level").toStdString()); level.has_value()) {
                logfault::LogManager::Instance().AddHandler(make_unique<logfault::StreamHandler>(path,*level, true));
            }
        }
    }

    {
        QSettings settings;

        if (!settings.contains("UI/style")) {
            settings.setValue("UI/style", 0);
        }

        auto style = styles.at(settings.value("UI/style").toInt());
        auto scale = scales.at(settings.value("UI/scale").toInt());

        if (const auto level = settings.value("logging/level", 0).toInt()) {
            if (auto path = settings.value("logging/path", "").toString().toStdString(); !path.empty()) {
                const bool prune = settings.value("logging/prune", "").toString() == "true";
                logfault::LogManager::Instance().AddHandler(
                    make_unique<logfault::StreamHandler>(path, static_cast<logfault::LogLevel>(level), prune));
            }
        }

#ifdef USE_ANDROID_UI
        if (style.empty()) {
            style = "Material";
        }
#endif
#if defined(USE_ANDROID_UI) || defined(__ANDROID__)
        if (scale.empty()) {
            scale = "1.3";
        }
#endif

        if (!style.empty()) {
            LOG_INFO << "Setting UI style to: " << style;
            QQuickStyle::setStyle(style.data());
        }

        if (!scale.empty()) {
            LOG_INFO << "Setting UI scale to: " << scale;
            qputenv("QT_SCALE_FACTOR", scale.data());
        }
    }

    nextapp::pb::Nextapp::Client cli{&app};
    auto info = cli.GetServerInfo({});

    NextAppCore core;
    ServerComm comms;
    ActionInfoCache ai_cache;
    MainTreeModel main_tree;

    auto& engine = NextAppCore::engine();


    qRegisterMetaType<ActionCategoriesModel*>("ActionCategoriesModel*");
    qRegisterMetaType<TimeBoxActionsModel*>("TimeBoxActionsModel*");

    qmlRegisterSingletonInstance<NextAppCore>("Nextapp.Models", 1, 0, "NaCore", &core);
    qmlRegisterSingletonInstance<ServerComm>("Nextapp.Models", 1, 0, "NaComm", &comms);
    qmlRegisterSingletonInstance<ActionInfoCache>("Nextapp.Models", 1, 0, "NaAiCache", &ai_cache);
    qmlRegisterSingletonInstance<MainTreeModel>("Nextapp.Models", 1, 0, "NaMainTreeModel", &main_tree);

#ifdef WITH_TREE_MODEL_TESTING
    new QAbstractItemModelTester{&main_tree, QAbstractItemModelTester::FailureReportingMode::Fatal, &engine};
#endif

    string_view main_qml = "qrc:/qt/qml/NextAppUi/Main.qml";
#if defined(__ANDROID__) || defined(USE_ANDROID_UI)
    // Code specific to Android
    qputenv("QT_QUICK_CONTROLS_STYLE", "Material");
    main_qml = "qrc:/qt/qml/NextAppUi/qml/android/main.qml";
#endif

    LOG_INFO << "Loading main QML file: " << main_qml;
    //engine.loadFromModule("NextAppUi", main_qml);
    engine.addImportPath("qrc:/qt/qml/NextAppUi/qml");
    engine.load(QUrl{QString::fromUtf8(main_qml)});
    if (engine.rootObjects().isEmpty()) {
        qWarning() << "Failed to initialize engine!";
        return -1;
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

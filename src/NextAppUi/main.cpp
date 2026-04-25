#include <iostream>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QIcon>
#include <QAbstractItemModelTester>
#include <QtPlugin>
#include <QQuickStyle>
#include <QSslSocket>
#include <QQuickWindow>
#include <QSettings>
#include <QScreen>
#include <optional>

#include "ServerComm.h"
#include "MainTreeModel.h"
#include "GreenDaysModel.h"
#include "DayColorModel.h"
#include "ActionsModel.h"
#include "WorkSessionsModel.h"
#include "NextAppCore.h"
#include "ActionInfoCache.h"
#include "ActionCategoriesModel.h"
#include "TimeBoxActionsModel.h"
#include "ActionCategoriesModel.h"
#include "ActionsModel.h"
#include "GreenDaysModel.h"
#include "WorkSessionsModel.h"
#include "AppInstanceMgr.h"
#include "LogModel.h"
#include "nextapp.qpb.h"

#ifdef __ANDROID__
// #   include <QJniObject>
// #   include <QJniEnvironment>
// #   include <QCoreApplication>
    #include "AndroidHandlers.h"
#endif

#include "logging.h"

extern void qml_register_types_nextapp_pb();

using namespace std;

Q_IMPORT_PLUGIN(NextAppUiPlugin)

#ifdef __ANDROID__

namespace nextapp::logging {
void initAndroidLogging(logfault::LogLevel level) {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

    logfault::LogManager::Instance().AddHandler(
        make_unique<logfault::AndroidHandler>("NextApp", static_cast<logfault::LogLevel>(level)));

}
} // ns

#endif


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

void logQtMessages(QtMsgType type, const QMessageLogContext &context, const QString &rawMsg)
{
    auto msg = rawMsg;
    msg.replace('\n', ' ');

    switch (type) {
    case QtDebugMsg:
        LOG_TRACE << "[Qt] " << msg;
        break;
    case QtInfoMsg:
        LOG_DEBUG << "[Qt] " << msg;
        break;
    case QtWarningMsg: {
        static const QRegularExpression filter{"is neither a default constructible QObject"
                                               "|Cannot anchor to an item that isn't a parent or sibling"
                                               "|Detected anchors on an item that is managed by a layout"};
        if (filter.match(msg).hasMatch()) {
            LOG_TRACE << "[Qt] " << msg;
            break;
        }
        LOG_WARN << "[Qt] " << msg;
        } break;
    case QtCriticalMsg:
        LOG_ERROR << "[Qt] " << msg;
        break;
    case QtFatalMsg:
        LOG_ERROR << "[Qt **FATAL**] " << msg;
        exit(-1);
        break;
    }
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

bool isX11Platform()
{
    return QGuiApplication::platformName().compare("xcb", Qt::CaseInsensitive) == 0;
}

bool isWaylandPlatform()
{
    return QGuiApplication::platformName().startsWith("wayland", Qt::CaseInsensitive);
}

bool isSessionRestored()
{
    const auto *app = qobject_cast<const QGuiApplication *>(QGuiApplication::instance());
    return app && app->isSessionRestored();
}

bool shouldSuppressManualGeometryRestore()
{
    // Qt 6.11's Wayland plugin has experimental support for xx-session-management-v1.
    // isSessionRestored() is a generic signal, not proof that this Wayland protocol is active,
    // so we only use it to avoid fighting a possible compositor/session restore.
    return isWaylandPlatform() && isSessionRestored();
}

enum class WindowRestorePolicy {
    FullManual,
    WaylandSession,
    WaylandFallback,
};

WindowRestorePolicy windowRestorePolicy()
{
    if (!isWaylandPlatform()) {
        return WindowRestorePolicy::FullManual;
    }

    return shouldSuppressManualGeometryRestore()
        ? WindowRestorePolicy::WaylandSession
        : WindowRestorePolicy::WaylandFallback;
}

const char *windowRestorePolicyName(WindowRestorePolicy policy)
{
    switch (policy) {
    case WindowRestorePolicy::FullManual:
        return "full manual geometry restore";
    case WindowRestorePolicy::WaylandSession:
        return "Wayland session restore; manual position suppressed";
    case WindowRestorePolicy::WaylandFallback:
        return "Wayland fallback; manual position attempted";
    }

    return "unknown";
}


class WindowStateManager { //: public QObject {
    //Q_OBJECT

    QRect getCombinedScreenGeometry() {
        QRect combinedGeometry;
        const auto screens = QGuiApplication::screens();  // Get all screens
        for (QScreen* screen : screens) {
            combinedGeometry = combinedGeometry.united(screen->availableGeometry());
        }
        LOG_DEBUG_N << "Combined screen geometry: " << combinedGeometry.width() << "x" << combinedGeometry.height();
        return combinedGeometry;
    }

public:
    explicit WindowStateManager(QQuickWindow* window, QString stableWindowId, QObject* parent = nullptr)
        : //QObject(parent),
        m_window(window),
        m_stableWindowId(std::move(stableWindowId)) {
        if (!m_window) {
            return;
        }

        if (m_window->objectName().isEmpty()) {
            m_window->setObjectName(m_stableWindowId);
        }

        const QRect legacyGeometry = QSettings{}.value("windowGeometry", QRect(0,0,0,0)).toRect();
        QSettings settings;
        settings.beginGroup("windows");
        settings.beginGroup(m_stableWindowId);
        const QRect geometry = settings.value("geometry", legacyGeometry).toRect();
        const QSize size = settings.value("size", geometry.size()).toSize();
        const bool maximized = settings.value("maximized", false).toBool();
        const bool fullscreen = settings.value("fullscreen", false).toBool();
        settings.setValue("id", m_stableWindowId);

        const auto platform = QGuiApplication::platformName().toStdString();
        const auto policy = windowRestorePolicy();
        LOG_INFO_N << "Window restore for " << m_stableWindowId.toStdString()
                   << ": platform=" << platform
                   << ", x11=" << isX11Platform()
                   << ", wayland=" << isWaylandPlatform()
                   << ", sessionRestored=" << isSessionRestored()
                   << ", policy=" << windowRestorePolicyName(policy);

        if (geometry.isEmpty()) {
            LOG_DEBUG_N << "No saved geometry for " << m_stableWindowId.toStdString();
            restoreWindowMode(fullscreen, maximized);
            return;
        };

        switch (policy) {
        case WindowRestorePolicy::FullManual:
            restoreGeometry(geometry);
            restoreWindowMode(fullscreen, maximized);
            break;
        case WindowRestorePolicy::WaylandSession:
            LOG_INFO_N << "Suppressing saved window position for " << m_stableWindowId.toStdString()
                       << " because Qt reports a restored session on Wayland.";
            restoreSizeIfNeeded(size);
            restoreWindowMode(fullscreen, maximized);
            break;
        case WindowRestorePolicy::WaylandFallback:
            restoreSize(size);
            restorePositionBestEffort(geometry.topLeft());
            restoreWindowMode(fullscreen, maximized);
            break;
        }
    }

    ~WindowStateManager() {
        if (!m_window) {
            return;
        }

        QSettings settings;
        const QRect geometry(m_window->x(), m_window->y(), m_window->width(), m_window->height());

        settings.beginGroup("windows");
        settings.beginGroup(m_stableWindowId);
        settings.setValue("id", m_stableWindowId);
        settings.setValue("geometry", geometry);
        settings.setValue("size", geometry.size());
        settings.setValue("position", geometry.topLeft());
        settings.setValue("maximized", m_window->windowStates().testFlag(Qt::WindowMaximized));
        settings.setValue("fullscreen", m_window->windowStates().testFlag(Qt::WindowFullScreen));
        settings.endGroup();
        settings.endGroup();

        // Keep the old key in sync so older builds keep their current behavior.
        settings.setValue("windowGeometry", geometry);
    }

private:
    void restoreGeometry(const QRect& geometry)
    {
        QRect availableGeometry = getCombinedScreenGeometry();
        if (!availableGeometry.contains(geometry)) {
            LOG_DEBUG_N << "Window geometry for " << m_stableWindowId.toStdString()
                        << " is outside of the screen, ignoring.";
            return;
        }

        LOG_INFO_N << "Restoring saved manual window position and size for " << m_stableWindowId.toStdString();
        m_window->setX(geometry.x());
        m_window->setY(geometry.y());
        restoreSize(geometry.size());
    }

    void restoreSize(const QSize& size)
    {
        if (!size.isValid() || size.isEmpty()) {
            return;
        }

        LOG_INFO_N << "Restoring saved window size for " << m_stableWindowId.toStdString()
                   << ": " << size.width() << "x" << size.height();
        m_window->setWidth(size.width());
        m_window->setHeight(size.height());
    }

    void restoreSizeIfNeeded(const QSize& size)
    {
        if (m_window->width() > 0 && m_window->height() > 0) {
            LOG_INFO_N << "Keeping current/session-restored window size for " << m_stableWindowId.toStdString();
            return;
        }

        restoreSize(size);
    }

    void restorePositionBestEffort(const QPoint& position)
    {
        const QRect geometry(position, QSize(m_window->width(), m_window->height()));
        if (!getCombinedScreenGeometry().contains(geometry)) {
            LOG_DEBUG_N << "Wayland fallback position for " << m_stableWindowId.toStdString()
                        << " is outside of the screen, ignoring.";
            return;
        }

        LOG_INFO_N << "Trying saved window position for " << m_stableWindowId.toStdString()
                   << " as Wayland fallback; the compositor may ignore it.";
        m_window->setX(position.x());
        m_window->setY(position.y());
    }

    void restoreWindowMode(bool fullscreen, bool maximized)
    {
        if (fullscreen) {
            LOG_INFO_N << "Restoring fullscreen state for " << m_stableWindowId.toStdString();
            m_window->showFullScreen();
        } else if (maximized) {
            LOG_INFO_N << "Restoring maximized state for " << m_stableWindowId.toStdString();
            m_window->showMaximized();
        }
    }

    QQuickWindow* m_window;
    QString m_stableWindowId;
};

} // anon ns

int main(int argc, char *argv[])
{
    //qRegisterProtobufTypes();

#ifdef __ANDROID__
#ifdef _DEBUG
    nextapp::logging::initAndroidLogging(logfault::LogLevel::TRACE);
#else
    nextapp::logging::initAndroidLogging(logfault::LogLevel::DEBUGGING);
#endif // _DEBUG

#endif // __ANDROID__

    std::string log_level_qt =
#ifdef _DEBUG
        "trace";
#else
        "debug";
#endif
    QGuiApplication app(argc, argv);
#ifdef NEXTAPP_DESKTOP_FILE_NAME
    QGuiApplication::setDesktopFileName(QStringLiteral(NEXTAPP_DESKTOP_FILE_NAME));
#endif

    LogModel log_handler;

    // Allow us to use an alternative config-file for testing
    if (const auto* org = getenv("NEXTAPP_ORG")){
         QGuiApplication::setOrganizationName(org);
    } else {
        QGuiApplication::setOrganizationName("TheLastViking");
    }

    QString app_name = "nextapp";
#ifdef DEVEL_SETTINGS
    app_name = "nextapp-devel";
#endif

    QGuiApplication::setApplicationVersion(NEXTAPP_UI_VERSION);
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
    parser.addOption({"app-name", "Override the application name (same as NEXTAPP_NAME)", "app-name"});
    parser.addOption({{"s", "signup"}, "Run the signup work-flow"});

    //parser.addPositionalArgument("", QGuiApplication::translate("main", "Initial directory"),"[path]");
    parser.process(app);
    //const auto args = parser.positionalArguments();

    if (const auto* org = getenv("NEXTAPP_NAME")) {
        app_name = org;
    }

    if (parser.isSet("app-name")) {
        if (const auto cli_app_name = parser.value("app-name"); !cli_app_name.isEmpty()) {
            app_name = cli_app_name;
        }
    }

    QGuiApplication::setApplicationName(app_name);

    if (parser.isSet("log-level-console")) {
        log_level_qt = parser.value("log-level-console").toStdString();
    }

    if (parser.isSet("log-file")) {
        if (auto path = parser.value("log-file").toStdString(); !path.empty()) {
            if (const auto level = toLogLevel(parser.value("log-level").toStdString()); level.has_value()) {
                logfault::LogManager::Instance().AddHandler(make_unique<logfault::StreamHandler>(path,*level, true));
            }
        }
    }

    QSettings settings{};
    const bool onboarded = settings.value("onboarding", false).toBool();

    bool delete_db_if_exists = false;
    {
        if (!onboarded || parser.isSet("signup") || settings.value("server/deleted", false).toBool()) {
            LOG_INFO << "Running the signup work-flow";
            settings.setValue("onboarding", false);
            settings.remove("device");
            settings.remove("server");
            settings.remove("windowGeometry");
            settings.remove("windows");
            delete_db_if_exists = true;
            settings.sync();
        }

        if (!settings.contains("UI/style")) {
            settings.setValue("UI/style", 0);
        }

        if (!settings.contains("UI/theme")) {
            settings.setValue("UI/theme", "dark");
        }

        if (!settings.contains("server/resend_requests")) {
            settings.setValue("server/resend_requests", true);
        }

        if (!settings.contains("client/maxInstances")) {
            settings.setValue("client/maxInstances", 1);
        }

        if (!settings.contains("logging/applevel")) {
            settings.setValue("logging/applevel", 4); // INFO
        }

        auto style = styles.at(settings.value("UI/style").toInt());
        auto scale = scales.at(settings.value("UI/scale").toInt());

        if (!settings.contains("logging/path")) {

#ifdef ANDROID_BUILD
            const QString media_path = nextapp::android::getLogsDir();
            if (!media_path.isEmpty() && !settings.contains("logging/path")) {
                // If the path is empty, set it to the media path
                settings.setValue("logging/path", media_path + "/" + app_name + ".log");
                LOG_INFO << "Using log directory: " << media_path;
            }
#else
            QDir baseDir(QDir::homePath());
            const auto log_path = baseDir.filePath("NextApp/Logging/" + app_name + ".log");
            settings.setValue("logging/path", log_path);
            const auto abs_path = QFileInfo{log_path}.absolutePath();
            QDir log_dir{abs_path};
            if (!abs_path.isEmpty() && !log_dir.exists()) {
                LOG_INFO << "Creating log directory: " << abs_path;
                log_dir.mkpath(abs_path);
            }
#endif
        }

        {
            auto level = settings.value("logging/level", 0).toInt();
#ifdef _DEBUG
            if (!level) {
                level = static_cast<int>(logfault::LogLevel::TRACE);
            }
#endif

            if (level > 0) {
                if (auto path = settings.value("logging/path", "").toString().toStdString(); !path.empty()) {
                    const bool prune = settings.value("logging/prune", "").toString() == "true";
                    logfault::LogManager::Instance().AddHandler(
                        make_unique<logfault::StreamHandler>(path, static_cast<logfault::LogLevel>(level), prune));

                    LOG_INFO << "Logging to: " << path;
                }
            }
        }

#if defined(LINUX_BUILD) || defined(__ANDROID__)
        if (const auto level = settings.value("logging/applevel", 4).toInt()) {
#ifdef __ANDROID__
            nextapp::logging::initAndroidLogging(static_cast<logfault::LogLevel>(level));
#else
            logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(clog, static_cast<logfault::LogLevel>(level)));
#endif

            LOG_INFO << "Logging to system";
        }
#endif

#ifndef __ANDROID__
        // Handle multiple instances of the app with their own data
        const auto has_instance = AppInstanceMgr::instance()->init();
        QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
            AppInstanceMgr::instance()->close();
        });
#else
        // Android doesn't support multiple instances
        const bool has_instance = true;
#endif

        if (!has_instance) {
            // Open qml/NoInstance.qml as the main window and exit when it closes
            LOG_WARN_N << "Opening NoInstance.qml as the main window.";
            QQmlApplicationEngine engine;
            engine.load(QUrl("qrc:/qt/qml/NextAppUi/qml/NoInstance.qml"));
            if (engine.rootObjects().isEmpty()) {
                LOG_ERROR_N << "Failed to initialize QML engine!";
                return -1;
            }
            return app.exec();
        }

        qInstallMessageHandler(logQtMessages);
        LOG_INFO << app_name << ' ' << NEXTAPP_UI_VERSION << " starting up.";
        LOG_INFO << "Configuration from '" << settings.fileName() << "'";

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

#ifndef __ANDROID__
        QQuickStyle::setFallbackStyle("Basic");
        if (style.empty()) {
            style = "Basic";
        }
#endif

        if (!style.empty()) {
            LOG_INFO << "Setting UI style to: " << style;
            QQuickStyle::setStyle(style.data());
        }

        // if (!scale.empty()) {
        //     LOG_INFO << "Setting UI scale to: " << scale;
        //     qputenv("QT_SCALE_FACTOR", scale.data());
        // }
    }

    // nextapp::pb::Nextapp::Client cli{&app};
    // auto info = cli.GetServerInfo({});

    LOG_TRACE_N << "Constructing QMLApplicatioonEngine...";
    QQmlApplicationEngine engine;

    LOG_TRACE_N << "Constructing static models...";
    NextAppCore core(engine);
    auto& comms = static_cast<ServerComm&>(core.serverComm());
    MainTreeModel main_tree;
    ActionCategoriesModel ac_model;
    ActionInfoCache ai_cache;
    ActionsModel actions_model;
    GreenDaysModel green_days;
    DayColorModel day_colors;
    WorkSessionsModel work_sessions;

    if (delete_db_if_exists) {
        LOG_WARN << "Deleting the current database if it exists!";
        core.db().clear();
    }

    LOG_TRACE_N << "Registering types...";
    qRegisterMetaType<ActionCategoriesModel*>("ActionCategoriesModel*");
    qRegisterMetaType<TimeBoxActionsModel*>("TimeBoxActionsModel*");

    LOG_TRACE_N << "Registering static models for QML...";
    qmlRegisterSingletonInstance<LogModel>("Nextapp.Models", 1, 0, "NaLogModel", &log_handler);
    qmlRegisterSingletonInstance<NextAppCore>("Nextapp.Models", 1, 0, "NaCore", &core);
    qmlRegisterSingletonInstance<ServerComm>("Nextapp.Models", 1, 0, "NaComm", &comms);
    qmlRegisterSingletonInstance<MainTreeModel>("Nextapp.Models", 1, 0, "NaMainTreeModel", &main_tree);
    qmlRegisterSingletonInstance<ActionCategoriesModel>("Nextapp.Models", 1, 0, "NaAcModel", &ac_model);
    qmlRegisterSingletonInstance<ActionInfoCache>("Nextapp.Models", 1, 0, "NaAiCache", &ai_cache);
    qmlRegisterSingletonInstance<ActionsModel>("Nextapp.Models", 1, 0, "NaActionsModel", &actions_model);
    qmlRegisterSingletonInstance<DayColorModel>("Nextapp.Models", 1, 0, "NaDayColorModel", &day_colors);
    qmlRegisterSingletonInstance<GreenDaysModel>("Nextapp.Models", 1, 0, "NaGreenDaysModel", &green_days);
    qmlRegisterSingletonInstance<WorkSessionsModel>("Nextapp.Models", 1, 0, "NaWorkSessionsModel", &work_sessions);

#ifdef WITH_TREE_MODEL_TESTING
    new QAbstractItemModelTester{&main_tree, QAbstractItemModelTester::FailureReportingMode::Fatal, &engine};
#endif

    string_view main_qml = "qrc:/qt/qml/NextAppUi/Main.qml";
#if defined(__ANDROID__) || defined(USE_ANDROID_UI)
    qputenv("QT_QUICK_CONTROLS_STYLE", "Material");
    main_qml = "qrc:/qt/qml/NextAppUi/qml/android/main.qml";
#endif

    LOG_INFO << "Loading main QML file: " << main_qml;
    engine.addImportPath("qrc:/qt/qml/NextAppUi/qml");
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, [](const QList<QQmlError>& warnings) {
        for (const auto& warning : warnings) {
            LOG_ERROR_N << "QML warning: " << warning.toString().toStdString();
        }
    });
    engine.load(QUrl{QString::fromUtf8(main_qml)});
    if (engine.rootObjects().isEmpty()) {
        LOG_ERROR_N << "Failed to initialize QML engine!";
        return -1;
    }

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() {
            LOG_ERROR_N << "Failed to create QML UI!";
            QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);

    LOG_INFO << "Device supports OpenSSL: " << QSslSocket::supportsSsl();

    NextAppCore::instance()->modelsAreCreated();

#ifndef __ANDROID__
    QQuickWindow* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    WindowStateManager manager(window, "mainWindow");
#endif

    LOG_DEBUG_N << "Handing the main thread over to QT";
    auto ret = app.exec();

    if (auto server_comm =  engine.singletonInstance<ServerComm*>(
            "NextAppUi","ServerComm")) {
        LOG_DEBUG_N << "Shutting down gRPC";
        server_comm->stop();
    }

    LOG_DEBUG_N << "Exiting the app";
    return ret;
}

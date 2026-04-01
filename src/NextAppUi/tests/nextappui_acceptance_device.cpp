#include <QCommandLineOption>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QMetaObject>
#include <QSettings>
#include <QTimer>
#include <QEventLoop>
#include <QThread>
#include <optional>

#include "ActionsModel.h"
#include "ActionCategoriesModel.h"
#include "ActionInfoCache.h"
#include "AppInstanceMgr.h"
#include "CalendarCache.h"
#include "GreenDaysModel.h"
#include "MainTreeModel.h"
#include "NextAppCore.h"
#include "NotificationsModel.h"
#include "OtpModel.h"
#include "ServerComm.h"
#include "UseCaseTemplates.h"
#include "WorkCache.h"
#include "logging.h"

namespace {

template <typename T>
bool waitForTask(QCoro::Task<T> task, T* out, int timeout_ms)
{
    QEventLoop loop;
    QTimer timer;
    bool finished = false;
    bool timed_out = false;

    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        timed_out = true;
        loop.quit();
    });

    auto runner = [&]() -> QCoro::Task<void> {
        if constexpr (std::is_void_v<T>) {
            co_await task;
        } else {
            *out = co_await task;
        }
        finished = true;
        loop.quit();
    };

    runner();
    timer.start(timeout_ms);
    loop.exec();
    return finished && !timed_out;
}

bool waitForTaskVoid(QCoro::Task<void> task, int timeout_ms)
{
    QEventLoop loop;
    QTimer timer;
    bool finished = false;
    bool timed_out = false;

    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        timed_out = true;
        loop.quit();
    });

    auto runner = [&]() -> QCoro::Task<void> {
        co_await task;
        finished = true;
        loop.quit();
    };

    runner();
    timer.start(timeout_ms);
    loop.exec();
    return finished && !timed_out;
}

bool waitForCondition(const std::function<bool()>& condition, int timeout_ms)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeout_ms) {
        if (condition()) {
            return true;
        }

        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(50);
    }

    return condition();
}

void printJson(const QJsonObject& object)
{
    const auto json = QJsonDocument{object}.toJson(QJsonDocument::Compact);
    fwrite(json.constData(), 1, static_cast<size_t>(json.size()), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

[[noreturn]] void fastExit(int code)
{
    AppInstanceMgr::instance()->close();
    std::_Exit(code);
}

QJsonObject baseResult(const QString& command,
                       const QString& device_name,
                       const NextAppCore& core,
                       const ServerComm& comm)
{
    return {
        {QStringLiteral("command"), command},
        {QStringLiteral("deviceName"), device_name},
        {QStringLiteral("dbPath"), core.db().dbPath()},
        {QStringLiteral("dataDir"), QString::fromStdString(core.db().dataDir().string())},
        {QStringLiteral("instanceId"), static_cast<int>(AppInstanceMgr::instance()->instanceId())},
        {QStringLiteral("serverStatus"), static_cast<int>(comm.status())},
        {QStringLiteral("hasServerUrl"), !core.settings().value(QStringLiteral("server/url"), QString{}).toString().isEmpty()},
    };
}

bool allSyncModelsValid()
{
    return GreenDaysModel::instance()->valid()
        && MainTreeModel::instance()->valid()
        && ActionCategoriesModel::instance().valid()
        && ActionInfoCache::instance()->valid()
        && WorkCache::instance()->valid()
        && CalendarCache::instance()->valid()
        && NotificationsModel::instance()->valid();
}

int signupStatus(const ServerComm& comm)
{
    return comm.property("signupStatus").toInt();
}

void mergeObject(QJsonObject& target, const QJsonObject& source);

std::optional<QJsonObject> loadDbInfo(NextAppCore& core)
{
    tl::expected<nextapp::pb::UserDataInfo, DbStore::Error> info_result;
    if (!waitForTask(core.db().getDbDataInfo(), &info_result, 120000) || !info_result) {
        return std::nullopt;
    }

    tl::expected<int, DbStore::Error> day_color_count;
    if (!waitForTask(core.db().queryOne<int>(QStringLiteral("SELECT COUNT(*) FROM day_colors")),
                     &day_color_count,
                     120000)
        || !day_color_count) {
        return std::nullopt;
    }
    const auto num_day_colors = day_color_count.value();

    const auto& info = info_result.value();
    return QJsonObject{
        {QStringLiteral("hash"), info.hash()},
        {QStringLiteral("numNodes"), static_cast<int>(info.numNodes())},
        {QStringLiteral("numActionCategories"), static_cast<int>(info.numActionCategories())},
        {QStringLiteral("numActions"), static_cast<int>(info.numActions())},
        {QStringLiteral("numDays"), static_cast<int>(info.numDays())},
        {QStringLiteral("numDayColors"), num_day_colors},
        {QStringLiteral("numWorkSessions"), static_cast<int>(info.numWorkSessions())},
        {QStringLiteral("numTimeBlocks"), static_cast<int>(info.numTimeBlocks())},
    };
}

bool tryMergeDbInfo(QJsonObject& target, NextAppCore& core)
{
    const auto info = loadDbInfo(core);
    if (!info) {
        return false;
    }

    const auto hash = info->value(QStringLiteral("hash")).toString();
    if (hash.isEmpty()) {
        return false;
    }

    mergeObject(target, *info);
    return true;
}

bool waitForAndMergeDbInfo(QJsonObject& target, NextAppCore& core, int timeout_ms)
{
    QJsonObject merged;
    const auto have_info = waitForCondition([&] {
        merged = target;
        return tryMergeDbInfo(merged, core);
    }, timeout_ms);

    if (!have_info) {
        return false;
    }

    target = merged;
    return true;
}

bool waitForAndMergeDbInfoMatching(QJsonObject& target,
                                   NextAppCore& core,
                                   int timeout_ms,
                                   const std::function<bool(const QJsonObject&)>& predicate)
{
    QJsonObject merged;
    const auto have_info = waitForCondition([&] {
        merged = target;
        if (!tryMergeDbInfo(merged, core)) {
            return false;
        }
        return predicate(merged);
    }, timeout_ms);

    if (!have_info) {
        return false;
    }

    target = merged;
    return true;
}

std::optional<int> loadPendingRequestCount(NextAppCore& core)
{
    tl::expected<int, DbStore::Error> request_count;
    if (!waitForTask(core.db().queryOne<int>(QStringLiteral("SELECT COUNT(*) FROM requests")),
                     &request_count,
                     120000)
        || !request_count) {
        return std::nullopt;
    }

    return request_count.value();
}

void mergeObject(QJsonObject& target, const QJsonObject& source)
{
    for (auto it = source.begin(); it != source.end(); ++it) {
        target.insert(it.key(), it.value());
    }
}

QString findCategoryIdByName(ActionCategoriesModel& model, const QString& name)
{
    const auto rows = model.rowCount({});
    for (int row = 0; row < rows; ++row) {
        const auto category = model.get(row);
        if (category.name() == name && !category.id_proto().isEmpty()) {
            return category.id_proto();
        }
    }

    return {};
}

nextapp::pb::ActionCategory makeCategory(const QString& id_suffix, const QString& batch_name)
{
    nextapp::pb::ActionCategory category;
    category.setId_proto(QUuid::createUuid().toString(QUuid::WithoutBraces));
    category.setName(QStringLiteral("Acceptance %1 %2").arg(batch_name, id_suffix));
    category.setColor(QStringLiteral("dodgerblue"));
    category.setIcon(QStringLiteral("briefcase"));
    category.setDescr(QStringLiteral("Acceptance category %1 %2").arg(batch_name, id_suffix));
    category.setVersion(1);
    return category;
}

nextapp::pb::Node makeNode(const QString& batch_name, const QString& device_name)
{
    nextapp::pb::Node node;
    node.setUuid(QUuid::createUuid().toString(QUuid::WithoutBraces));
    node.setName(QStringLiteral("Acceptance %1 %2").arg(batch_name, device_name));
    node.setKind(nextapp::pb::Node::Kind::PROJECT);
    node.setParent(QString{});
    node.setActive(true);
    return node;
}

nextapp::pb::Action makeAction(const QString& batch_name,
                               const QString& device_name,
                               const nextapp::pb::Node& node,
                               const nextapp::pb::ActionCategory& category)
{
    nextapp::pb::Action action;
    action.setId_proto(QUuid::createUuid().toString(QUuid::WithoutBraces));
    action.setNode(node.uuid());
    action.setStatus(nextapp::pb::ActionStatusGadget::ActionStatus::ACTIVE);
    action.setFavorite(false);
    action.setName(QStringLiteral("Acceptance action %1 %2").arg(batch_name, device_name));

    nextapp::pb::Date created;
    const auto today = QDate::currentDate();
    created.setYear(today.year());
    created.setMonth(today.month());
    created.setMday(today.day());
    action.setCreatedDate(created);

    nextapp::pb::Priority priority;
    priority.setPriority(nextapp::pb::ActionPriorityGadget::ActionPriority::PRI_NORMAL);
    priority.setScore(10);
    action.setDynamicPriority(priority);

    action.setDescr(QStringLiteral("Acceptance action for batch %1").arg(batch_name));
    action.setHasDescr(true);
    action.setTimeEstimate(15);
    action.setDifficulty(nextapp::pb::ActionDifficultyGadget::ActionDifficulty::NORMAL);
    action.setKind(nextapp::pb::ActionKindGadget::ActionKind::AC_ACTIVE);
    action.setVersion(1);
    action.setCategory(category.id_proto());
    action.setTimeSpent(0);

    nextapp::pb::Due due;
    due.setKind(nextapp::pb::ActionDueKindGadget::ActionDueKind::UNSET);
    due.setTimezone(QStringLiteral("UTC"));
    action.setDue(due);
    return action;
}

} // namespace

int main(int argc, char** argv)
{
    QString workspace_root;
    QString device_name;
    QString command;
    QString signup_url;
    QString user_name;
    QString user_email;
    QString company;
    QString requested_device_name;
    QString otp_value;
    QString batch_name;
    QString template_name;
    int region = 0;

    for (int i = 1; i < argc; ++i) {
        const auto arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--workspace-root") && i + 1 < argc) {
            workspace_root = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        if (arg == QStringLiteral("--device-name") && i + 1 < argc) {
            device_name = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        if (arg == QStringLiteral("--signup-url") && i + 1 < argc) {
            signup_url = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        if (arg == QStringLiteral("--user-name") && i + 1 < argc) {
            user_name = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        if (arg == QStringLiteral("--user-email") && i + 1 < argc) {
            user_email = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        if (arg == QStringLiteral("--company") && i + 1 < argc) {
            company = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        if (arg == QStringLiteral("--region") && i + 1 < argc) {
            region = QString::fromLocal8Bit(argv[++i]).toInt();
            continue;
        }
        if (arg == QStringLiteral("--signup-device-name") && i + 1 < argc) {
            requested_device_name = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        if (arg == QStringLiteral("--otp") && i + 1 < argc) {
            otp_value = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        if (arg == QStringLiteral("--batch") && i + 1 < argc) {
            batch_name = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        if (arg == QStringLiteral("--template-name") && i + 1 < argc) {
            template_name = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        if (arg == QStringLiteral("--help")) {
            fprintf(stdout, "Usage: nextappui_acceptance_device --workspace-root PATH --device-name NAME [--otp OTP] [--batch NAME] [--template-name NAME] <prepare|wait-ready|signup-first-device|request-otp|add-device-with-otp|disconnect|reconnect|force-full-sync|apply-scripted-batch>\n");
            return 0;
        }
        if (command.isEmpty()) {
            command = arg;
            continue;
        }
    }

    if (workspace_root.isEmpty() || device_name.isEmpty() || command.isEmpty()) {
        fprintf(stderr, "Missing --workspace-root, --device-name, or command\n");
        return 2;
    }

    QDir{}.mkpath(workspace_root + QStringLiteral("/config"));
    QDir{}.mkpath(workspace_root + QStringLiteral("/data"));
    QDir{}.mkpath(workspace_root + QStringLiteral("/home"));

    qputenv("QT_QPA_PLATFORM", qEnvironmentVariable("QT_QPA_PLATFORM", "offscreen").toUtf8());
    qputenv("SDL_AUDIODRIVER", qEnvironmentVariable("SDL_AUDIODRIVER", "dummy").toUtf8());
    qputenv("XDG_CONFIG_HOME", (workspace_root + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (workspace_root + QStringLiteral("/data")).toUtf8());
    qputenv("HOME", (workspace_root + QStringLiteral("/home")).toUtf8());
    qputenv("NEXTAPP_ORG", QByteArrayLiteral("NextAppAcceptance"));
    const auto app_name = qEnvironmentVariable(
        "NEXTAPP_NAME",
        QStringLiteral("nextapp-acceptance-%1-%2")
            .arg(device_name)
            .arg(QString::number(qHash(workspace_root), 16)));
    qputenv("NEXTAPP_NAME", app_name.toUtf8());

    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(qEnvironmentVariable("NEXTAPP_ORG"));
    QGuiApplication::setApplicationName(qEnvironmentVariable("NEXTAPP_NAME"));
    QGuiApplication::setApplicationVersion(NEXTAPP_UI_VERSION);

    {
        const auto log_dir = workspace_root + QStringLiteral("/logs");
        QDir{}.mkpath(log_dir);
        const auto log_path = log_dir + QStringLiteral("/") + device_name + QStringLiteral(".log");
        logfault::LogManager::Instance().AddHandler(
            std::make_unique<logfault::StreamHandler>(
                log_path.toStdString(),
                logfault::LogLevel::TRACE,
                true));
        LOG_INFO_N << "Acceptance helper logging to " << log_path;
    }

    QSettings settings;
    if (!settings.contains(QStringLiteral("client/maxInstances"))) {
        settings.setValue(QStringLiteral("client/maxInstances"), 10);
    }
    settings.sync();

    if (!AppInstanceMgr::instance()->init()) {
        fprintf(stderr, "Failed to initialize AppInstanceMgr\n");
        return 3;
    }

    QQmlApplicationEngine engine;
    NextAppCore core{engine};
    GreenDaysModel green_days{core};
    MainTreeModel main_tree{core};
    ActionCategoriesModel action_categories{core};
    ActionInfoCache action_info{core};
    WorkCache work_cache{core};
    CalendarCache calendar_cache{core};
    NotificationsModel notifications{core};
    auto& comm = static_cast<ServerComm&>(core.serverComm());

    if (!waitForTaskVoid(core.modelsAreCreated(), 120000)) {
        fprintf(stderr, "Timed out while initializing NextAppCore models\n");
        return 4;
    }

    if (command == QStringLiteral("prepare")) {
        printJson(baseResult(command, device_name, core, comm));
        fastExit(0);
    }

    if (command == QStringLiteral("signup-first-device")) {
        if (signup_url.isEmpty() || user_name.isEmpty() || user_email.isEmpty()) {
            fprintf(stderr, "signup-first-device requires --signup-url, --user-name and --user-email\n");
            return 2;
        }

        comm.setSignupServerAddress(signup_url);
        const auto have_info = waitForCondition([&] {
            return signupStatus(comm) == ServerComm::SIGNUP_HAVE_INFO;
        }, 120000);
        if (!have_info) {
            fprintf(stderr, "Timed out waiting for signup server info\n");
            return 6;
        }

        const auto device_label = requested_device_name.isEmpty() ? device_name : requested_device_name;
        comm.signup(user_name, user_email, company, device_label, region);

        const auto signed_up = waitForCondition([&] {
            const auto status = signupStatus(comm);
            return status == ServerComm::SIGNUP_SUCCESS
                || status == ServerComm::SIGNUP_OK
                || status == ServerComm::SIGNUP_ERROR;
        }, 240000);
        if (!signed_up || signupStatus(comm) == ServerComm::SIGNUP_ERROR) {
            fprintf(stderr, "Timed out waiting for signup to complete\n");
            return 7;
        }

        auto result = baseResult(command, device_name, core, comm);
        result.insert(QStringLiteral("templateName"), template_name);

        if (!template_name.isEmpty()) {
            UseCaseTemplates templates{core};
            const auto names = templates.getTemplateNames();
            const auto template_index = names.indexOf(template_name);
            if (template_index <= 0) {
                fprintf(stderr,
                        "Unknown template name: %s\n",
                        template_name.toUtf8().constData());
                return 21;
            }
            templates.createFromTemplate(template_index);
            result.insert(QStringLiteral("templateApplied"), true);
            result.insert(QStringLiteral("templateIndex"), template_index);
        } else {
            result.insert(QStringLiteral("templateApplied"), false);
        }

        comm.signupDone();

        const auto online = waitForCondition([&] {
            return comm.status() == ServerCommAccess::Status::ONLINE;
        }, 240000);
        const auto synced = online && waitForCondition(allSyncModelsValid, 240000);

        result.insert(QStringLiteral("online"), online);
        result.insert(QStringLiteral("synced"), synced);
        result.insert(QStringLiteral("signupStatus"), signupStatus(comm));
        result.insert(QStringLiteral("signupUrl"), signup_url);

        const auto have_db_info = synced && (template_name.isEmpty()
            ? waitForAndMergeDbInfo(result, core, 120000)
            : waitForAndMergeDbInfoMatching(result, core, 120000, [](const QJsonObject& info) {
                return info.value(QStringLiteral("numNodes")).toInt() > 0;
            }));
        result.insert(QStringLiteral("haveDbInfo"), have_db_info);

        printJson(result);
        fastExit(synced && have_db_info ? 0 : 8);
    }

    if (command == QStringLiteral("request-otp")) {
        auto result = baseResult(command, device_name, core, comm);

        const auto online = waitForCondition([&] {
            return comm.status() == ServerCommAccess::Status::ONLINE;
        }, 240000);
        const auto synced = online && waitForCondition(allSyncModelsValid, 240000);

        result.insert(QStringLiteral("online"), online);
        result.insert(QStringLiteral("synced"), synced);

        if (!synced) {
            printJson(result);
            fastExit(9);
        }

        OtpModel otp_model{core};
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        bool otp_ready = false;

        QObject::connect(&otp_model, &OtpModel::otpChanged, &loop, [&] {
            otp_ready = !otp_model.property("otp").toString().isEmpty();
            if (otp_ready) {
                loop.quit();
            }
        });
        QObject::connect(&otp_model, &OtpModel::errorChanged, &loop, [&] {
            if (!otp_model.property("error").toString().isEmpty()) {
                loop.quit();
            }
        });
        QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
            loop.quit();
        });

        otp_model.requestOtpForNewDevice();
        timeout.start(120000);
        loop.exec();

        const auto otp = otp_model.property("otp").toString();
        const auto email = otp_model.property("email").toString();
        const auto error = otp_model.property("error").toString();

        result.insert(QStringLiteral("otp"), otp);
        result.insert(QStringLiteral("email"), email);
        result.insert(QStringLiteral("error"), error);
        result.insert(QStringLiteral("otpReady"), !otp.isEmpty());

        printJson(result);
        fastExit(!otp.isEmpty() && !email.isEmpty() ? 0 : 10);
    }

    if (command == QStringLiteral("add-device-with-otp")) {
        if (signup_url.isEmpty() || user_email.isEmpty() || otp_value.isEmpty()) {
            fprintf(stderr, "add-device-with-otp requires --signup-url, --user-email and --otp\n");
            return 2;
        }

        comm.setSignupServerAddress(signup_url);
        const auto have_info = waitForCondition([&] {
            return signupStatus(comm) == ServerComm::SIGNUP_HAVE_INFO;
        }, 120000);
        if (!have_info) {
            fprintf(stderr, "Timed out waiting for signup server info\n");
            return 6;
        }

        const auto device_label = requested_device_name.isEmpty() ? device_name : requested_device_name;
        comm.addDeviceWithOtp(otp_value, user_email, device_label);

        const auto signed_up = waitForCondition([&] {
            const auto status = signupStatus(comm);
            return status == ServerComm::SIGNUP_SUCCESS
                || status == ServerComm::SIGNUP_OK
                || status == ServerComm::SIGNUP_ERROR;
        }, 240000);
        if (!signed_up || signupStatus(comm) == ServerComm::SIGNUP_ERROR) {
            fprintf(stderr,
                    "Add-device failed: signupStatus=%d email=%s otp_len=%d\n",
                    signupStatus(comm),
                    user_email.toUtf8().constData(),
                    static_cast<int>(otp_value.size()));
            return 12;
        }

        comm.signupDone();

        auto result = baseResult(command, device_name, core, comm);
        const auto online = waitForCondition([&] {
            return comm.status() == ServerCommAccess::Status::ONLINE;
        }, 240000);
        const auto synced = online && waitForCondition(allSyncModelsValid, 240000);

        result.insert(QStringLiteral("online"), online);
        result.insert(QStringLiteral("synced"), synced);
        result.insert(QStringLiteral("signupStatus"), signupStatus(comm));
        result.insert(QStringLiteral("signupUrl"), signup_url);
        result.insert(QStringLiteral("email"), user_email);

        const auto have_db_info = synced && waitForAndMergeDbInfo(result, core, 120000);
        result.insert(QStringLiteral("haveDbInfo"), have_db_info);

        printJson(result);
        fastExit(synced && have_db_info ? 0 : 13);
    }

    if (command == QStringLiteral("wait-ready")) {
        auto result = baseResult(command, device_name, core, comm);
        if (!result.value(QStringLiteral("hasServerUrl")).toBool()) {
            result.insert(QStringLiteral("synced"), false);
            printJson(result);
            return 0;
        }

        const auto online = waitForCondition([&] {
            return comm.status() == ServerCommAccess::Status::ONLINE;
        }, 120000);
        const auto synced = online && waitForCondition(allSyncModelsValid, 120000);

        result.insert(QStringLiteral("online"), online);
        result.insert(QStringLiteral("synced"), synced);
        result.insert(QStringLiteral("serverStatus"), static_cast<int>(comm.status()));

        const auto have_info = synced && waitForAndMergeDbInfo(result, core, 120000);
        result.insert(QStringLiteral("haveDbInfo"), have_info);

        printJson(result);
        fastExit(synced && have_info ? 0 : 5);
    }

    if (command == QStringLiteral("disconnect")) {
        auto result = baseResult(command, device_name, core, comm);
        const auto online_or_offline = waitForCondition([&] {
            return comm.status() == ServerCommAccess::Status::ONLINE
                || comm.status() == ServerCommAccess::Status::MANUAL_OFFLINE;
        }, 120000);
        result.insert(QStringLiteral("readyForDisconnect"), online_or_offline);

        if (comm.status() == ServerCommAccess::Status::ONLINE) {
            comm.toggleConnect();
        }

        const auto disconnected = waitForCondition([&] {
            return comm.status() == ServerCommAccess::Status::MANUAL_OFFLINE;
        }, 60000);
        result.insert(QStringLiteral("disconnected"), disconnected);
        result.insert(QStringLiteral("serverStatus"), static_cast<int>(comm.status()));
        printJson(result);
        fastExit(disconnected ? 0 : 14);
    }

    if (command == QStringLiteral("reconnect")) {
        auto result = baseResult(command, device_name, core, comm);
        if (comm.status() == ServerCommAccess::Status::MANUAL_OFFLINE) {
            comm.toggleConnect();
        } else if (comm.status() != ServerCommAccess::Status::ONLINE) {
            comm.start();
        }

        const auto online = waitForCondition([&] {
            return comm.status() == ServerCommAccess::Status::ONLINE;
        }, 240000);
        const auto synced = online && waitForCondition(allSyncModelsValid, 240000);
        result.insert(QStringLiteral("online"), online);
        result.insert(QStringLiteral("synced"), synced);
        result.insert(QStringLiteral("serverStatus"), static_cast<int>(comm.status()));
        const auto have_info = synced && waitForAndMergeDbInfo(result, core, 120000);
        result.insert(QStringLiteral("haveDbInfo"), have_info);
        printJson(result);
        fastExit(synced && have_info ? 0 : 15);
    }

    if (command == QStringLiteral("force-full-sync")) {
        auto result = baseResult(command, device_name, core, comm);
        if (!result.value(QStringLiteral("hasServerUrl")).toBool()) {
            result.insert(QStringLiteral("synced"), false);
            printJson(result);
            fastExit(19);
        }

        bool saw_initial_sync = false;
        bool saw_online_after_resync = false;
        bool saw_data_updated = false;
        QMetaObject::Connection status_conn;
        QMetaObject::Connection data_conn;
        status_conn = QObject::connect(&comm, &ServerCommAccess::statusChanged, &app, [&] {
            if (comm.status() == ServerCommAccess::Status::INITIAL_SYNC) {
                saw_initial_sync = true;
            } else if (saw_initial_sync && comm.status() == ServerCommAccess::Status::ONLINE) {
                saw_online_after_resync = true;
            }
        });
        data_conn = QObject::connect(&comm, &ServerCommAccess::dataUpdated, &app, [&] {
            if (saw_initial_sync) {
                saw_data_updated = true;
            }
        });

        const auto ready_for_resync = waitForCondition([&] {
            return comm.status() == ServerCommAccess::Status::ONLINE
                || comm.status() == ServerCommAccess::Status::MANUAL_OFFLINE;
        }, 120000);
        result.insert(QStringLiteral("readyForResync"), ready_for_resync);

        comm.resync();

        const auto online = waitForCondition([&] {
            return comm.status() == ServerCommAccess::Status::ONLINE;
        }, 240000);
        const auto synced = online && waitForCondition([&] {
            return saw_initial_sync
                && saw_online_after_resync
                && saw_data_updated
                && allSyncModelsValid();
        }, 240000);
        QObject::disconnect(status_conn);
        QObject::disconnect(data_conn);
        result.insert(QStringLiteral("online"), online);
        result.insert(QStringLiteral("synced"), synced);
        result.insert(QStringLiteral("serverStatus"), static_cast<int>(comm.status()));
        result.insert(QStringLiteral("fullResyncRequested"), true);
        result.insert(QStringLiteral("sawInitialSync"), saw_initial_sync);
        result.insert(QStringLiteral("sawOnlineAfterResync"), saw_online_after_resync);
        result.insert(QStringLiteral("sawDataUpdated"), saw_data_updated);
        const auto have_info = synced && waitForAndMergeDbInfo(result, core, 120000);
        result.insert(QStringLiteral("haveDbInfo"), have_info);

        printJson(result);
        fastExit(synced && have_info ? 0 : 20);
    }

    if (command == QStringLiteral("apply-scripted-batch")) {
        if (batch_name.isEmpty()) {
            fprintf(stderr, "apply-scripted-batch requires --batch\n");
            return 2;
        }

        auto result = baseResult(command, device_name, core, comm);
        const auto online = waitForCondition([&] {
            return comm.status() == ServerCommAccess::Status::ONLINE;
        }, 240000);
        const auto synced = online && waitForCondition(allSyncModelsValid, 240000);
        result.insert(QStringLiteral("online"), online);
        result.insert(QStringLiteral("syncedBeforeWrite"), synced);
        if (!synced) {
            printJson(result);
            std::_Exit(16);
        }

        const auto baseline_info = loadDbInfo(core);
        if (!baseline_info) {
            printJson(result);
            std::_Exit(17);
        }

        const auto category = makeCategory(device_name, batch_name);
        const auto node = makeNode(batch_name, device_name);
        action_categories.createCategory(category);

        QString persisted_category_id;
        const auto category_persisted = waitForCondition([&] {
            persisted_category_id = findCategoryIdByName(action_categories, category.name());
            return !persisted_category_id.isEmpty();
        }, 120000);

        comm.addNode(node);
        const auto node_persisted = waitForCondition([&] {
            const auto current = loadDbInfo(core);
            if (!current) {
                return false;
            }

            return current->value(QStringLiteral("numNodes")).toInt()
                >= baseline_info->value(QStringLiteral("numNodes")).toInt() + 1;
        }, 120000);

        auto action = makeAction(batch_name, device_name, node, category);
        action.setCategory(persisted_category_id);
        if (category_persisted && node_persisted) {
            comm.addAction(action);
        }

        const auto persisted = waitForCondition([&] {
            const auto current = loadDbInfo(core);
            if (!current) {
                return false;
            }

            return current->value(QStringLiteral("numActionCategories")).toInt()
                    >= baseline_info->value(QStringLiteral("numActionCategories")).toInt() + 1
                && current->value(QStringLiteral("numNodes")).toInt()
                    >= baseline_info->value(QStringLiteral("numNodes")).toInt() + 1;
        }, 240000);

        const auto action_persisted = waitForCondition([&] {
            const auto current = loadDbInfo(core);
            if (!current) {
                return false;
            }

            return current->value(QStringLiteral("numActions")).toInt()
                >= baseline_info->value(QStringLiteral("numActions")).toInt() + 1;
        }, 240000);

        const auto queue_drained = waitForCondition([&] {
            const auto current = loadPendingRequestCount(core);
            return current.has_value() && *current == 0;
        }, 120000);

        result.insert(QStringLiteral("batch"), batch_name);
        result.insert(QStringLiteral("categoryPersisted"), category_persisted);
        result.insert(QStringLiteral("nodePersisted"), node_persisted);
        result.insert(QStringLiteral("persisted"), persisted);
        result.insert(QStringLiteral("actionPersisted"), action_persisted);
        result.insert(QStringLiteral("actionSubmitted"), category_persisted && node_persisted);
        result.insert(QStringLiteral("queueDrained"), queue_drained);
        result.insert(QStringLiteral("categoryId"), persisted_category_id);
        result.insert(QStringLiteral("nodeId"), node.uuid());
        result.insert(QStringLiteral("actionId"), action.id_proto());
        if (const auto pending_requests = loadPendingRequestCount(core); pending_requests) {
            result.insert(QStringLiteral("pendingRequests"), *pending_requests);
        }

        const auto have_db_info = tryMergeDbInfo(result, core);
        result.insert(QStringLiteral("haveDbInfo"), have_db_info);

        printJson(result);
        fastExit(category_persisted && node_persisted ? 0 : 18);
    }

    fprintf(stderr, "Unknown command: %s\n", command.toUtf8().constData());
    return 2;
}
#include <cstdlib>

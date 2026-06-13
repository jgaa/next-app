
#include <iostream>

#include "AcceptanceDeviceRunner.h"

#include <QCommandLineOption>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QMetaObject>
#include <QProtobufSerializer>
#include <QSettings>
#include <QTimer>
#include <QEventLoop>
#include <QThread>
#include <optional>
#include <unistd.h>

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

int g_result_fd = -1;
constexpr auto kDbInfoProbeTimeoutMs = 5000;

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

    auto runner_task = runner();
    (void)runner_task;
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

    auto runner_task = runner();
    (void)runner_task;
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
    if (g_result_fd >= 0) {
        if (::write(g_result_fd, json.constData(), static_cast<size_t>(json.size())) == json.size()) {
            ::close(g_result_fd);
            g_result_fd = -1;
            return;
        }
        fprintf(stderr, "Failed to write result to result fd\n");
    }

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

QJsonObject nodeToJson(const nextapp::pb::Node& node)
{
    QJsonObject object;
    object.insert(QStringLiteral("uuid"), node.uuid());
    object.insert(QStringLiteral("parent"), node.parent());
    object.insert(QStringLiteral("active"), node.active());
    object.insert(QStringLiteral("name"), node.name());
    object.insert(QStringLiteral("kind"), static_cast<int>(node.kind()));
    object.insert(QStringLiteral("descr"), node.descr());
    object.insert(QStringLiteral("version"), static_cast<int>(node.version()));
    object.insert(QStringLiteral("deleted"), node.deleted());
    object.insert(QStringLiteral("updated"), static_cast<qint64>(node.updated()));
    object.insert(QStringLiteral("excludeFromWeeklyReview"), node.excludeFromWeeklyReview());
    object.insert(QStringLiteral("category"), node.category());
    object.insert(QStringLiteral("updatedId"), static_cast<qint64>(node.updatedId()));
    return object;
}

std::optional<QJsonArray> loadNodeSnapshot(NextAppCore& core)
{
    static QString last_failure;
    const auto log_failure_once = [&](const QString& failure) {
        if (last_failure != failure) {
            last_failure = failure;
            LOG_DEBUG_N << "loadNodeSnapshot failure: " << failure;
        }
    };

    tl::expected<DbStore::QueryResult, DbStore::Error> query_result;
    const auto got_rows = waitForTask(
        core.db().query(QStringLiteral("SELECT data FROM node ORDER BY uuid")),
        &query_result,
        kDbInfoProbeTimeoutMs);
    if (!got_rows) {
        log_failure_once(QStringLiteral("node snapshot query timed out for %1").arg(core.db().dbPath()));
        return std::nullopt;
    }
    if (!query_result) {
        log_failure_once(QStringLiteral("node snapshot query failed for %1 error=%2")
                             .arg(core.db().dbPath())
                             .arg(static_cast<int>(query_result.error())));
        return std::nullopt;
    }

    QProtobufSerializer serializer;
    QJsonArray snapshot;
    for (const auto& row : query_result->rows) {
        if (row.isEmpty()) {
            log_failure_once(QStringLiteral("node snapshot query returned empty row for %1")
                                 .arg(core.db().dbPath()));
            return std::nullopt;
        }

        const auto data = row.front().toByteArray();
        nextapp::pb::Node node;
        if (!node.deserialize(&serializer, data)) {
            log_failure_once(QStringLiteral("node snapshot deserialize failed for %1")
                                 .arg(core.db().dbPath()));
            return std::nullopt;
        }

        auto node_object = nodeToJson(node);
        node_object.insert(
            QStringLiteral("dataSha1"),
            QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex()));
        snapshot.push_back(node_object);
    }

    last_failure.clear();
    return snapshot;
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

const char *signupStatusName(int status)
{
    switch (status) {
    case ServerComm::SIGNUP_NOT_STARTED:
        return "SIGNUP_NOT_STARTED";
    case ServerComm::SIGNUP_HAVE_INFO:
        return "SIGNUP_HAVE_INFO";
    case ServerComm::SIGNUP_SIGNING_UP:
        return "SIGNUP_SIGNING_UP";
    case ServerComm::SIGNUP_SUCCESS:
        return "SIGNUP_SUCCESS";
    case ServerComm::SIGNUP_OK:
        return "SIGNUP_OK";
    case ServerComm::SIGNUP_ERROR:
        return "SIGNUP_ERROR";
    default:
        return "SIGNUP_UNKNOWN";
    }
}

const char *serverStatusName(ServerCommAccess::Status status)
{
    switch (status) {
    case ServerCommAccess::Status::MANUAL_OFFLINE:
        return "MANUAL_OFFLINE";
    case ServerCommAccess::Status::OFFLINE:
        return "OFFLINE";
    case ServerCommAccess::Status::READY_TO_CONNECT:
        return "READY_TO_CONNECT";
    case ServerCommAccess::Status::CONNECTING:
        return "CONNECTING";
    case ServerCommAccess::Status::INITIAL_SYNC:
        return "INITIAL_SYNC";
    case ServerCommAccess::Status::ONLINE:
        return "ONLINE";
    case ServerCommAccess::Status::ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

bool connectToSignupServerWithRetry(ServerComm& comm,
                                    const QString& signup_url,
                                    int attempts,
                                    int wait_per_attempt_ms)
{
    for (int attempt = 0; attempt < attempts; ++attempt) {
        LOG_DEBUG_N << "Connecting to signup server attempt "
                    << (attempt + 1) << "/" << attempts
                    << " url=" << signup_url
                    << " wait_ms=" << wait_per_attempt_ms;
        comm.resetSignupStatus();
        comm.setSignupServerAddress(signup_url);
        const auto have_info = waitForCondition([&] {
            const auto status = signupStatus(comm);
            return status == ServerComm::SIGNUP_HAVE_INFO
                || status == ServerComm::SIGNUP_ERROR;
        }, wait_per_attempt_ms);
        LOG_DEBUG_N << "Signup server attempt "
                    << (attempt + 1)
                    << " completed have_info=" << have_info
                    << " signup_status=" << signupStatusName(signupStatus(comm))
                    << " messages=" << comm.property("messages").toString();
        if (have_info && signupStatus(comm) == ServerComm::SIGNUP_HAVE_INFO) {
            return true;
        }

        if (attempt + 1 < attempts) {
            QThread::msleep(250);
        }
    }

    return false;
}

void mergeObject(QJsonObject& target, const QJsonObject& source);

std::optional<QJsonObject> loadDbInfo(NextAppCore& core)
{
    static QString last_failure;
    const auto log_failure_once = [&](const QString& failure) {
        if (last_failure != failure) {
            last_failure = failure;
            LOG_DEBUG_N << "loadDbInfo failure: " << failure;
        }
    };
    const auto clear_failure = [&] {
        last_failure.clear();
    };

    tl::expected<DbStore::DbDataInfo, DbStore::Error> info_result;
    const auto got_info = waitForTask(core.db().getDbDataInfo(), &info_result, kDbInfoProbeTimeoutMs);
    if (!got_info) {
        log_failure_once(QStringLiteral("getDbDataInfo timed out for %1").arg(core.db().dbPath()));
        return std::nullopt;
    }
    if (!info_result) {
        log_failure_once(QStringLiteral("getDbDataInfo failed for %1 error=%2")
                             .arg(core.db().dbPath())
                             .arg(static_cast<int>(info_result.error())));
        return std::nullopt;
    }

    int num_day_colors = -1;
    for (const auto& table : info_result->tables) {
        if (table.name == QStringLiteral("Day Colors")) {
            num_day_colors = static_cast<int>(table.count);
            break;
        }
    }
    if (num_day_colors < 0) {
        log_failure_once(QStringLiteral("Day Colors table summary missing for %1")
                             .arg(core.db().dbPath()));
        return std::nullopt;
    }

    const auto& info = info_result->summary;
    QJsonObject table_hashes;
    for (const auto& table : info_result->tables) {
        table_hashes.insert(table.name, table.hash);
    }
    const auto node_snapshot = loadNodeSnapshot(core);
    clear_failure();
    QJsonObject result{
        {QStringLiteral("hash"), info.hash()},
        {QStringLiteral("numNodes"), static_cast<int>(info.numNodes())},
        {QStringLiteral("numActionCategories"), static_cast<int>(info.numActionCategories())},
        {QStringLiteral("numActions"), static_cast<int>(info.numActions())},
        {QStringLiteral("numDays"), static_cast<int>(info.numDays())},
        {QStringLiteral("numDayColors"), num_day_colors},
        {QStringLiteral("numWorkSessions"), static_cast<int>(info.numWorkSessions())},
        {QStringLiteral("numTimeBlocks"), static_cast<int>(info.numTimeBlocks())},
        {QStringLiteral("tableHashes"), table_hashes},
    };
    if (node_snapshot) {
        result.insert(QStringLiteral("nodeSnapshot"), *node_snapshot);
    }
    return result;
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

QJsonObject normalizeDbInfoForStability(QJsonObject value)
{
    value.remove(QStringLiteral("nodeSnapshot"));
    return value;
}

bool waitForAndMergeDbInfo(QJsonObject& target, NextAppCore& core, int timeout_ms)
{
    QJsonObject merged;
    QJsonObject stable_candidate;
    QJsonObject last_logged_candidate;
    QElapsedTimer stable_since;
    constexpr auto stable_window_ms = 750;

    const auto have_info = waitForCondition([&] {
        auto current = target;
        if (!tryMergeDbInfo(current, core)) {
            if (stable_since.isValid()
                && !stable_candidate.isEmpty()
                && stable_since.elapsed() >= stable_window_ms) {
                merged = stable_candidate;
                LOG_DEBUG_N << "waitForAndMergeDbInfo reusing last stable candidate after DB info retry failure. hash="
                            << merged.value(QStringLiteral("hash")).toString()
                            << " nodes=" << merged.value(QStringLiteral("numNodes")).toInt()
                            << " categories=" << merged.value(QStringLiteral("numActionCategories")).toInt()
                            << " actions=" << merged.value(QStringLiteral("numActions")).toInt()
                            << " days=" << merged.value(QStringLiteral("numDays")).toInt()
                            << " day_colors=" << merged.value(QStringLiteral("numDayColors")).toInt()
                            << " work_sessions=" << merged.value(QStringLiteral("numWorkSessions")).toInt()
                            << " time_blocks=" << merged.value(QStringLiteral("numTimeBlocks")).toInt();
                return true;
            }

            stable_candidate = {};
            stable_since.invalidate();
            return false;
        }

        const auto comparable_current = normalizeDbInfoForStability(current);
        const auto comparable_stable = normalizeDbInfoForStability(stable_candidate);
        if (comparable_stable != comparable_current) {
            stable_candidate = current;
            stable_since.restart();
            if (normalizeDbInfoForStability(last_logged_candidate) != comparable_current) {
                last_logged_candidate = current;
                LOG_DEBUG_N << "waitForAndMergeDbInfo candidate hash="
                            << stable_candidate.value(QStringLiteral("hash")).toString()
                            << " nodes=" << stable_candidate.value(QStringLiteral("numNodes")).toInt()
                            << " categories=" << stable_candidate.value(QStringLiteral("numActionCategories")).toInt()
                            << " actions=" << stable_candidate.value(QStringLiteral("numActions")).toInt()
                            << " days=" << stable_candidate.value(QStringLiteral("numDays")).toInt()
                            << " day_colors=" << stable_candidate.value(QStringLiteral("numDayColors")).toInt()
                            << " work_sessions=" << stable_candidate.value(QStringLiteral("numWorkSessions")).toInt()
                            << " time_blocks=" << stable_candidate.value(QStringLiteral("numTimeBlocks")).toInt();
            }
            return false;
        }

        if (!stable_since.isValid() || stable_since.elapsed() < stable_window_ms) {
            return false;
        }

        merged = stable_candidate;
        return true;
    }, timeout_ms);

    if (!have_info) {
        LOG_DEBUG_N << "waitForAndMergeDbInfo timed out last_candidate_hash="
                    << stable_candidate.value(QStringLiteral("hash")).toString()
                    << " nodes=" << stable_candidate.value(QStringLiteral("numNodes")).toInt()
                    << " categories=" << stable_candidate.value(QStringLiteral("numActionCategories")).toInt()
                    << " actions=" << stable_candidate.value(QStringLiteral("numActions")).toInt()
                    << " days=" << stable_candidate.value(QStringLiteral("numDays")).toInt()
                    << " day_colors=" << stable_candidate.value(QStringLiteral("numDayColors")).toInt()
                    << " work_sessions=" << stable_candidate.value(QStringLiteral("numWorkSessions")).toInt()
                    << " time_blocks=" << stable_candidate.value(QStringLiteral("numTimeBlocks")).toInt();
        return false;
    }

    LOG_DEBUG_N << "waitForAndMergeDbInfo stabilized hash="
                << merged.value(QStringLiteral("hash")).toString()
                << " nodes=" << merged.value(QStringLiteral("numNodes")).toInt()
                << " categories=" << merged.value(QStringLiteral("numActionCategories")).toInt()
                << " actions=" << merged.value(QStringLiteral("numActions")).toInt()
                << " days=" << merged.value(QStringLiteral("numDays")).toInt()
                << " day_colors=" << merged.value(QStringLiteral("numDayColors")).toInt()
                << " work_sessions=" << merged.value(QStringLiteral("numWorkSessions")).toInt()
                << " time_blocks=" << merged.value(QStringLiteral("numTimeBlocks")).toInt();
    target = merged;
    return true;
}

bool waitForAndMergeDbInfoMatching(QJsonObject& target,
                                   NextAppCore& core,
                                   int timeout_ms,
                                   const std::function<bool(const QJsonObject&)>& predicate)
{
    QJsonObject merged;
    QJsonObject stable_candidate;
    QElapsedTimer stable_since;
    constexpr auto stable_window_ms = 750;

    const auto have_info = waitForCondition([&] {
        auto current = target;
        if (!tryMergeDbInfo(current, core)) {
            stable_candidate = {};
            stable_since.invalidate();
            return false;
        }

        if (!predicate(current)) {
            stable_candidate = {};
            stable_since.invalidate();
            return false;
        }

        if (normalizeDbInfoForStability(stable_candidate) != normalizeDbInfoForStability(current)) {
            stable_candidate = current;
            stable_since.restart();
            return false;
        }

        if (!stable_since.isValid() || stable_since.elapsed() < stable_window_ms) {
            return false;
        }

        merged = stable_candidate;
        return true;
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

void mergeSyncDiagnostics(QJsonObject& target, const ServerComm& comm)
{
    mergeObject(target, comm.syncDiagnostics());
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

nextapp::pb::Node makeChildNode(const QString& batch_name,
                                const QString& device_name,
                                const QString& parent_uuid)
{
    auto node = makeNode(batch_name, device_name);
    node.setParent(parent_uuid);
    node.setName(QStringLiteral("Acceptance child %1 %2").arg(batch_name, device_name));
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

int runAcceptanceDevice(int argc, char** argv)
{
    logfault::LogManager::Instance().AddHandler(
        std::make_unique<logfault::StreamHandler>(std::clog, logfault::LogLevel::DEBUGGING));

    LOG_INFO_N << "Starting acceptance device helper with args: " << QStringList{argv + 1, argv + argc}.join(' ');

    QString workspace_root;
    QString device_name;
    QString command;
    QString signup_url;
    int result_fd = -1;
    QString user_name;
    QString user_email;
    QString company;
    QString requested_device_name;
    QString otp_value;
    QString batch_name;
    QString template_name;
    int batch_count = 1;
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
        if (arg == QStringLiteral("--result-fd") && i + 1 < argc) {
            result_fd = QString::fromLocal8Bit(argv[++i]).toInt();
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
        if (arg == QStringLiteral("--count") && i + 1 < argc) {
            batch_count = QString::fromLocal8Bit(argv[++i]).toInt();
            continue;
        }
        if (arg == QStringLiteral("--template-name") && i + 1 < argc) {
            template_name = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        if (arg == QStringLiteral("--help")) {
            fprintf(stdout, "Usage: nextappui_acceptance_device --workspace-root PATH --device-name NAME [--otp OTP] [--batch NAME] [--count N] [--template-name NAME] <prepare|probe-signup-server|wait-ready|signup-first-device|request-otp|add-device-with-otp|disconnect|reconnect|force-full-sync|apply-scripted-batch|apply-scripted-batches|apply-structural-node-delete>\n");
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
    g_result_fd = result_fd;

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

    LOG_INFO_N << "Acceptance helper starting command=" << command
               << " device=" << device_name
               << " workspace_root=" << workspace_root;

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
    QObject::connect(&comm, &ServerComm::signupStatusChanged, &app, [&] {
        LOG_DEBUG_N << "Signal signupStatusChanged status="
                    << signupStatusName(signupStatus(comm))
                    << " messages=" << comm.property("messages").toString();
    });
    QObject::connect(&comm, &ServerCommAccess::statusChanged, &app, [&] {
        LOG_DEBUG_N << "Signal statusChanged status="
                    << serverStatusName(comm.status())
                    << " status_code=" << static_cast<int>(comm.status());
    });
    QObject::connect(&comm, &ServerComm::messagesChanged, &app, [&] {
        LOG_DEBUG_N << "Signal messagesChanged messages="
                    << comm.property("messages").toString();
    });

    if (!waitForTaskVoid(core.modelsAreCreated(), 120000)) {
        fprintf(stderr, "Timed out while initializing NextAppCore models\n");
        return 4;
    }
    LOG_DEBUG_N << "Acceptance helper models initialized for command=" << command;

    if (command == QStringLiteral("prepare")) {
        printJson(baseResult(command, device_name, core, comm));
        fastExit(0);
    }

    if (command == QStringLiteral("probe-signup-server")) {
        if (signup_url.isEmpty()) {
            fprintf(stderr, "probe-signup-server requires --signup-url\n");
            return 2;
        }

        auto result = baseResult(command, device_name, core, comm);
        LOG_DEBUG_N << "Starting signup server probe for url=" << signup_url;
        const auto connected = connectToSignupServerWithRetry(comm, signup_url, 10, 3000);
        LOG_DEBUG_N << "Signup server probe completed connected=" << connected
                    << " signup_status=" << signupStatusName(signupStatus(comm));
        result.insert(QStringLiteral("signupUrl"), signup_url);
        result.insert(QStringLiteral("signupStatus"), signupStatus(comm));
        result.insert(QStringLiteral("connected"), connected);
        mergeSyncDiagnostics(result, comm);
        printJson(result);
        fastExit(connected ? 0 : 24);
    }

    if (command == QStringLiteral("signup-first-device")) {
        if (signup_url.isEmpty() || user_name.isEmpty() || user_email.isEmpty()) {
            fprintf(stderr, "signup-first-device requires --signup-url, --user-name and --user-email\n");
            return 2;
        }

        LOG_DEBUG_N << "signup-first-device: connecting to signup server url=" << signup_url;
        if (!connectToSignupServerWithRetry(comm, signup_url, 20, 15000)) {
            fprintf(stderr,
                    "Failed waiting for signup server info: status=%s messages=%s url=%s\n",
                    signupStatusName(signupStatus(comm)),
                    comm.property("messages").toString().toUtf8().constData(),
                    signup_url.toUtf8().constData());
            return 6;
        }

        const auto device_label = requested_device_name.isEmpty() ? device_name : requested_device_name;
        LOG_DEBUG_N << "signup-first-device: issuing signup"
                    << " user_name=" << user_name
                    << " user_email=" << user_email
                    << " company=" << company
                    << " device_label=" << device_label
                    << " region=" << region;
        comm.signup(user_name, user_email, company, device_label, region);

        LOG_DEBUG_N << "signup-first-device: waiting for signup completion";
        const auto signed_up = waitForCondition([&] {
            const auto status = signupStatus(comm);
            return status == ServerComm::SIGNUP_SUCCESS
                || status == ServerComm::SIGNUP_OK
                || status == ServerComm::SIGNUP_ERROR;
        }, 240000);
        LOG_DEBUG_N << "signup-first-device: signup wait completed"
                    << " signed_up=" << signed_up
                    << " signup_status=" << signupStatusName(signupStatus(comm))
                    << " messages=" << comm.property("messages").toString();
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

        LOG_DEBUG_N << "signup-first-device: calling signupDone()";
        comm.signupDone();

        LOG_DEBUG_N << "signup-first-device: waiting for ONLINE state";
        const auto online = waitForCondition([&] {
            return comm.status() == ServerCommAccess::Status::ONLINE;
        }, 240000);
        LOG_DEBUG_N << "signup-first-device: ONLINE wait completed"
                    << " online=" << online
                    << " server_status=" << static_cast<int>(comm.status());
        LOG_DEBUG_N << "signup-first-device: waiting for sync models";
        const auto synced = online && waitForCondition(allSyncModelsValid, 240000);
        LOG_DEBUG_N << "signup-first-device: sync wait completed synced=" << synced;

        result.insert(QStringLiteral("online"), online);
        result.insert(QStringLiteral("synced"), synced);
        result.insert(QStringLiteral("signupStatus"), signupStatus(comm));
        result.insert(QStringLiteral("signupUrl"), signup_url);

        LOG_DEBUG_N << "signup-first-device: waiting for DB info";
        const auto have_db_info = synced && (template_name.isEmpty()
            ? waitForAndMergeDbInfo(result, core, 120000)
            : waitForAndMergeDbInfoMatching(result, core, 120000, [](const QJsonObject& info) {
                return info.value(QStringLiteral("numNodes")).toInt() > 0;
            }));
        LOG_DEBUG_N << "signup-first-device: DB info wait completed have_db_info=" << have_db_info;
        result.insert(QStringLiteral("haveDbInfo"), have_db_info);
        mergeSyncDiagnostics(result, comm);

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
        mergeSyncDiagnostics(result, comm);

        printJson(result);
        fastExit(!otp.isEmpty() && !email.isEmpty() ? 0 : 10);
    }

    if (command == QStringLiteral("add-device-with-otp")) {
        if (signup_url.isEmpty() || user_email.isEmpty() || otp_value.isEmpty()) {
            fprintf(stderr, "add-device-with-otp requires --signup-url, --user-email and --otp\n");
            return 2;
        }

        if (!connectToSignupServerWithRetry(comm, signup_url, 20, 15000)) {
            fprintf(stderr,
                    "Failed waiting for signup server info: status=%s messages=%s url=%s\n",
                    signupStatusName(signupStatus(comm)),
                    comm.property("messages").toString().toUtf8().constData(),
                    signup_url.toUtf8().constData());
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
        mergeSyncDiagnostics(result, comm);

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
        mergeSyncDiagnostics(result, comm);

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
        mergeSyncDiagnostics(result, comm);
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
        mergeSyncDiagnostics(result, comm);
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
        mergeSyncDiagnostics(result, comm);

        printJson(result);
        fastExit(synced && have_info ? 0 : 20);
    }

    if (command == QStringLiteral("apply-scripted-batch")
        || command == QStringLiteral("apply-scripted-batches")) {
        if (batch_name.isEmpty()) {
            fprintf(stderr, "%s requires --batch\n", command.toUtf8().constData());
            return 2;
        }
        if (batch_count <= 0) {
            batch_count = 1;
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

        QString last_category_id;
        QString last_node_id;
        QString last_action_id;
        bool category_persisted = true;
        bool node_persisted = true;
        bool action_submitted = true;
        for (int batch_index = 0; batch_index < batch_count; ++batch_index) {
            const auto effective_batch = batch_count == 1
                ? batch_name
                : QStringLiteral("%1-%2").arg(batch_name).arg(batch_index + 1);
            const auto category = makeCategory(device_name, effective_batch);
            const auto node = makeNode(effective_batch, device_name);
            action_categories.createCategory(category);

            QString persisted_category_id;
            const auto category_ready = waitForCondition([&] {
                persisted_category_id = findCategoryIdByName(action_categories, category.name());
                return !persisted_category_id.isEmpty();
            }, 120000);
            category_persisted = category_persisted && category_ready;

            comm.addNode(node);
            const auto node_ready = waitForCondition([&] {
                const auto current = loadDbInfo(core);
                if (!current) {
                    return false;
                }

                return current->value(QStringLiteral("numNodes")).toInt()
                    >= baseline_info->value(QStringLiteral("numNodes")).toInt() + batch_index + 1;
            }, 120000);
            node_persisted = node_persisted && node_ready;

            auto action = makeAction(effective_batch, device_name, node, category);
            action.setCategory(persisted_category_id);
            if (category_ready && node_ready) {
                comm.addAction(action);
            } else {
                action_submitted = false;
            }

            last_category_id = persisted_category_id;
            last_node_id = node.uuid();
            last_action_id = action.id_proto();
        }

        const auto persisted = waitForCondition([&] {
            const auto current = loadDbInfo(core);
            if (!current) {
                return false;
            }

            return current->value(QStringLiteral("numActionCategories")).toInt()
                    >= baseline_info->value(QStringLiteral("numActionCategories")).toInt() + batch_count
                && current->value(QStringLiteral("numNodes")).toInt()
                    >= baseline_info->value(QStringLiteral("numNodes")).toInt() + batch_count;
        }, 240000);

        const auto action_persisted = waitForCondition([&] {
            const auto current = loadDbInfo(core);
            if (!current) {
                return false;
            }

            return current->value(QStringLiteral("numActions")).toInt()
                >= baseline_info->value(QStringLiteral("numActions")).toInt() + batch_count;
        }, batch_count > 1 ? 600000 : 240000);

        const auto queue_drained = waitForCondition([&] {
            const auto current = loadPendingRequestCount(core);
            return current.has_value() && *current == 0;
        }, 120000);

        result.insert(QStringLiteral("batch"), batch_name);
        result.insert(QStringLiteral("batchCount"), batch_count);
        result.insert(QStringLiteral("categoryPersisted"), category_persisted);
        result.insert(QStringLiteral("nodePersisted"), node_persisted);
        result.insert(QStringLiteral("persisted"), persisted);
        result.insert(QStringLiteral("actionPersisted"), action_persisted);
        result.insert(QStringLiteral("actionSubmitted"), action_submitted);
        result.insert(QStringLiteral("queueDrained"), queue_drained);
        result.insert(QStringLiteral("categoryId"), last_category_id);
        result.insert(QStringLiteral("nodeId"), last_node_id);
        result.insert(QStringLiteral("actionId"), last_action_id);
        if (const auto pending_requests = loadPendingRequestCount(core); pending_requests) {
            result.insert(QStringLiteral("pendingRequests"), *pending_requests);
        }

        const auto have_db_info = tryMergeDbInfo(result, core);
        result.insert(QStringLiteral("haveDbInfo"), have_db_info);
        mergeSyncDiagnostics(result, comm);

        printJson(result);
        fastExit(category_persisted && node_persisted && action_submitted ? 0 : 18);
    }

    if (command == QStringLiteral("apply-structural-node-delete")) {
        if (batch_name.isEmpty()) {
            fprintf(stderr, "apply-structural-node-delete requires --batch\n");
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
            mergeSyncDiagnostics(result, comm);
            printJson(result);
            std::_Exit(22);
        }

        const auto category = makeCategory(device_name, batch_name);
        const auto parent = makeNode(batch_name, device_name);
        const auto child = makeChildNode(batch_name, device_name, parent.uuid());
        action_categories.createCategory(category);

        QString persisted_category_id;
        const auto category_persisted = waitForCondition([&] {
            persisted_category_id = findCategoryIdByName(action_categories, category.name());
            return !persisted_category_id.isEmpty();
        }, 120000);

        comm.addNode(parent);
        const auto parent_persisted = waitForCondition([&] {
            return MainTreeModel::instance()->nodeFromUuid(parent.uuid()) != nullptr;
        }, 120000);

        comm.addNode(child);
        const auto child_persisted = waitForCondition([&] {
            return MainTreeModel::instance()->nodeFromUuid(child.uuid()) != nullptr;
        }, 120000);

        auto action = makeAction(batch_name, device_name, child, category);
        action.setCategory(persisted_category_id);
        if (category_persisted && parent_persisted && child_persisted) {
            comm.addAction(action);
        }

        const auto action_persisted = waitForCondition([&] {
            const auto current = loadDbInfo(core);
            return current
                && current->value(QStringLiteral("numActions")).toInt() > 0
                && current->value(QStringLiteral("numNodes")).toInt() > 1;
        }, 240000);

        bool saw_server_resync = false;
        auto update_conn = QObject::connect(&comm, &ServerCommAccess::onUpdate, &app,
                                            [&](const std::shared_ptr<nextapp::pb::Update>& update) {
            if (update && update->hasResync() && update->resync()) {
                saw_server_resync = true;
            }
        });

        comm.deleteNode(QUuid{parent.uuid()});
        const auto resync_requested = waitForCondition([&] {
            return saw_server_resync
                || core.settings().value(QStringLiteral("sync/incremental_repair"), false).toBool()
                || comm.status() == ServerCommAccess::Status::OFFLINE
                || comm.status() == ServerCommAccess::Status::ERROR;
        }, 240000);
        QObject::disconnect(update_conn);

        result.insert(QStringLiteral("categoryPersisted"), category_persisted);
        result.insert(QStringLiteral("parentPersisted"), parent_persisted);
        result.insert(QStringLiteral("childPersisted"), child_persisted);
        result.insert(QStringLiteral("actionPersisted"), action_persisted);
        result.insert(QStringLiteral("serverResyncObserved"), saw_server_resync);
        result.insert(QStringLiteral("resyncRequested"), resync_requested);
        result.insert(QStringLiteral("parentNodeId"), parent.uuid());
        result.insert(QStringLiteral("childNodeId"), child.uuid());
        result.insert(QStringLiteral("actionId"), action.id_proto());
        result.insert(QStringLiteral("incrementalRepairPending"),
                      core.settings().value(QStringLiteral("sync/incremental_repair"), false).toBool());
        mergeSyncDiagnostics(result, comm);
        printJson(result);
        fastExit(category_persisted && parent_persisted && child_persisted && action_persisted && resync_requested ? 0 : 23);
    }

    fprintf(stderr, "Unknown command: %s\n", command.toUtf8().constData());
    return 2;
}
#include <cstdlib>

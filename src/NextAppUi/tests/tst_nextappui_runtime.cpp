#include <optional>
#include <memory>

#include <QEventLoop>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVariantMap>
#include <QtTest>

#include "AppInstanceMgr.h"
#include "ActionCategoriesModel.h"
#include "ActionInfoCache.h"
#include "ActionsModel.h"
#include "CalendarCache.h"
#include "DbStore.h"
#include "DevicesModel.h"
#include "ImportExportModel.h"
#include "MainTreeModel.h"
#include "MaterialDesignStyling.h"
#include "NotificationsModel.h"
#include "OtpModel.h"
#include "ServerComm.h"
#include "ServerSynchedCahce.h"
#include "UseCaseTemplates.h"
#include "WorkCache.h"

#include "tests/TestRuntimeSupport.h"

namespace {

template <typename T>
T waitForTask(QCoro::Task<T> task)
{
    QEventLoop loop;
    std::optional<T> result;
    QString error_message;
    bool completed = false;

    std::move(task).then([&](T value) {
        result = std::move(value);
        completed = true;
        if (loop.isRunning()) {
            loop.quit();
        }
    }, [&](const std::exception& ex) {
        error_message = QString::fromUtf8(ex.what());
        completed = true;
        if (loop.isRunning()) {
            loop.quit();
        }
    });

    if (!completed) {
        loop.exec();
    }

    if (!error_message.isEmpty()) {
        qFatal("Task failed unexpectedly: %s", qPrintable(error_message));
    }

    return std::move(*result);
}

void waitForTask(QCoro::Task<void> task)
{
    QEventLoop loop;
    QString error_message;
    bool completed = false;

    std::move(task).then([&]() {
        completed = true;
        if (loop.isRunning()) {
            loop.quit();
        }
    }, [&](const std::exception& ex) {
        error_message = QString::fromUtf8(ex.what());
        completed = true;
        if (loop.isRunning()) {
            loop.quit();
        }
    });

    if (!completed) {
        loop.exec();
    }

    if (!error_message.isEmpty()) {
        qFatal("Task failed unexpectedly: %s", qPrintable(error_message));
    }
}

nextapp::pb::Update makeNodeUpdate(const QString& uuid)
{
    nextapp::pb::Update update;
    update.setOp(nextapp::pb::Update::Operation::UPDATED);

    nextapp::pb::Node node;
    node.setUuid(uuid);
    node.setName("Node-" + uuid);
    update.setNode(node);

    return update;
}

nextapp::pb::Update makeNodeUpdate(
    nextapp::pb::Update::Operation op,
    const nextapp::pb::Node& node)
{
    nextapp::pb::Update update;
    update.setOp(op);
    update.setNode(node);
    return update;
}

nextapp::pb::ActionCategory makeActionCategory(
    QString id,
    QString name,
    QString color,
    QString icon,
    quint32 version)
{
    nextapp::pb::ActionCategory category;
    category.setId_proto(std::move(id));
    category.setName(std::move(name));
    category.setColor(std::move(color));
    category.setIcon(std::move(icon));
    category.setDescr(QStringLiteral("Descr %1").arg(category.id_proto()));
    category.setVersion(version);
    return category;
}

nextapp::pb::Update makeActionCategoryUpdate(
    nextapp::pb::Update::Operation op,
    const nextapp::pb::ActionCategory& category)
{
    nextapp::pb::Update update;
    update.setOp(op);
    update.setActionCategory(category);
    return update;
}

nextapp::pb::Notification makeNotification(
    quint64 id,
    QString subject,
    quint64 created_unix_time,
    quint64 updated,
    quint64 updated_id,
    std::optional<quint64> valid_to_unix_time = std::nullopt)
{
    nextapp::pb::Notification notification;
    notification.setId_proto(id);
    notification.setSubject(std::move(subject));
    notification.setMessage(QStringLiteral("Message %1").arg(id));
    notification.setSenderType(nextapp::pb::Notification::SenderType::SYSTEM);
    notification.setSenderId(QStringLiteral("system"));
    notification.setKind(nextapp::pb::Notification::Kind::INFO);
    notification.setData(QStringLiteral("payload-%1").arg(id));
    notification.setUpdated(updated);
    notification.setUpdatedId(updated_id);

    common::Time created_time;
    created_time.setUnixTime(created_unix_time);
    notification.setCreatedTime(std::move(created_time));

    common::Uuid uuid;
    uuid.setUuid(QUuid::createUuid().toString(QUuid::WithoutBraces));
    notification.setUuid(std::move(uuid));

    if (valid_to_unix_time) {
        common::Time valid_to;
        valid_to.setUnixTime(*valid_to_unix_time);
        notification.setValidTo(std::move(valid_to));
    }

    return notification;
}

nextapp::pb::Node makeNode(
    QString uuid,
    QString name,
    nextapp::pb::Node::Kind kind,
    QString parent = {},
    quint64 updated = 0,
    quint64 updated_id = 0)
{
    nextapp::pb::Node node;
    node.setUuid(std::move(uuid));
    node.setName(std::move(name));
    node.setKind(kind);
    node.setParent(std::move(parent));
    node.setActive(true);
    node.setUpdated(updated);
    node.setUpdatedId(updated_id);
    return node;
}

nextapp::pb::Action makeAction(
    QString id,
    QString node_id,
    QString name,
    int version,
    quint64 updated,
    quint64 updated_id,
    std::optional<QString> origin = std::nullopt,
    QStringList tags = {})
{
    nextapp::pb::Action action;
    action.setId_proto(std::move(id));
    action.setNode(std::move(node_id));
    if (origin) {
        action.setOrigin(std::move(*origin));
    }
    action.setStatus(nextapp::pb::ActionStatusGadget::ActionStatus::ACTIVE);
    action.setFavorite(false);
    action.setName(std::move(name));

    nextapp::pb::Date created;
    created.setYear(2026);
    created.setMonth(2);
    created.setMday(25);
    action.setCreatedDate(created);

    nextapp::pb::Priority priority;
    priority.setPriority(nextapp::pb::ActionPriorityGadget::ActionPriority::PRI_HIGH);
    priority.setScore(42);
    action.setDynamicPriority(priority);

    action.setDescr(QStringLiteral("Descr %1").arg(action.id_proto()));
    action.setHasDescr(true);
    action.setTimeEstimate(30);
    action.setDifficulty(nextapp::pb::ActionDifficultyGadget::ActionDifficulty::NORMAL);
    action.setKind(nextapp::pb::ActionKindGadget::ActionKind::AC_ACTIVE);
    action.setVersion(version);
    action.setCategory(QStringLiteral("default"));
    action.setTimeSpent(5);
    action.setUpdated(static_cast<qint64>(updated));
    action.setUpdatedId(updated_id);
    action.setTags(std::move(tags));

    nextapp::pb::Due due;
    due.setKind(nextapp::pb::ActionDueKindGadget::ActionDueKind::UNSET);
    due.setTimezone(QStringLiteral("UTC"));
    action.setDue(due);
    return action;
}

nextapp::pb::Update makeActionUpdate(
    nextapp::pb::Update::Operation op,
    const nextapp::pb::Action& action)
{
    nextapp::pb::Update update;
    update.setOp(op);
    update.setAction(action);
    return update;
}

nextapp::pb::WorkEvent makeWorkEvent(
    nextapp::pb::WorkEvent_QtProtobufNested::Kind kind,
    quint64 time,
    std::optional<quint64> end = std::nullopt,
    std::optional<quint64> start = std::nullopt,
    std::optional<quint64> duration = std::nullopt,
    std::optional<quint64> paused = std::nullopt,
    std::optional<QString> name = std::nullopt)
{
    nextapp::pb::WorkEvent event;
    event.setKind(kind);
    event.setTime(time);
    if (end) {
        event.setEnd(*end);
    }
    if (start) {
        event.setStart(*start);
    }
    if (duration) {
        event.setDuration(*duration);
    }
    if (paused) {
        event.setPaused(*paused);
    }
    if (name) {
        event.setName(std::move(*name));
    }
    return event;
}

nextapp::pb::WorkSession makeWorkSession(
    QString id,
    QString action_id,
    QString name,
    QList<nextapp::pb::WorkEvent> events,
    nextapp::pb::WorkSession::State state = nextapp::pb::WorkSession::State::ACTIVE,
    quint64 updated = 1,
    quint64 updated_id = 1)
{
    nextapp::pb::WorkSession work;
    work.setId_proto(std::move(id));
    work.setAction(std::move(action_id));
    work.setName(std::move(name));
    work.setState(state);
    work.setEvents(std::move(events));
    work.setUpdated(updated);
    work.setUpdatedId(updated_id);
    return work;
}

nextapp::pb::Update makeWorkUpdate(
    nextapp::pb::Update::Operation op,
    const nextapp::pb::WorkSession& work)
{
    nextapp::pb::Update update;
    update.setOp(op);
    update.setWork(work);
    return update;
}

nextapp::pb::Status makeOkStatus()
{
    nextapp::pb::Status status;
    status.setError(nextapp::pb::ErrorGadget::Error::OK);
    return status;
}

nextapp::pb::Status makeOtpStatus(QString otp, QString email)
{
    nextapp::pb::OtpResponse response;
    response.setOtp(std::move(otp));
    response.setEmail(std::move(email));

    auto status = makeOkStatus();
    status.setOtpResponse(std::move(response));
    return status;
}

nextapp::pb::Device makeDevice(
    QString id,
    QString name,
    bool enabled,
    quint64 created_seconds,
    quint64 last_seen_seconds,
    quint64 num_sessions)
{
    nextapp::pb::Device device;
    device.setId_proto(std::move(id));
    device.setUser(QStringLiteral("tester@example.com"));
    device.setName(std::move(name));
    device.setHostName(QStringLiteral("host"));
    device.setOs(QStringLiteral("Linux"));
    device.setOsVersion(QStringLiteral("6.0"));
    device.setAppVersion(QStringLiteral("1.2.3"));
    device.setProductType(QStringLiteral("desktop"));
    device.setProductVersion(QStringLiteral("2026.1"));
    device.setArch(QStringLiteral("x86_64"));
    device.setPrettyName(QStringLiteral("Test Device"));
    device.setEnabled(enabled);
    device.setNumSessions(num_sessions);

    nextapp::pb::Timestamp created;
    created.setSeconds(created_seconds);
    device.setCreated(created);

    nextapp::pb::Timestamp last_seen;
    last_seen.setSeconds(last_seen_seconds);
    device.setLastSeen(last_seen);

    return device;
}

nextapp::pb::Status makeDevicesStatus(const QList<nextapp::pb::Device>& devices)
{
    nextapp::pb::Devices payload;
    payload.setDevices(devices);

    auto status = makeOkStatus();
    status.setDevices(std::move(payload));
    return status;
}

nextapp::pb::Status makeActionCategoriesStatus(const QList<nextapp::pb::ActionCategory>& categories)
{
    nextapp::pb::ActionCategories payload;
    payload.setCategories(categories);

    auto status = makeOkStatus();
    status.setActionCategories(std::move(payload));
    return status;
}

nextapp::pb::Status makeUserStatus(QString uuid, QString name, QString email, bool has_more)
{
    nextapp::pb::User user;
    user.setUuid(std::move(uuid));
    user.setName(std::move(name));
    user.setEmail(std::move(email));

    auto status = makeOkStatus();
    status.setUser(std::move(user));
    status.setHasMore(has_more);
    return status;
}

std::unique_ptr<DbStore> makeInitializedDb(const QString& db_name)
{
    auto db = std::make_unique<DbStore>(db_name);
    if (!waitForTask(db->init())) {
        qFatal("Failed to initialize test database");
    }
    return db;
}

QDateTime utcDateTime(QDate date, QTime time)
{
    return QDateTime(date, time, QTimeZone::UTC);
}

void ensureSyncTestTable(DbStore& db)
{
    QVERIFY(waitForTask(db.query(
        "CREATE TABLE IF NOT EXISTS sync_test ("
        "id INTEGER PRIMARY KEY, "
        "updated INTEGER NOT NULL, "
        "updated_id INTEGER NOT NULL, "
        "name TEXT NOT NULL)")));
    QVERIFY(waitForTask(db.query("DELETE FROM sync_test")));
}

void insertMinimalAction(DbStore& db, const QString& id, const QString& name = QStringLiteral("Action"))
{
    QVERIFY(waitForTask(db.query(
        "INSERT INTO action (id, node, status, favorite, name, created_date, due_kind, kind, version, updated) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        id,
        QStringLiteral("node-1"),
        0,
        false,
        name,
        utcDateTime(QDate(2026, 3, 25), QTime(9, 0)),
        0,
        0,
        1,
        1)));
}

void insertMinimalAction(
    DbStore& db,
    const QString& id,
    const QString& node_id,
    const QString& name)
{
    QVERIFY(waitForTask(db.query(
        "INSERT INTO action (id, node, status, favorite, name, created_date, due_kind, kind, version, updated) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        id,
        node_id,
        0,
        false,
        name,
        utcDateTime(QDate(2026, 3, 25), QTime(9, 0)),
        0,
        0,
        1,
        1)));
}

nextapp::pb::TimeBlock makeTimeBlock(
    QString id,
    qint64 start_unix,
    qint64 end_unix,
    QStringList action_ids,
    nextapp::pb::TimeBlock::Kind kind = nextapp::pb::TimeBlock::Kind::ACTIONS,
    quint64 updated = 1,
    quint64 updated_id = 1)
{
    nextapp::pb::TimeBlock tb;
    tb.setId_proto(std::move(id));
    tb.setUser(QStringLiteral("tester"));
    tb.setName(QStringLiteral("Calendar block"));
    tb.setKind(kind);
    tb.setCategory(QStringLiteral("calendar"));

    nextapp::pb::TimeSpan span;
    span.setStart(start_unix);
    span.setEnd(end_unix);
    tb.setTimeSpan(std::move(span));

    nextapp::pb::StringList actions;
    actions.setList(std::move(action_ids));
    tb.setActions(std::move(actions));

    tb.setVersion(1);
    tb.setUpdated(updated);
    tb.setUpdatedId(updated_id);
    return tb;
}

nextapp::pb::Update makeCalendarUpdate(
    nextapp::pb::Update::Operation op,
    const nextapp::pb::TimeBlock& tb)
{
    nextapp::pb::CalendarEvent event;
    event.setId_proto(tb.id_proto());
    event.setTimeBlock(tb);
    event.setTimeSpan(tb.timeSpan());

    nextapp::pb::CalendarEvents events;
    events.setEvents({event});

    nextapp::pb::Update update;
    update.setOp(op);
    update.setCalendarEvents(std::move(events));
    return update;
}

nextapp::pb::TimeBlock readStoredTimeBlock(DbStore& db, const QString& id)
{
    QList<QVariant> params;
    params << id;
    const auto rows = waitForTask(db.legacyQuery("SELECT data FROM time_block WHERE id = ?", &params));
    if (!rows.has_value() || rows->isEmpty() || rows->front().isEmpty()) {
        qFatal("Failed to read stored time block for id=%s", qPrintable(id));
    }

    QProtobufSerializer serializer;
    nextapp::pb::TimeBlock tb;
    if (!tb.deserialize(&serializer, rows->front().front().toByteArray())) {
        qFatal("Failed to deserialize stored time block for id=%s", qPrintable(id));
    }
    return tb;
}

class TestSyncedNodeCache final
    : public QObject
    , public ServerSynchedCahce<nextapp::pb::Node, TestSyncedNodeCache>
{
    Q_OBJECT

public:
    explicit TestSyncedNodeCache(RuntimeServices& runtime)
        : ServerSynchedCahce<nextapp::pb::Node, TestSyncedNodeCache>(runtime)
    {
        setState(State::LOCAL);
    }

    QCoro::Task<void> pocessUpdate(const std::shared_ptr<nextapp::pb::Update> update) override
    {
        processed_ids_.append(update->node().uuid());
        co_return;
    }

    QCoro::Task<bool> save(const QProtobufMessage&) override
    {
        co_return true;
    }

    QCoro::Task<bool> loadFromCache() override
    {
        ++load_from_cache_calls_;
        co_return true;
    }

    bool hasItems(const nextapp::pb::Status&) const noexcept override
    {
        return false;
    }

    QList<nextapp::pb::Node> getItems(const nextapp::pb::Status&) override
    {
        return {};
    }

    bool isRelevant(const nextapp::pb::Update& update) const noexcept override
    {
        return update.hasNode();
    }

    std::string_view itemName() const noexcept override
    {
        return "node";
    }

    std::shared_ptr<GrpcIncomingStream> openServerStream(nextapp::pb::GetNewReq) override
    {
        return {};
    }

    void clear() override
    {
    }

signals:
    void stateChanged();

public:
    QStringList processed_ids_;
    int load_from_cache_calls_{0};
};

class TestTransactionalSyncedCache final
    : public QObject
    , public ServerSynchedCahce<nextapp::pb::Node, TestTransactionalSyncedCache>
{
    Q_OBJECT

public:
    explicit TestTransactionalSyncedCache(RuntimeServices& runtime)
        : ServerSynchedCahce<nextapp::pb::Node, TestTransactionalSyncedCache>(runtime)
    {
        setState(State::LOCAL);
    }

    QCoro::Task<void> pocessUpdate(const std::shared_ptr<nextapp::pb::Update> update) override
    {
        processed_update_names_.append(update->node().name());
        co_return;
    }

    QCoro::Task<bool> save(const QProtobufMessage&) override
    {
        co_return true;
    }

    QCoro::Task<bool> loadFromCache() override
    {
        ++load_from_cache_calls_;
        auto rows = co_await syncDb().legacyQuery("SELECT name FROM sync_test ORDER BY id");
        if (!rows) {
            co_return false;
        }

        loaded_names_.clear();
        for (const auto& row : rows.value()) {
            loaded_names_.append(row.at(0).toString());
        }

        co_return load_from_cache_result_;
    }

    bool hasItems(const nextapp::pb::Status&) const noexcept override
    {
        return false;
    }

    QList<nextapp::pb::Node> getItems(const nextapp::pb::Status&) override
    {
        return {};
    }

    bool isRelevant(const nextapp::pb::Update& update) const noexcept override
    {
        return update.hasNode();
    }

    std::string_view itemName() const noexcept override
    {
        return "sync_test";
    }

    std::shared_ptr<GrpcIncomingStream> openServerStream(nextapp::pb::GetNewReq) override
    {
        return {};
    }

    void clear() override
    {
        ++clear_calls_;
        loaded_names_.clear();
    }

    QCoro::Task<bool> finalizeSyncPersistence() override
    {
        ++finalize_calls_;
        co_return finalize_result_;
    }

    QCoro::Task<bool> synchFromServer() override
    {
        ++synch_from_server_calls_;
        observed_last_update_ = co_await getLastUpdate();
        observed_last_updated_id_ = co_await getLastUpdatedId();
        observed_transaction_during_sync_ = syncTransactionToken().has_value();

        const auto insert = [&](int id, qlonglong updated, qulonglong updated_id, const QString& name) -> QCoro::Task<bool> {
            if (auto token = syncTransactionToken()) {
                co_return static_cast<bool>(co_await syncDb().queryInTransaction(
                    *token,
                    "INSERT INTO sync_test (id, updated, updated_id, name) VALUES (?,?,?,?)",
                    id,
                    updated,
                    QVariant::fromValue(updated_id),
                    name));
            }

            co_return static_cast<bool>(co_await syncDb().query(
                "INSERT INTO sync_test (id, updated, updated_id, name) VALUES (?,?,?,?)",
                id,
                updated,
                QVariant::fromValue(updated_id),
                name));
        };

        for (const auto& row : rows_to_insert_) {
            if (!co_await insert(row.id, row.updated, row.updated_id, row.name)) {
                co_return false;
            }
        }

        co_return synch_from_server_result_;
    }

signals:
    void stateChanged();

public:
    struct Row {
        int id;
        qlonglong updated;
        qulonglong updated_id;
        QString name;
    };

    QList<Row> rows_to_insert_;
    QStringList loaded_names_;
    QStringList processed_update_names_;
    int clear_calls_{0};
    int load_from_cache_calls_{0};
    int finalize_calls_{0};
    int synch_from_server_calls_{0};
    bool load_from_cache_result_{true};
    bool finalize_result_{true};
    bool synch_from_server_result_{true};
    bool observed_transaction_during_sync_{false};
    qlonglong observed_last_update_{-1};
    qulonglong observed_last_updated_id_{std::numeric_limits<qulonglong>::max()};
};

} // namespace

class tst_NextAppUiRuntime final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void mainTreeModelUsesInjectedRuntimeForCommands();
    void mainTreeModelRejectsInvalidRequestsWithoutCallingComms();
    void mainTreeModelRepairsPersistedParentsAndLoadsSortedTree();
    void materialDesignStylingUsesInjectedSettingsAndRuntime();
    void otpModelUsesInjectedServerCommForSuccessAndFailure();
    void otpModelHandlesMissingOtpRepliesAndRecoversOnLaterSuccess();
    void serverCommPersistsLocalActionCategoryVersion();
    void serverCommNeedFullResyncHonorsSettingsAndDataEpoc();
    void serverCommPersistsAndReloadsSyncCursorFile();
    void serverCommResyncSetsFlagAndStops();
    void serverCommDuplicateResyncIsCoalesced();
    void serverCommOutOfOrderUpdateRequestsFullResync();
    void serverSynchedCacheQueuesUpdatesUntilLocalLoadCompletes();
    void serverSynchedCacheCommitsTransactionAndLoadsPersistedState();
    void serverSynchedCacheRollsBackTransactionOnSyncFailure();
    void serverSynchedCacheCanSkipReloadAfterSuccessfulSync();
    void calendarCacheSaveBatchSanitizesActionRefsAndPersistsRelations();
    void calendarCacheRepairStoredTimeBlocksRemovesDanglingActionRefs();
    void calendarCacheInvalidCalendarDeleteUpdateRequestsResync();
    void workCacheLoadsPersistedSessionsAndFiltersByAction();
    void workCacheRejectsDanglingActionReferences();
    void workCacheProcessesActionMoveAndDeleteUpdates();
    void actionCategoriesModelSyncsVersionsAppliesPendingUpdatesAndRoutesCommands();
    void devicesModelUsesInjectedServerCommForFetchEnableAndDelete();
    void devicesModelHandlesOfflineAndRpcFailures();
    void notificationsModelRoundTripsDbStateAndSettings();
    void notificationsModelProcessesUpdatesDeletesAndLastReadSignals();
    void importExportModelDispatchesExportAndImportThroughInjectedComms();
    void importExportModelRejectsInvalidInputsWithoutCallingImport();
    void actionInfoCacheComputesStableScores();
    void actionInfoCacheRepairsOriginsPersistsTagsAndReloads();
    void actionInfoCacheUpdateReloadsMissingOriginsAndInvalidDeletesRequestResync();
    void actionsModelAddsTodayActionFromLiveUpdate();
    void mainTreeModelReloadsFromCacheWhenUpdateTargetsMissingNode();
    void mainTreeModelDeleteUpdateClearsSelectionAndEmitsNodeDeleted();
    void mainTreeModelFailsLocalLoadWithDanglingParent();
    void useCaseTemplatesExposeSortedNamesAndCreateSelectedTree();

private:
    QTemporaryDir temp_dir_;
};

void tst_NextAppUiRuntime::initTestCase()
{
    QVERIFY2(temp_dir_.isValid(), "Failed to create temporary test directory");

    QCoreApplication::setOrganizationName(QStringLiteral("nextapp-tests"));
    QCoreApplication::setApplicationName(
        QStringLiteral("tst_nextappui_runtime_%1").arg(QCoreApplication::applicationPid()));

    const auto data_dir = temp_dir_.filePath("data");
    const auto config_dir = temp_dir_.filePath("config");
    QVERIFY(QDir{}.mkpath(data_dir));
    QVERIFY(QDir{}.mkpath(config_dir));

    qputenv("HOME", temp_dir_.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    const auto data_path = data_dir.toUtf8();
    const auto config_path = config_dir.toUtf8();
    qputenv("XDG_DATA_HOME", data_path);
    qputenv("XDG_CONFIG_HOME", config_path);

    AppInstanceMgr::instance()->close();
    QVERIFY(AppInstanceMgr::instance()->init());
}

void tst_NextAppUiRuntime::cleanupTestCase()
{
    AppInstanceMgr::instance()->close();
}

void tst_NextAppUiRuntime::mainTreeModelUsesInjectedRuntimeForCommands()
{
    TestRuntimeServices runtime;
    MainTreeModel model(runtime);

    QVariantMap args{
        {"uuid", "4b7bfbda-c4df-4df1-9da9-89d6e8d5d9f2"},
        {"parent", "8feadf78-e28f-4ffb-b3a3-1b9de9ab9607"},
        {"name", "Inbox"},
        {"kind", "folder"},
        {"active", true},
    };

    model.addNode(args);
    QCOMPARE(runtime.server_comm_.added_nodes_.size(), 1);
    QCOMPARE(runtime.server_comm_.added_nodes_.front().uuid(), args["uuid"].toString());
    QCOMPARE(runtime.server_comm_.added_nodes_.front().name(), args["name"].toString());

    args["name"] = "Inbox Updated";
    model.updateNode(args);
    QCOMPARE(runtime.server_comm_.updated_nodes_.size(), 1);
    QCOMPARE(runtime.server_comm_.updated_nodes_.front().name(), QStringLiteral("Inbox Updated"));

    model.deleteNode(args["uuid"].toString());
    QCOMPARE(runtime.server_comm_.deleted_nodes_.size(), 1);
    QCOMPARE(runtime.server_comm_.deleted_nodes_.front().toString(QUuid::WithoutBraces), args["uuid"].toString());

    model.moveNode(args["uuid"].toString(), args["parent"].toString());
    QCOMPARE(runtime.server_comm_.moved_nodes_.size(), 1);
    QCOMPARE(runtime.server_comm_.moved_nodes_.front().first.toString(QUuid::WithoutBraces), args["uuid"].toString());
    QCOMPARE(runtime.server_comm_.moved_nodes_.front().second.toString(QUuid::WithoutBraces), args["parent"].toString());
}

void tst_NextAppUiRuntime::mainTreeModelRejectsInvalidRequestsWithoutCallingComms()
{
    TestRuntimeServices runtime;
    MainTreeModel model(runtime);

    model.addNode({
        {"uuid", "2ea8544f-7cf3-4d43-91d0-bd84602aa6a2"},
        {"name", ""},
        {"kind", "folder"},
    });
    model.updateNode({
        {"uuid", ""},
        {"name", "Still Invalid"},
        {"kind", "folder"},
    });
    model.deleteNode("not-a-uuid");
    model.moveNode("not-a-uuid", "8feadf78-e28f-4ffb-b3a3-1b9de9ab9607");

    QCOMPARE(runtime.server_comm_.added_nodes_.size(), 0);
    QCOMPARE(runtime.server_comm_.updated_nodes_.size(), 0);
    QCOMPARE(runtime.server_comm_.deleted_nodes_.size(), 0);
    QCOMPARE(runtime.server_comm_.moved_nodes_.size(), 0);
}

void tst_NextAppUiRuntime::mainTreeModelRepairsPersistedParentsAndLoadsSortedTree()
{
    const auto app_data_dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QVERIFY2(!app_data_dir.isEmpty(), "AppLocalDataLocation resolved to an empty path");
    const auto db_dir = app_data_dir + AppInstanceMgr::instance()->name() + "/";
    QVERIFY2(QDir{}.mkpath(db_dir), qPrintable(QStringLiteral("Failed to create test db dir: %1").arg(db_dir)));

    DbStore db(QStringLiteral("main-tree-test.sqlite"));
    QVERIFY(waitForTask(db.init()));

    TestRuntimeServices runtime;
    runtime.setDbForTest(db);
    MainTreeModel model(runtime);

    const auto root_id = QStringLiteral("11111111-1111-1111-1111-111111111111");
    const auto alpha_id = QStringLiteral("22222222-2222-2222-2222-222222222222");
    const auto zulu_id = QStringLiteral("33333333-3333-3333-3333-333333333333");

    const QList<nextapp::pb::Node> batch{
        makeNode(alpha_id, QStringLiteral("Alpha"), nextapp::pb::Node::Kind::TASK, root_id, 200, 20),
        makeNode(zulu_id, QStringLiteral("Zulu"), nextapp::pb::Node::Kind::TASK, root_id, 300, 30),
        makeNode(root_id, QStringLiteral("Root"), nextapp::pb::Node::Kind::FOLDER, {}, 100, 10),
    };

    model.setState(MainTreeModel::State::SYNCHING);
    QVERIFY(waitForTask(model.saveBatch(batch)));
    QVERIFY(waitForTask(model.finalizeSyncPersistence()));

    const auto stored = waitForTask(db.legacyQuery(
        QStringLiteral("SELECT uuid, parent FROM node ORDER BY uuid")));
    QVERIFY(stored.has_value());
    QCOMPARE(stored->size(), 3);
    QCOMPARE(stored->at(0).at(0).toString(), root_id);
    QVERIFY(stored->at(0).at(1).toString().isEmpty());
    QCOMPARE(stored->at(1).at(0).toString(), alpha_id);
    QCOMPARE(stored->at(1).at(1).toString(), root_id);
    QCOMPARE(stored->at(2).at(0).toString(), zulu_id);
    QCOMPARE(stored->at(2).at(1).toString(), root_id);

    QVERIFY(waitForTask(model.loadLocally()));
    QCOMPARE(model.state(), MainTreeModel::State::VALID);

    const auto root_index = model.indexFromUuid(root_id);
    QVERIFY(root_index.isValid());
    QCOMPARE(model.nodeNameFromUuid(root_id), QStringLiteral("Root"));
    QCOMPARE(model.rowCount(root_index), 2);
    QCOMPARE(model.data(model.index(0, 0, root_index), MainTreeModel::TreeNode::NameRole).toString(), QStringLiteral("Alpha"));
    QCOMPARE(model.data(model.index(1, 0, root_index), MainTreeModel::TreeNode::NameRole).toString(), QStringLiteral("Zulu"));
    QCOMPARE(model.nodeNameFromUuid(alpha_id), QStringLiteral("Alpha"));
    QCOMPARE(model.nodeNameFromUuid(alpha_id, true), QStringLiteral("/Root/Alpha"));

    db.close();
}

void tst_NextAppUiRuntime::materialDesignStylingUsesInjectedSettingsAndRuntime()
{
    TestRuntimeServices runtime;
    runtime.settings_.setValue("UI/theme", "dark");
    runtime.mobile_ui_ = true;

    MaterialDesignStyling styling(runtime);

    QCOMPARE(styling.primary(), QStringLiteral("#D0BCFE"));
    QCOMPARE(styling.scrollBarWidth(), 16);
}

void tst_NextAppUiRuntime::otpModelUsesInjectedServerCommForSuccessAndFailure()
{
    TestRuntimeServices runtime;
    runtime.server_comm_.request_otp_response_ = makeOtpStatus(
        QStringLiteral("654321"),
        QStringLiteral("user@example.com"));

    OtpModel model(runtime);
    model.requestOtpForNewDevice();

    QCOMPARE(runtime.server_comm_.request_otp_calls_, 1);
    QCOMPARE(model.property("otp").toString(), QStringLiteral("654321"));
    QCOMPARE(model.property("email").toString(), QStringLiteral("user@example.com"));
    QCOMPARE(model.property("error").toString(), QString{});

    runtime.server_comm_.request_otp_error_ = ServerCommAccess::CbError{
        nextapp::pb::ErrorGadget::Error::GENERIC_ERROR,
        QStringLiteral("network down"),
    };

    model.requestOtpForNewDevice();

    QCOMPARE(runtime.server_comm_.request_otp_calls_, 2);
    QCOMPARE(model.property("otp").toString(), QStringLiteral("654321"));
    QCOMPARE(model.property("email").toString(), QStringLiteral("user@example.com"));
    QCOMPARE(model.property("error").toString(), QStringLiteral("Failed to get OTP: network down"));
}

void tst_NextAppUiRuntime::otpModelHandlesMissingOtpRepliesAndRecoversOnLaterSuccess()
{
    TestRuntimeServices runtime;
    OtpModel model(runtime);

    QSignalSpy otp_spy(&model, &OtpModel::otpChanged);
    QSignalSpy email_spy(&model, &OtpModel::emailChanged);
    QSignalSpy error_spy(&model, &OtpModel::errorChanged);

    runtime.server_comm_.request_otp_response_ = makeOkStatus();
    model.requestOtpForNewDevice();

    QCOMPARE(runtime.server_comm_.request_otp_calls_, 1);
    QCOMPARE(model.property("otp").toString(), QString{});
    QCOMPARE(model.property("email").toString(), QString{});
    QCOMPARE(model.property("error").toString(), QStringLiteral("Missing OTP in reply from server"));
    QCOMPARE(otp_spy.count(), 0);
    QCOMPARE(email_spy.count(), 0);
    QCOMPARE(error_spy.count(), 1);

    runtime.server_comm_.request_otp_error_ = ServerCommAccess::CbError{
        nextapp::pb::ErrorGadget::Error::GENERIC_ERROR,
        QStringLiteral("temporary failure"),
    };
    model.requestOtpForNewDevice();

    QCOMPARE(runtime.server_comm_.request_otp_calls_, 2);
    QCOMPARE(model.property("otp").toString(), QString{});
    QCOMPARE(model.property("email").toString(), QString{});
    QCOMPARE(model.property("error").toString(), QStringLiteral("Failed to get OTP: temporary failure"));
    QCOMPARE(error_spy.count(), 2);

    runtime.server_comm_.request_otp_error_.reset();
    runtime.server_comm_.request_otp_response_ = makeOtpStatus(
        QStringLiteral("777888"),
        QStringLiteral("recover@example.com"));
    model.requestOtpForNewDevice();

    QCOMPARE(runtime.server_comm_.request_otp_calls_, 3);
    QCOMPARE(model.property("otp").toString(), QStringLiteral("777888"));
    QCOMPARE(model.property("email").toString(), QStringLiteral("recover@example.com"));
    QCOMPARE(model.property("error").toString(), QString{});
    QCOMPARE(otp_spy.count(), 1);
    QCOMPARE(email_spy.count(), 1);
    QCOMPARE(error_spy.count(), 3);
}

void tst_NextAppUiRuntime::serverCommPersistsLocalActionCategoryVersion()
{
    auto db = makeInitializedDb(QStringLiteral("servercomm-local-versions.sqlite"));

    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);

    const auto persisted_key = AppInstanceMgr::instance()->name() + "/sync/action_category_version";
    runtime.settings_.setValue(persisted_key, qulonglong{17});

    {
        ServerComm comm(runtime);
        QCOMPARE(comm.getLocalDataVersions().actionCategoryVersion(), quint64{17});

        comm.setLocalActionCategoryVersion(42);
        QCOMPARE(comm.getLocalDataVersions().actionCategoryVersion(), quint64{42});
        QCOMPARE(runtime.settings_.value(persisted_key).toULongLong(), quint64{42});

        comm.setLocalActionCategoryVersion(42);
        QCOMPARE(runtime.settings_.value(persisted_key).toULongLong(), quint64{42});
    }

    db->close();
}

void tst_NextAppUiRuntime::serverCommNeedFullResyncHonorsSettingsAndDataEpoc()
{
    {
        auto db = makeInitializedDb(QStringLiteral("servercomm-full-resync-settings.sqlite"));
        TestRuntimeServices runtime;
        runtime.setDbForTest(*db);
        runtime.settings_.setValue("sync/resync", true);

        ServerComm comm(runtime);
        QVERIFY(waitForTask(comm.needFullResync()));
        db->close();
    }

    {
        auto db = makeInitializedDb(QStringLiteral("servercomm-full-resync-dbinit.sqlite"));
        TestRuntimeServices runtime;
        runtime.setDbForTest(*db);

        ServerComm comm(runtime);
        QVERIFY(waitForTask(comm.needFullResync()));

        db->clearDbInitializedFlag();
        QVERIFY(waitForTask(db->query("UPDATE nextapp SET data_epoc = ?", nextapp::app_data_epoc)));
        QVERIFY(!waitForTask(comm.needFullResync()));

        QVERIFY(waitForTask(db->query("UPDATE nextapp SET data_epoc = ?", quint32{0})));
        QCOMPARE(waitForTask(comm.needFullResync()), nextapp::app_data_epoc != 0u);
        db->close();
    }
}

void tst_NextAppUiRuntime::serverCommPersistsAndReloadsSyncCursorFile()
{
    auto db = makeInitializedDb(QStringLiteral("servercomm-sync-cursor.sqlite"));

    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);

    {
        ServerComm comm(runtime);
        comm.last_seen_update_id_ = 77;
        comm.last_seen_server_instance_ = 9988;
        comm.last_seen_user_publish_epoch_ = 123456;
        comm.close();
    }

    const auto cursor_file = QString::fromStdString((db->dataDir() / "svrreqid.dat").string());
    QVERIFY2(QFileInfo::exists(cursor_file), qPrintable(cursor_file));

    {
        ServerComm comm(runtime);
        QCOMPARE(comm.last_seen_update_id_, 0u);
        QCOMPARE(comm.last_seen_server_instance_, quint64{0});
        QCOMPARE(comm.last_seen_user_publish_epoch_, quint64{0});

        comm.start();

        QCOMPARE(comm.last_seen_update_id_, 77u);
        QCOMPARE(comm.last_seen_server_instance_, quint64{9988});
        QCOMPARE(comm.last_seen_user_publish_epoch_, quint64{123456});
    }

    db->close();
}

void tst_NextAppUiRuntime::serverCommResyncSetsFlagAndStops()
{
    auto db = makeInitializedDb(QStringLiteral("servercomm-resync.sqlite"));

    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);
    runtime.settings_.setValue("sync/resync", false);

    ServerComm comm(runtime);
    comm.signup_status_ = ServerComm::SIGNUP_OK;
    comm.setStatus(ServerComm::ONLINE);

    QSignalSpy spy(&comm, &ServerComm::resynching);
    comm.resync();

    QCOMPARE(spy.count(), 1);
    QCOMPARE(runtime.settings_.value("sync/resync").toString(), QStringLiteral("true"));
    QCOMPARE(comm.status(), ServerComm::OFFLINE);
    QVERIFY(comm.messages_.contains("Initiating full resynch with the server"));

    db->close();
}

void tst_NextAppUiRuntime::serverCommDuplicateResyncIsCoalesced()
{
    auto db = makeInitializedDb(QStringLiteral("servercomm-duplicate-resync.sqlite"));

    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);
    runtime.settings_.setValue("sync/resync", false);

    ServerComm comm(runtime);
    comm.signup_status_ = ServerComm::SIGNUP_OK;
    comm.setStatus(ServerComm::ONLINE);

    QSignalSpy spy(&comm, &ServerComm::resynching);
    comm.resync();
    comm.resync();

    QCOMPARE(spy.count(), 1);
    QCOMPARE(runtime.settings_.value("sync/resync").toString(), QStringLiteral("true"));
    QCOMPARE(comm.status(), ServerComm::OFFLINE);
    QCOMPARE(comm.resync_scheduled_, true);

    db->close();
}

void tst_NextAppUiRuntime::serverCommOutOfOrderUpdateRequestsFullResync()
{
    auto db = makeInitializedDb(QStringLiteral("servercomm-out-of-order-resync.sqlite"));

    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);
    runtime.settings_.setValue("sync/resync", false);

    ServerComm comm(runtime);
    comm.signup_status_ = ServerComm::SIGNUP_OK;
    comm.setStatus(ServerComm::ONLINE);

    comm.requestResyncAfterStreamGap(2);

    QCOMPARE(runtime.settings_.value("sync/resync").toBool(), true);
    QCOMPARE(runtime.settings_.sync_calls_, 1);
    QCOMPARE(comm.status(), ServerComm::ERROR);

    QCoreApplication::processEvents(QEventLoop::AllEvents);

    QCOMPARE(comm.status(), ServerComm::OFFLINE);
    QVERIFY(comm.messages_.contains("Initiating full resynch with the server"));

    db->close();
}

void tst_NextAppUiRuntime::serverSynchedCacheQueuesUpdatesUntilLocalLoadCompletes()
{
    TestRuntimeServices runtime;
    TestSyncedNodeCache cache(runtime);

    auto first = std::make_shared<nextapp::pb::Update>(makeNodeUpdate("2907c66c-6d7d-4c6b-a709-bf87b00f4df0"));
    auto second = std::make_shared<nextapp::pb::Update>(makeNodeUpdate("824f37a3-c7e9-4304-873d-55af6f3cd3f7"));

    cache.onUpdate(first);
    cache.onUpdate(second);

    QCOMPARE(cache.processed_ids_.size(), 0);
    QCOMPARE(cache.state(), TestSyncedNodeCache::State::LOCAL);

    QVERIFY(waitForTask(cache.loadLocally(false)));
    QCOMPARE(cache.load_from_cache_calls_, 1);
    const QStringList expected_ids{
        QStringLiteral("2907c66c-6d7d-4c6b-a709-bf87b00f4df0"),
        QStringLiteral("824f37a3-c7e9-4304-873d-55af6f3cd3f7"),
    };
    QCOMPARE(cache.processed_ids_, expected_ids);
    QCOMPARE(cache.state(), TestSyncedNodeCache::State::VALID);

    auto live = std::make_shared<nextapp::pb::Update>(makeNodeUpdate("fd43cb47-a212-4940-ae85-a74739c53a55"));
    cache.onUpdate(live);
    QCOMPARE(cache.processed_ids_.back(), QStringLiteral("fd43cb47-a212-4940-ae85-a74739c53a55"));
}

void tst_NextAppUiRuntime::actionsModelAddsTodayActionFromLiveUpdate()
{
    auto action = makeAction(
        QStringLiteral("d5fba219-4217-4864-a313-60ecf5288292"),
        QStringLiteral("f5666bfe-d280-4c57-8dc4-83dcbcc7a8a1"),
        QStringLiteral("Later today"),
        1,
        100,
        1);

    nextapp::pb::Due due;
    due.setKind(nextapp::pb::ActionDueKindGadget::ActionDueKind::DATETIME);
    due.setDue(QDate::currentDate().startOfDay().addSecs(18 * 60 * 60).toSecsSinceEpoch());
    due.setTimezone(QStringLiteral("UTC"));
    action.setDue(due);

    nextapp::pb::ActionInfo info;
    info.setId_proto(action.id_proto());
    info.setNode(action.node());
    info.setStatus(action.status());
    info.setFavorite(action.favorite());
    info.setDue(action.due());

    QVERIFY(ActionsModel::matchesActionForMode(ActionsModel::FW_TODAY_AND_OVERDUE, info));
    QVERIFY(!ActionsModel::matchesActionForMode(ActionsModel::FW_TOMORROW, info));
}

void tst_NextAppUiRuntime::serverSynchedCacheCommitsTransactionAndLoadsPersistedState()
{
    auto db = makeInitializedDb(QStringLiteral("server-synched-cache-commit.sqlite"));
    ensureSyncTestTable(*db);
    QVERIFY(waitForTask(db->query(
        "INSERT INTO sync_test (id, updated, updated_id, name) VALUES (?,?,?,?)",
        1,
        qlonglong{100},
        QVariant::fromValue(qulonglong{7}),
        QStringLiteral("existing"))));

    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);

    TestTransactionalSyncedCache cache(runtime);
    cache.rows_to_insert_.append({2, 200, 9, QStringLiteral("synced")});

    auto pending = std::make_shared<nextapp::pb::Update>(makeNodeUpdate("77777777-7777-7777-7777-777777777777"));
    auto pending_node = pending->node();
    pending_node.setName(QStringLiteral("pending-applied"));
    pending->setNode(std::move(pending_node));
    cache.onUpdate(pending);

    QVERIFY(waitForTask(cache.synch(false)));
    QCOMPARE(cache.state(), TestTransactionalSyncedCache::State::VALID);
    QCOMPARE(cache.synch_from_server_calls_, 1);
    QCOMPARE(cache.finalize_calls_, 1);
    QCOMPARE(cache.load_from_cache_calls_, 1);
    QCOMPARE(cache.clear_calls_, 1);
    QVERIFY(cache.observed_transaction_during_sync_);
    QCOMPARE(cache.observed_last_update_, qlonglong{100});
    QCOMPARE(cache.observed_last_updated_id_, qulonglong{7});
    QCOMPARE(cache.loaded_names_, QStringList({QStringLiteral("existing"), QStringLiteral("synced")}));
    QCOMPARE(cache.processed_update_names_, QStringList({QStringLiteral("pending-applied")}));

    const auto rows = waitForTask(db->legacyQuery("SELECT name FROM sync_test ORDER BY id"));
    QVERIFY(rows.has_value());
    QCOMPARE(rows->size(), 2);
    QCOMPARE(rows->at(0).at(0).toString(), QStringLiteral("existing"));
    QCOMPARE(rows->at(1).at(0).toString(), QStringLiteral("synced"));

    db->close();
}

void tst_NextAppUiRuntime::serverSynchedCacheRollsBackTransactionOnSyncFailure()
{
    auto db = makeInitializedDb(QStringLiteral("server-synched-cache-rollback.sqlite"));
    ensureSyncTestTable(*db);

    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);

    TestTransactionalSyncedCache cache(runtime);
    cache.rows_to_insert_.append({1, 50, 3, QStringLiteral("transient")});
    cache.synch_from_server_result_ = false;

    QVERIFY(!waitForTask(cache.synch(false)));
    QCOMPARE(cache.state(), TestTransactionalSyncedCache::State::ERROR);
    QCOMPARE(cache.finalize_calls_, 0);
    QCOMPARE(cache.load_from_cache_calls_, 0);
    QVERIFY(cache.observed_transaction_during_sync_);

    const auto rows = waitForTask(db->legacyQuery("SELECT name FROM sync_test"));
    QVERIFY(rows.has_value());
    QCOMPARE(rows->size(), 0);

    db->close();
}

void tst_NextAppUiRuntime::serverSynchedCacheCanSkipReloadAfterSuccessfulSync()
{
    auto db = makeInitializedDb(QStringLiteral("server-synched-cache-no-reload.sqlite"));
    ensureSyncTestTable(*db);

    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);

    TestTransactionalSyncedCache cache(runtime);
    cache.setLoadAfterSync(false);
    cache.rows_to_insert_.append({5, 500, 11, QStringLiteral("persist-only")});

    QVERIFY(waitForTask(cache.synch(false)));
    QCOMPARE(cache.state(), TestTransactionalSyncedCache::State::LOCAL);
    QCOMPARE(cache.load_from_cache_calls_, 0);
    QCOMPARE(cache.clear_calls_, 0);

    const auto rows = waitForTask(db->legacyQuery("SELECT name FROM sync_test ORDER BY id"));
    QVERIFY(rows.has_value());
    QCOMPARE(rows->size(), 1);
    QCOMPARE(rows->at(0).at(0).toString(), QStringLiteral("persist-only"));

    db->close();
}

void tst_NextAppUiRuntime::calendarCacheSaveBatchSanitizesActionRefsAndPersistsRelations()
{
    auto db = makeInitializedDb(QStringLiteral("calendar-cache-save-batch.sqlite"));
    insertMinimalAction(*db, QStringLiteral("action-1"), QStringLiteral("Action One"));
    insertMinimalAction(*db, QStringLiteral("action-2"), QStringLiteral("Action Two"));

    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);

    CalendarCache cache(runtime);
    const auto tb = makeTimeBlock(
        QStringLiteral("tb-1"),
        utcDateTime(QDate(2026, 3, 26), QTime(10, 0)).toSecsSinceEpoch(),
        utcDateTime(QDate(2026, 3, 26), QTime(11, 0)).toSecsSinceEpoch(),
        {QStringLiteral("action-1"), QStringLiteral("missing-action"), QStringLiteral("action-1"), QStringLiteral("action-2")},
        nextapp::pb::TimeBlock::Kind::ACTIONS,
        100,
        10);

    QVERIFY(waitForTask(cache.saveBatch({tb})));

    const auto stored = readStoredTimeBlock(*db, QStringLiteral("tb-1"));
    QCOMPARE(stored.actions().list(), QStringList({QStringLiteral("action-1"), QStringLiteral("action-2")}));

    const auto refs = waitForTask(db->legacyQuery(
        "SELECT action FROM time_block_actions WHERE time_block = 'tb-1' ORDER BY action"));
    QVERIFY(refs.has_value());
    QCOMPARE(refs->size(), 2);
    QCOMPARE(refs->at(0).at(0).toString(), QStringLiteral("action-1"));
    QCOMPARE(refs->at(1).at(0).toString(), QStringLiteral("action-2"));

    db->close();
}

void tst_NextAppUiRuntime::calendarCacheRepairStoredTimeBlocksRemovesDanglingActionRefs()
{
    auto db = makeInitializedDb(QStringLiteral("calendar-cache-repair.sqlite"));
    insertMinimalAction(*db, QStringLiteral("action-1"), QStringLiteral("Action One"));
    insertMinimalAction(*db, QStringLiteral("action-2"), QStringLiteral("Action Two"));

    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);

    CalendarCache cache(runtime);
    const auto tb = makeTimeBlock(
        QStringLiteral("tb-2"),
        utcDateTime(QDate(2026, 3, 26), QTime(12, 0)).toSecsSinceEpoch(),
        utcDateTime(QDate(2026, 3, 26), QTime(13, 0)).toSecsSinceEpoch(),
        {QStringLiteral("action-1"), QStringLiteral("action-2")},
        nextapp::pb::TimeBlock::Kind::ACTIONS,
        101,
        11);

    QVERIFY(waitForTask(cache.saveBatch({tb})));
    const auto events = waitForTask(cache.getCalendarEvents(QDate(2026, 3, 26), QDate(2026, 3, 27)));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.front()->timeBlock().actions().list().size(), 2);

    QVERIFY(waitForTask(db->query("DELETE FROM action WHERE id = ?", QStringLiteral("action-2"))));

    QSignalSpy updated_spy(&cache, &CalendarCache::eventUpdated);
    QVERIFY(waitForTask(cache.repairStoredTimeBlocks()));

    QCOMPARE(updated_spy.count(), 1);

    const auto stored = readStoredTimeBlock(*db, QStringLiteral("tb-2"));
    QCOMPARE(stored.actions().list(), QStringList({QStringLiteral("action-1")}));

    const auto repaired_events = waitForTask(cache.getCalendarEvents(QDate(2026, 3, 26), QDate(2026, 3, 27)));
    QCOMPARE(repaired_events.size(), 1);
    QCOMPARE(repaired_events.front()->timeBlock().actions().list(), QStringList({QStringLiteral("action-1")}));

    db->close();
}

void tst_NextAppUiRuntime::calendarCacheInvalidCalendarDeleteUpdateRequestsResync()
{
    auto db = makeInitializedDb(QStringLiteral("calendar-cache-invalid-update.sqlite"));

    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);

    CalendarCache cache(runtime);
    const auto invalid_deleted_tb = makeTimeBlock(
        QStringLiteral("tb-3"),
        utcDateTime(QDate(2026, 3, 26), QTime(14, 0)).toSecsSinceEpoch(),
        utcDateTime(QDate(2026, 3, 26), QTime(15, 0)).toSecsSinceEpoch(),
        {},
        nextapp::pb::TimeBlock::Kind::ACTIONS,
        102,
        12);

    const auto update = std::make_shared<nextapp::pb::Update>(
        makeCalendarUpdate(nextapp::pb::Update::Operation::DELETED, invalid_deleted_tb));

    waitForTask(cache.pocessUpdate(update));

    QCOMPARE(runtime.server_comm_.resync_calls_, 1);

    db->close();
}

void tst_NextAppUiRuntime::workCacheLoadsPersistedSessionsAndFiltersByAction()
{
    auto db = makeInitializedDb(QStringLiteral("work-cache-load.sqlite"));
    insertMinimalAction(*db, QStringLiteral("action-1"), QStringLiteral("node-1"), QStringLiteral("Action One"));
    insertMinimalAction(*db, QStringLiteral("action-2"), QStringLiteral("node-2"), QStringLiteral("Action Two"));

    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);

    WorkCache cache(runtime);
    const auto base = static_cast<quint64>(QDateTime::currentSecsSinceEpoch() - 600);
    const QList<nextapp::pb::WorkSession> sessions{
        makeWorkSession(
            QStringLiteral("11111111-1111-1111-1111-111111111111"),
            QStringLiteral("action-1"),
            QStringLiteral("Active work"),
            {
                makeWorkEvent(nextapp::pb::WorkEvent_QtProtobufNested::Kind::START, base),
                makeWorkEvent(nextapp::pb::WorkEvent_QtProtobufNested::Kind::TOUCH, base + 300),
            },
            nextapp::pb::WorkSession::State::ACTIVE,
            100,
            10),
        makeWorkSession(
            QStringLiteral("22222222-2222-2222-2222-222222222222"),
            QStringLiteral("action-2"),
            QStringLiteral("Paused work"),
            {
                makeWorkEvent(nextapp::pb::WorkEvent_QtProtobufNested::Kind::START, base - 60),
                makeWorkEvent(nextapp::pb::WorkEvent_QtProtobufNested::Kind::PAUSE, base + 120),
                makeWorkEvent(nextapp::pb::WorkEvent_QtProtobufNested::Kind::TOUCH, base + 180),
            },
            nextapp::pb::WorkSession::State::PAUSED,
            101,
            11),
        makeWorkSession(
            QStringLiteral("33333333-3333-3333-3333-333333333333"),
            QStringLiteral("action-1"),
            QStringLiteral("Done work"),
            {
                makeWorkEvent(nextapp::pb::WorkEvent_QtProtobufNested::Kind::START, base - 3600),
                makeWorkEvent(nextapp::pb::WorkEvent_QtProtobufNested::Kind::STOP, base - 3000, base - 3000),
            },
            nextapp::pb::WorkSession::State::DONE,
            102,
            12),
    };

    QVERIFY(waitForTask(cache.saveBatch(sessions)));
    QVERIFY(waitForTask(cache.loadLocally()));
    QCOMPARE(cache.state(), WorkCache::State::VALID);

    const auto& active = cache.getActive();
    QCOMPARE(active.size(), 2);
    QCOMPARE(active.at(0)->id_proto(), QStringLiteral("11111111-1111-1111-1111-111111111111"));
    QCOMPARE(active.at(0)->state(), nextapp::pb::WorkSession::State::ACTIVE);
    QCOMPARE(active.at(1)->id_proto(), QStringLiteral("22222222-2222-2222-2222-222222222222"));
    QCOMPARE(active.at(1)->state(), nextapp::pb::WorkSession::State::PAUSED);

    nextapp::pb::Page page;
    page.setPageSize(10);

    nextapp::pb::GetWorkSessionsReq req;
    req.setPage(page);
    req.setActionId(QStringLiteral("action-1"));

    const auto filtered = waitForTask(cache.getWorkSessions(req));
    QCOMPARE(filtered.size(), size_t{2});
    QCOMPARE(filtered.at(0)->action(), QStringLiteral("action-1"));
    QCOMPARE(filtered.at(1)->action(), QStringLiteral("action-1"));

    db->close();
}

void tst_NextAppUiRuntime::workCacheRejectsDanglingActionReferences()
{
    auto db = makeInitializedDb(QStringLiteral("work-cache-dangling.sqlite"));
    insertMinimalAction(*db, QStringLiteral("action-1"), QStringLiteral("node-1"), QStringLiteral("Action One"));

    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);

    WorkCache cache(runtime);
    const auto orphan = makeWorkSession(
        QStringLiteral("44444444-4444-4444-4444-444444444444"),
        QStringLiteral("action-1"),
        QStringLiteral("Orphan work"),
        {makeWorkEvent(
            nextapp::pb::WorkEvent_QtProtobufNested::Kind::START,
            static_cast<quint64>(QDateTime::currentSecsSinceEpoch() - 60))},
        nextapp::pb::WorkSession::State::ACTIVE,
        110,
        13);

    QVERIFY(waitForTask(cache.saveBatch({orphan})));
    QVERIFY(waitForTask(db->query("PRAGMA foreign_keys = OFF")));
    QVERIFY(waitForTask(db->query(
        "UPDATE work_session SET action = ? WHERE id = ?",
        QStringLiteral("missing-action"),
        QStringLiteral("44444444-4444-4444-4444-444444444444"))));
    QVERIFY(!waitForTask(cache.loadLocally()));
    QVERIFY(cache.getActive().empty());

    db->close();
}

void tst_NextAppUiRuntime::workCacheProcessesActionMoveAndDeleteUpdates()
{
    auto db = makeInitializedDb(QStringLiteral("work-cache-action-updates.sqlite"));
    insertMinimalAction(*db, QStringLiteral("action-1"), QStringLiteral("node-1"), QStringLiteral("Action One"));

    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);

    WorkCache cache(runtime);
    const auto session = makeWorkSession(
        QStringLiteral("55555555-5555-5555-5555-555555555555"),
        QStringLiteral("action-1"),
        QStringLiteral("Tracked work"),
        {
            makeWorkEvent(nextapp::pb::WorkEvent_QtProtobufNested::Kind::START, quint64{100}),
            makeWorkEvent(nextapp::pb::WorkEvent_QtProtobufNested::Kind::STOP, quint64{160}, quint64{160}),
        },
        nextapp::pb::WorkSession::State::DONE,
        120,
        14);

    waitForTask(cache.pocessUpdate(std::make_shared<nextapp::pb::Update>(
        makeWorkUpdate(nextapp::pb::Update::Operation::ADDED, session))));
    QVERIFY(cache.getActive().empty());

    QSignalSpy moved_spy(&cache, &WorkCache::WorkSessionActionMoved);
    QSignalSpy deleted_spy(&cache, &WorkCache::WorkSessionDeleted);
    QSignalSpy active_spy(&cache, &WorkCache::activeChanged);

    auto moved_action = makeAction(
        QStringLiteral("action-1"),
        QStringLiteral("node-2"),
        QStringLiteral("Moved"),
        2,
        121,
        15);
    waitForTask(cache.pocessUpdate(std::make_shared<nextapp::pb::Update>(
        makeActionUpdate(nextapp::pb::Update::Operation::MOVED, moved_action))));

    QCOMPARE(moved_spy.count(), 1);
    QVERIFY(cache.getActive().empty());

    auto deleted_action = moved_action;
    deleted_action.setStatus(nextapp::pb::ActionStatusGadget::ActionStatus::DELETED);
    waitForTask(cache.pocessUpdate(std::make_shared<nextapp::pb::Update>(
        makeActionUpdate(nextapp::pb::Update::Operation::DELETED, deleted_action))));

    QCOMPARE(deleted_spy.count(), 1);
    QVERIFY(cache.getActive().empty());
    QCOMPARE(active_spy.count(), 0);

    db->close();
}

void tst_NextAppUiRuntime::actionCategoriesModelSyncsVersionsAppliesPendingUpdatesAndRoutesCommands()
{
    auto db = makeInitializedDb(QStringLiteral("action-categories-sync.sqlite"));

    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);
    runtime.server_comm_.data_versions_.setActionCategoryVersion(9);

    const auto alpha = makeActionCategory(
        QStringLiteral("cat-alpha"),
        QStringLiteral("Alpha"),
        QStringLiteral("red"),
        QStringLiteral("briefcase"),
        1);
    const auto beta = makeActionCategory(
        QStringLiteral("cat-beta"),
        QStringLiteral("Beta"),
        QStringLiteral("blue"),
        QStringLiteral("home"),
        2);
    runtime.server_comm_.get_action_categories_response_ = makeActionCategoriesStatus({beta, alpha});

    ActionCategoriesModel model(runtime);

    auto queued_beta = beta;
    queued_beta.setName(QStringLiteral("Beta Updated"));
    runtime.server_comm_.emitUpdateForTest(std::make_shared<nextapp::pb::Update>(
        makeActionCategoryUpdate(nextapp::pb::Update::Operation::UPDATED, queued_beta)));

    auto queued_gamma = makeActionCategory(
        QStringLiteral("cat-gamma"),
        QStringLiteral("Gamma"),
        QStringLiteral("green"),
        QStringLiteral("leaf"),
        3);
    runtime.server_comm_.emitUpdateForTest(std::make_shared<nextapp::pb::Update>(
        makeActionCategoryUpdate(nextapp::pb::Update::Operation::ADDED, queued_gamma)));

    QVERIFY(waitForTask(model.synch(false)));
    QCOMPARE(runtime.server_comm_.local_action_category_version_, quint64{9});
    QCOMPARE(waitForTask(db->queryOne<qulonglong>(
        "SELECT value FROM sync_state WHERE key = 'action_category_version'")).value_or(0),
        qulonglong{9});

    QCOMPARE(model.rowCount({}), 4);
    QCOMPARE(model.get(0).name(), QStringLiteral("Alpha"));
    QCOMPARE(model.get(1).name(), QStringLiteral("Beta Updated"));
    QCOMPARE(model.get(2).name(), QStringLiteral("Gamma"));
    QCOMPARE(model.getName(QStringLiteral("cat-beta")), QStringLiteral("Beta Updated"));
    QCOMPARE(model.getColorFromUuid(QStringLiteral("cat-gamma")), QStringLiteral("green"));
    QCOMPARE(model.getIndexByUuid(QStringLiteral("cat-alpha")), 0);
    QCOMPARE(model.get(model.rowCount({}) - 1).name(), QStringLiteral("-- None --"));

    model.onOnlineChanged(true);
    model.createCategory(queued_gamma);
    model.updateCategory(queued_beta);
    model.deleteCategory(QStringLiteral("cat-alpha"));

    QCOMPARE(runtime.server_comm_.created_categories_.size(), 1);
    QCOMPARE(runtime.server_comm_.created_categories_.front().id_proto(), QStringLiteral("cat-gamma"));
    QCOMPARE(runtime.server_comm_.updated_categories_.size(), 1);
    QCOMPARE(runtime.server_comm_.updated_categories_.front().id_proto(), QStringLiteral("cat-beta"));
    QCOMPARE(runtime.server_comm_.deleted_categories_, QList<QString>({QStringLiteral("cat-alpha")}));

    auto deleted_gamma = queued_gamma;
    runtime.server_comm_.emitUpdateForTest(std::make_shared<nextapp::pb::Update>(
        makeActionCategoryUpdate(nextapp::pb::Update::Operation::DELETED, deleted_gamma)));
    QTRY_COMPARE(model.rowCount({}), 3);
    QCOMPARE(model.getIndexByUuid(QStringLiteral("cat-gamma")), -1);

    db->close();
}

void tst_NextAppUiRuntime::devicesModelUsesInjectedServerCommForFetchEnableAndDelete()
{
    TestRuntimeServices runtime;
    runtime.server_comm_.setConnectedForTest(true);

    const auto created = quint64{1'711'111'111};
    const auto last_seen = quint64{1'711'111'777};
    const auto first_device = makeDevice(
        QStringLiteral("dev-1"),
        QStringLiteral("Laptop"),
        true,
        created,
        last_seen,
        8);
    const auto second_device = makeDevice(
        QStringLiteral("dev-2"),
        QStringLiteral("Tablet"),
        false,
        created + 10,
        0,
        2);

    runtime.server_comm_.fetch_devices_response_ = makeDevicesStatus({first_device, second_device});

    DevicesModel model(runtime);

    QTRY_COMPARE(runtime.server_comm_.fetch_devices_calls_, 1);
    QTRY_COMPARE(model.rowCount({}), 2);
    QTRY_VERIFY(model.property("valid").toBool());
    QCOMPARE(model.data(model.index(0, 0, {}), DevicesModel::IdRole).toString(), QStringLiteral("dev-1"));
    QCOMPARE(model.data(model.index(0, 0, {}), DevicesModel::NameRole).toString(), QStringLiteral("Laptop"));
    QCOMPARE(model.data(model.index(0, 0, {}), DevicesModel::EnabledRole).toBool(), true);
    QCOMPARE(model.data(model.index(1, 0, {}), DevicesModel::LastSeenRole).toString(), QStringLiteral("Never"));

    auto disabled_first_device = first_device;
    disabled_first_device.setEnabled(false);
    runtime.server_comm_.enable_device_response_ = makeOkStatus();
    runtime.server_comm_.enable_device_response_.setDevice(disabled_first_device);

    model.enableDevice(QStringLiteral("dev-1"), false);

    QTRY_COMPARE(runtime.server_comm_.enabled_devices_.size(), 1);
    QTRY_COMPARE(runtime.server_comm_.enabled_devices_.front().first, QStringLiteral("dev-1"));
    QTRY_COMPARE(runtime.server_comm_.enabled_devices_.front().second, false);
    QTRY_COMPARE(model.data(model.index(0, 0, {}), DevicesModel::EnabledRole).toBool(), false);

    runtime.server_comm_.delete_device_response_ = makeOkStatus();
    runtime.server_comm_.fetch_devices_response_ = makeDevicesStatus({second_device});

    model.deleteDevice(QStringLiteral("dev-1"));

    QTRY_COMPARE(runtime.server_comm_.deleted_devices_.size(), 1);
    QTRY_COMPARE(runtime.server_comm_.fetch_devices_calls_, 2);
    QTRY_COMPARE(model.rowCount({}), 1);
    QCOMPARE(model.data(model.index(0, 0, {}), DevicesModel::IdRole).toString(), QStringLiteral("dev-2"));
}

void tst_NextAppUiRuntime::devicesModelHandlesOfflineAndRpcFailures()
{
    TestRuntimeServices runtime;

    DevicesModel model(runtime);
    QTRY_COMPARE(runtime.server_comm_.fetch_devices_calls_, 0);
    QCOMPARE(model.rowCount({}), 0);
    QVERIFY(!model.property("valid").toBool());

    runtime.server_comm_.setConnectedForTest(true);
    auto device = makeDevice(
        QStringLiteral("dev-9"),
        QStringLiteral("Phone"),
        true,
        quint64{1'711'111'111},
        quint64{1'711'111'222},
        1);
    runtime.server_comm_.fetch_devices_response_ = makeDevicesStatus({device});
    model.refresh();

    QTRY_COMPARE(runtime.server_comm_.fetch_devices_calls_, 1);
    QTRY_COMPARE(model.rowCount({}), 1);
    QTRY_VERIFY(model.property("valid").toBool());

    nextapp::pb::Status failure;
    failure.setError(nextapp::pb::ErrorGadget::Error::GENERIC_ERROR);
    runtime.server_comm_.enable_device_response_ = failure;
    runtime.server_comm_.delete_device_response_ = failure;

    model.enableDevice(QStringLiteral("dev-9"), false);
    QTRY_COMPARE(runtime.server_comm_.enabled_devices_.size(), 1);
    QCOMPARE(model.data(model.index(0, 0, {}), DevicesModel::EnabledRole).toBool(), true);

    model.deleteDevice(QStringLiteral("dev-9"));
    QTRY_COMPARE(runtime.server_comm_.deleted_devices_.size(), 1);
    QCOMPARE(runtime.server_comm_.fetch_devices_calls_, 1);
    QCOMPARE(model.rowCount({}), 1);
    QVERIFY(model.property("valid").toBool());
}

void tst_NextAppUiRuntime::notificationsModelRoundTripsDbStateAndSettings()
{
    const auto app_data_dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QVERIFY2(!app_data_dir.isEmpty(), "AppLocalDataLocation resolved to an empty path");
    const auto db_dir = app_data_dir + AppInstanceMgr::instance()->name() + "/";
    QVERIFY2(QDir{}.mkpath(db_dir), qPrintable(QStringLiteral("Failed to create test db dir: %1").arg(db_dir)));

    DbStore db(QStringLiteral("notifications-test.sqlite"));
    QVERIFY(waitForTask(db.init()));

    TestRuntimeServices runtime;
    runtime.setDbForTest(db);
    runtime.settings_.setValue("notificatins/last_read", 5u);
    runtime.settings_.setValue("notificatins/last_seen", qulonglong{11});

    NotificationsModel model(runtime);
    QCOMPARE(model.lastUpdateSeen(), quint64{11});

    const auto now = static_cast<quint64>(QDateTime::currentSecsSinceEpoch());
    const QList<nextapp::pb::Notification> items{
        makeNotification(5, QStringLiteral("Older"), now - 120, 100, 10),
        makeNotification(7, QStringLiteral("Newest"), now - 60, 200, 11),
        makeNotification(6, QStringLiteral("Expired"), now - 90, 150, 10, now - 30),
    };

    QVERIFY(waitForTask(model.saveBatch(items)));
    QVERIFY(waitForTask(model.loadFromCache()));

    QCOMPARE(model.rowCount({}), 2);
    QCOMPARE(model.data(model.index(0, 0, {}), NotificationsModel::idRole).toUInt(), 7u);
    QCOMPARE(model.data(model.index(0, 0, {}), NotificationsModel::subjectRole).toString(), QStringLiteral("Newest"));
    QCOMPARE(model.data(model.index(1, 0, {}), NotificationsModel::idRole).toUInt(), 5u);
    QVERIFY(model.unread());

    QVERIFY(model.SetLastReadValue(7));
    QCOMPARE(runtime.settings_.value("notificatins/last_read").toUInt(), 7u);
    QVERIFY(!model.unread());

    model.setLastUpdateSeen(200);
    QCOMPARE(model.lastUpdateSeen(), quint64{200});
    QCOMPARE(runtime.settings_.value("notificatins/last_seen").toULongLong(), quint64{200});

    db.close();
}

void tst_NextAppUiRuntime::notificationsModelProcessesUpdatesDeletesAndLastReadSignals()
{
    auto db = makeInitializedDb(QStringLiteral("notifications-updates.sqlite"));

    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);
    runtime.settings_.setValue("notificatins/last_read", 1u);

    NotificationsModel model(runtime);
    QSignalSpy last_read_spy(&model, &NotificationsModel::lastReadChanged);
    QSignalSpy unread_spy(&model, &NotificationsModel::unreadChanged);

    const auto now = static_cast<quint64>(QDateTime::currentSecsSinceEpoch());
    const auto first = makeNotification(2, QStringLiteral("Second"), now - 60, 200, 20);
    const auto second = makeNotification(3, QStringLiteral("Third"), now - 30, 220, 22);
    QVERIFY(waitForTask(model.saveBatch({first, second})));
    QVERIFY(waitForTask(model.loadFromCache()));

    QCOMPARE(model.rowCount({}), 2);
    QCOMPARE(model.data(model.index(0, 0, {}), NotificationsModel::idRole).toUInt(), 3u);
    QVERIFY(model.unread());

    auto deleted = second;
    deleted.setKind(nextapp::pb::Notification::Kind::DELETED);
    QVERIFY(waitForTask(model.save(deleted)));
    QVERIFY(waitForTask(model.loadFromCache()));
    QCOMPARE(model.rowCount({}), 1);
    QCOMPARE(model.data(model.index(0, 0, {}), NotificationsModel::idRole).toUInt(), 2u);

    model.setLastRead(2);
    QCOMPARE(runtime.server_comm_.last_read_notification_id_, 2u);
    QCOMPARE(runtime.settings_.value("notificatins/last_read").toUInt(), 2u);
    QVERIFY(!model.unread());
    QCOMPARE(last_read_spy.count(), 1);
    QVERIFY(unread_spy.count() >= 2);

    auto last_read_update = std::make_shared<nextapp::pb::Update>();
    last_read_update->setLastReadNotificationId(5);
    waitForTask(model.pocessUpdate(last_read_update));

    QCOMPARE(runtime.server_comm_.last_read_notification_id_, 2u);
    QCOMPARE(runtime.settings_.value("notificatins/last_read").toUInt(), 5u);
    QCOMPARE(last_read_spy.count(), 2);

    db->close();
}

void tst_NextAppUiRuntime::importExportModelDispatchesExportAndImportThroughInjectedComms()
{
    TestRuntimeServices runtime;
    ImportExportModel model(runtime);

    const auto export_path = temp_dir_.filePath(QStringLiteral("export.nextapp"));
    const auto export_url = QUrl::fromLocalFile(export_path);
    model.exportData(export_url);

    QTRY_VERIFY(runtime.server_comm_.last_export_file_);
    QVERIFY(runtime.server_comm_.last_write_export_fn_);
    QCOMPARE(runtime.server_comm_.last_export_file_->fileName(), export_path);
    QVERIFY(!model.property("working").toBool());

    auto first = makeUserStatus(
        QStringLiteral("11111111-1111-1111-1111-111111111111"),
        QStringLiteral("Export User"),
        QStringLiteral("export@example.com"),
        true);
    auto last = makeUserStatus(
        QStringLiteral("22222222-2222-2222-2222-222222222222"),
        QStringLiteral("Last User"),
        QStringLiteral("last@example.com"),
        false);

    runtime.server_comm_.last_write_export_fn_(first, *runtime.server_comm_.last_export_file_);
    runtime.server_comm_.last_write_export_fn_(last, *runtime.server_comm_.last_export_file_);
    runtime.server_comm_.last_export_file_->flush();
    runtime.server_comm_.last_export_file_->close();

    QFile exported_file(export_path);
    QVERIFY(exported_file.open(QIODevice::ReadOnly));
    const auto exported_data = exported_file.readAll();
    QVERIFY(exported_data.size() > static_cast<int>(sizeof(ImportExportModel::FileHeader)));
    QCOMPARE(exported_data.left(7), QByteArray("NextApp", 7));
    exported_file.close();

    runtime.server_comm_.last_read_export_fn_ = {};
    runtime.server_comm_.consume_import_read_immediately_ = true;
    model.importData(export_url);

    QTRY_VERIFY(runtime.server_comm_.last_read_export_fn_);
    QTRY_COMPARE(runtime.server_comm_.imported_messages_.size(), 2);
    QVERIFY(runtime.server_comm_.imported_messages_.at(0).hasUser());
    QCOMPARE(runtime.server_comm_.imported_messages_.at(0).user().name(), QStringLiteral("Export User"));
    QVERIFY(runtime.server_comm_.imported_messages_.at(0).hasHasMore());
    QVERIFY(runtime.server_comm_.imported_messages_.at(0).hasMore());
    QVERIFY(runtime.server_comm_.imported_messages_.at(1).hasUser());
    QCOMPARE(runtime.server_comm_.imported_messages_.at(1).user().email(), QStringLiteral("last@example.com"));
    QVERIFY(runtime.server_comm_.imported_messages_.at(1).hasHasMore());
    QVERIFY(!runtime.server_comm_.imported_messages_.at(1).hasMore());
    QCOMPARE(runtime.server_comm_.resync_calls_, 0);
    QVERIFY(!model.property("working").toBool());

    const auto json_path = temp_dir_.filePath(QStringLiteral("export.json"));
    const auto json_url = QUrl::fromLocalFile(json_path);
    model.exportData(json_url);

    QTRY_VERIFY(runtime.server_comm_.last_export_file_);
    QVERIFY(runtime.server_comm_.last_write_export_fn_);
    auto json_status = makeUserStatus(
        QStringLiteral("33333333-3333-3333-3333-333333333333"),
        QStringLiteral("Json User"),
        QStringLiteral("json@example.com"),
        false);
    runtime.server_comm_.last_write_export_fn_(json_status, *runtime.server_comm_.last_export_file_);
    runtime.server_comm_.last_export_file_->flush();
    runtime.server_comm_.last_export_file_->close();

    QFile json_file(json_path);
    QVERIFY(json_file.open(QIODevice::ReadOnly));
    const auto json_text = QString::fromUtf8(json_file.readAll());
    QVERIFY(json_text.contains(QStringLiteral("\"user\"")));
    QVERIFY(json_text.contains(QStringLiteral("Json User")));
    QVERIFY(json_text.trimmed().startsWith(QLatin1Char('{')));
    QVERIFY(json_text.trimmed().endsWith(QLatin1Char('}')));
    json_file.close();
}

void tst_NextAppUiRuntime::importExportModelRejectsInvalidInputsWithoutCallingImport()
{
    TestRuntimeServices runtime;
    ImportExportModel model(runtime);

    model.importData(QUrl{});
    QVERIFY(!runtime.server_comm_.last_read_export_fn_);

    const auto invalid_path = temp_dir_.filePath(QStringLiteral("invalid.nextapp"));
    QFile invalid_file(invalid_path);
    QVERIFY(invalid_file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    invalid_file.write("bad");
    invalid_file.close();

    model.importData(QUrl::fromLocalFile(invalid_path));
    QVERIFY(!runtime.server_comm_.last_read_export_fn_);
    QVERIFY(!model.property("working").toBool());

    const auto missing_export_path = temp_dir_.filePath(QStringLiteral("missing/blocked.nextapp"));
    model.exportData(QUrl::fromLocalFile(missing_export_path));
    QVERIFY(!runtime.server_comm_.last_export_file_ || runtime.server_comm_.last_export_file_->fileName() != missing_export_path);
    QVERIFY(!QFileInfo::exists(missing_export_path));
}

void tst_NextAppUiRuntime::actionInfoCacheComputesStableScores()
{
    auto low = makeAction(
        QStringLiteral("dddddddd-dddd-dddd-dddd-dddddddddddd"),
        QStringLiteral("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"),
        QStringLiteral("Low"),
        1,
        100,
        10);

    nextapp::pb::Priority low_priority;
    low_priority.setPriority(nextapp::pb::ActionPriorityGadget::ActionPriority::PRI_LOW);
    low.setDynamicPriority(low_priority);

    auto high = makeAction(
        QStringLiteral("eeeeeeee-eeee-eeee-eeee-eeeeeeeeeeee"),
        QStringLiteral("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"),
        QStringLiteral("High"),
        1,
        100,
        11);

    nextapp::pb::Priority high_priority;
    high_priority.setPriority(nextapp::pb::ActionPriorityGadget::ActionPriority::PRI_HIGH);
    high.setDynamicPriority(high_priority);

    const auto low_score = ActionInfoCache::getScore(low);
    const auto high_score = ActionInfoCache::getScore(high);
    QVERIFY(high_score > low_score);

    nextapp::pb::UrgencyImportance ui;
    ui.setUrgency(9.0f);
    ui.setImportance(9.0f);
    nextapp::pb::Priority urgent_priority;
    urgent_priority.setUrgencyImportance(ui);
    urgent_priority.setScore(80);
    high.setDynamicPriority(urgent_priority);
    high.setTimeEstimate(15);
    high.setTimeSpent(10);

    common::Time due_time;
    due_time.setUnixTime(static_cast<quint64>(QDateTime::currentSecsSinceEpoch() + 3600));
    nextapp::pb::Due due;
    due.setKind(nextapp::pb::ActionDueKindGadget::ActionDueKind::DATETIME);
    due.setDue(due_time.unixTime());
    due.setTimezone(QStringLiteral("Europe/Sofia"));
    high.setDue(due);

    const auto urgent_score = ActionInfoCache::getScore(high);
    QVERIFY(urgent_score >= high_score);

    const auto color = ActionInfoCache::getScoreColor(urgent_score);
    QVERIFY(color.isValid());
}

void tst_NextAppUiRuntime::actionInfoCacheRepairsOriginsPersistsTagsAndReloads()
{
    ActionInfoCache::instance_ = nullptr;

    auto db = makeInitializedDb(QStringLiteral("action-info-repair.sqlite"));
    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);
    MainTreeModel tree(runtime);
    const auto node_id = QStringLiteral("10101010-1010-1010-1010-101010101010");
    QVERIFY(waitForTask(tree.save(makeNode(
        node_id,
        QStringLiteral("Node"),
        nextapp::pb::Node::Kind::FOLDER,
        {},
        1,
        1))));

    {
        ActionInfoCache cache(runtime);
        cache.setState(ActionInfoCache::State::SYNCHING);

        const auto parent = makeAction(
            QStringLiteral("aaaaaaaa-1111-1111-1111-111111111111"),
            node_id,
            QStringLiteral("Parent"),
            1,
            100,
            10,
            std::nullopt,
            {QStringLiteral("focus"), QStringLiteral("home")});
        const auto child = makeAction(
            QStringLiteral("bbbbbbbb-2222-2222-2222-222222222222"),
            node_id,
            QStringLiteral("Child"),
            1,
            101,
            11,
            QStringLiteral("aaaaaaaa-1111-1111-1111-111111111111"),
            {QStringLiteral("child")});

        QVERIFY(waitForTask(cache.save(child)));
        QVERIFY(waitForTask(cache.save(parent)));
        QVERIFY(waitForTask(cache.finalizeSyncPersistence()));
        QVERIFY(waitForTask(cache.loadLocally()));

        auto loaded_child = waitForTask(cache.get(
            QUuid(QStringLiteral("bbbbbbbb-2222-2222-2222-222222222222")),
            false));
        QVERIFY(loaded_child);
        QCOMPARE(loaded_child->origin(), QStringLiteral("aaaaaaaa-1111-1111-1111-111111111111"));
        QVERIFY(loaded_child->tags().contains(QStringLiteral("child")));

        const auto stored_origin = waitForTask(db->queryOne<QString>(
            "SELECT origin FROM action WHERE id = ?",
            QStringLiteral("bbbbbbbb-2222-2222-2222-222222222222")));
        QVERIFY(stored_origin);
        QCOMPARE(stored_origin.value(), QStringLiteral("aaaaaaaa-1111-1111-1111-111111111111"));

        const auto tags = waitForTask(db->legacyQuery(
            "SELECT name FROM tag WHERE action = 'aaaaaaaa-1111-1111-1111-111111111111' ORDER BY name"));
        QVERIFY(tags.has_value());
        QCOMPARE(tags->size(), 2);
        QCOMPARE(tags->at(0).at(0).toString(), QStringLiteral("focus"));
        QCOMPARE(tags->at(1).at(0).toString(), QStringLiteral("home"));
    }

    ActionInfoCache::instance_ = nullptr;
    db->close();
}

void tst_NextAppUiRuntime::actionInfoCacheUpdateReloadsMissingOriginsAndInvalidDeletesRequestResync()
{
    ActionInfoCache::instance_ = nullptr;

    auto db = makeInitializedDb(QStringLiteral("action-info-updates.sqlite"));
    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);
    MainTreeModel tree(runtime);
    const auto node_id = QStringLiteral("20202020-2020-2020-2020-202020202020");
    QVERIFY(waitForTask(tree.save(makeNode(
        node_id,
        QStringLiteral("Node"),
        nextapp::pb::Node::Kind::FOLDER,
        {},
        1,
        1))));

    {
        ActionInfoCache cache(runtime);

        const auto parent = makeAction(
            QStringLiteral("cccccccc-3333-3333-3333-333333333333"),
            node_id,
            QStringLiteral("Origin"),
            1,
            200,
            20);
        QVERIFY(waitForTask(cache.save(parent)));
        cache.clear();

        QSignalSpy reload_spy(&cache, &ActionInfoCache::cacheReloaded);
        const auto child = makeAction(
            QStringLiteral("dddddddd-4444-4444-4444-444444444444"),
            node_id,
            QStringLiteral("Needs reload"),
            1,
            201,
            21,
            QStringLiteral("cccccccc-3333-3333-3333-333333333333"));

        waitForTask(cache.pocessUpdate(std::make_shared<nextapp::pb::Update>(
            makeActionUpdate(nextapp::pb::Update::Operation::ADDED, child))));

        QCOMPARE(reload_spy.count(), 2);
        auto loaded_child = waitForTask(cache.get(
            QUuid(QStringLiteral("dddddddd-4444-4444-4444-444444444444")),
            false));
        QVERIFY(loaded_child);
        QCOMPARE(loaded_child->origin(), QStringLiteral("cccccccc-3333-3333-3333-333333333333"));

        auto invalid_deleted = child;
        invalid_deleted.setStatus(nextapp::pb::ActionStatusGadget::ActionStatus::ACTIVE);
        waitForTask(cache.pocessUpdate(std::make_shared<nextapp::pb::Update>(
            makeActionUpdate(nextapp::pb::Update::Operation::DELETED, invalid_deleted))));

        QTRY_COMPARE(runtime.server_comm_.resync_calls_, 1);
    }

    ActionInfoCache::instance_ = nullptr;
    db->close();
}

void tst_NextAppUiRuntime::mainTreeModelReloadsFromCacheWhenUpdateTargetsMissingNode()
{
    auto db = makeInitializedDb(QStringLiteral("main-tree-reload-update.sqlite"));
    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);
    MainTreeModel model(runtime);

    const auto root = makeNode(
        QStringLiteral("66666666-6666-6666-6666-666666666666"),
        QStringLiteral("Root"),
        nextapp::pb::Node::Kind::FOLDER,
        {},
        100,
        10);
    QVERIFY(waitForTask(model.save(root)));
    QVERIFY(waitForTask(model.loadLocally()));
    QCOMPARE(model.state(), MainTreeModel::State::VALID);
    QCOMPARE(model.rowCount(model.indexFromUuid(root.uuid())), 0);

    const auto child = makeNode(
        QStringLiteral("77777777-7777-7777-7777-777777777777"),
        QStringLiteral("Reloaded Child"),
        nextapp::pb::Node::Kind::TASK,
        root.uuid(),
        101,
        11);
    waitForTask(model.pocessUpdate(std::make_shared<nextapp::pb::Update>(
        makeNodeUpdate(nextapp::pb::Update::Operation::UPDATED, child))));

    const auto root_index = model.indexFromUuid(root.uuid());
    QVERIFY(root_index.isValid());
    QCOMPARE(model.rowCount(root_index), 1);
    QCOMPARE(model.nodeNameFromUuid(child.uuid()), QStringLiteral("Reloaded Child"));

    db->close();
}

void tst_NextAppUiRuntime::mainTreeModelDeleteUpdateClearsSelectionAndEmitsNodeDeleted()
{
    auto db = makeInitializedDb(QStringLiteral("main-tree-delete-update.sqlite"));
    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);
    MainTreeModel model(runtime);

    const auto root = makeNode(
        QStringLiteral("88888888-8888-8888-8888-888888888888"),
        QStringLiteral("Root"),
        nextapp::pb::Node::Kind::FOLDER,
        {},
        100,
        10);
    const auto child = makeNode(
        QStringLiteral("99999999-9999-9999-9999-999999999999"),
        QStringLiteral("Child"),
        nextapp::pb::Node::Kind::TASK,
        root.uuid(),
        101,
        11);

    QVERIFY(waitForTask(model.saveBatch({root, child})));
    QVERIFY(waitForTask(model.loadLocally()));
    model.setSelected(child.uuid());

    QSignalSpy deleted_spy(&model, &MainTreeModel::nodeDeleted);

    auto deleted_child = child;
    deleted_child.setDeleted(true);
    waitForTask(model.pocessUpdate(std::make_shared<nextapp::pb::Update>(
        makeNodeUpdate(nextapp::pb::Update::Operation::DELETED, deleted_child))));

    QCOMPARE(deleted_spy.count(), 1);
    QCOMPARE(model.selected(), QString{});
    QVERIFY(!model.indexFromUuid(child.uuid()).isValid());
    QCOMPARE(model.rowCount(model.indexFromUuid(root.uuid())), 0);

    db->close();
}

void tst_NextAppUiRuntime::mainTreeModelFailsLocalLoadWithDanglingParent()
{
    auto db = makeInitializedDb(QStringLiteral("main-tree-dangling-parent.sqlite"));
    TestRuntimeServices runtime;
    runtime.setDbForTest(*db);
    MainTreeModel model(runtime);

    const auto orphan = makeNode(
        QStringLiteral("abababab-abab-abab-abab-abababababab"),
        QStringLiteral("Orphan"),
        nextapp::pb::Node::Kind::TASK,
        QStringLiteral("cdcdcdcd-cdcd-cdcd-cdcd-cdcdcdcdcdcd"),
        200,
        20);
    QVERIFY(waitForTask(model.saveBatch({orphan})));
    QVERIFY(!waitForTask(model.loadLocally()));
    QVERIFY(!model.indexFromUuid(orphan.uuid()).isValid());

    db->close();
}

void tst_NextAppUiRuntime::useCaseTemplatesExposeSortedNamesAndCreateSelectedTree()
{
    TestRuntimeServices runtime;
    UseCaseTemplates templates(runtime);

    const auto names = templates.getTemplateNames();
    QVERIFY(names.size() > 2);
    QCOMPARE(names.front(), QStringLiteral("-- None --"));
    QVERIFY(std::is_sorted(names.begin() + 1, names.end()));
    QCOMPARE(templates.getDescription(0), QString{});

    templates.createFromTemplate(0);
    QVERIFY(runtime.server_comm_.created_node_template_.children().isEmpty());

    const auto index = names.indexOf(QStringLiteral("Front-end Developer"));
    QVERIFY(index > 0);
    QVERIFY(!templates.getDescription(index).isEmpty());

    templates.createFromTemplate(index);

    const auto root = runtime.server_comm_.created_node_template_;
    const auto top_level = root.children();
    QVERIFY(!top_level.isEmpty());

    const auto inbox_it = std::find_if(top_level.begin(), top_level.end(), [](const auto& child) {
        return child.name() == QStringLiteral("Inbox");
    });
    QVERIFY(inbox_it != top_level.end());
    QCOMPARE(inbox_it->kind(), nextapp::pb::Node::Kind::FOLDER);

    const auto projects_it = std::find_if(top_level.begin(), top_level.end(), [](const auto& child) {
        return child.name() == QStringLiteral("Projects");
    });
    QVERIFY(projects_it != top_level.end());
    QCOMPARE(projects_it->kind(), nextapp::pb::Node::Kind::FOLDER);
    QVERIFY(!projects_it->children().isEmpty());
    QCOMPARE(projects_it->children().front().name(), QStringLiteral("Portfolio Website"));
    QCOMPARE(projects_it->children().front().kind(), nextapp::pb::Node::Kind::PROJECT);
    QVERIFY(!projects_it->children().front().children().isEmpty());
    QCOMPARE(projects_it->children().front().children().front().name(), QStringLiteral("Design landing page"));

    const auto out_of_range = names.size() + 1;
    templates.createFromTemplate(out_of_range);
    QCOMPARE(runtime.server_comm_.created_node_template_.children().size(), top_level.size());
}

QTEST_GUILESS_MAIN(tst_NextAppUiRuntime)

#include "tst_nextappui_runtime.moc"

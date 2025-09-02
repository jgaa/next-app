
#include <array>
#include <ranges>
#include <string_view>
#include <span>
#include <algorithm>

#include <QStandardPaths>
#include <QSqlQuery>
#include <QFile>
#include <QList>
#include <QVariant>
#include <QDir>
#include "qcorofuture.h"
#include <QSqlError>
#include <QSqlRecord>
#include "qcorosignal.h"
#include "qcorotimer.h"

#include "AppInstanceMgr.h"
#include "DbStore.h"

#include "nextapp.h"
#include "logging.h"

using namespace std;

namespace {

#include <QCryptographicHash>
#include <QSqlQuery>
#include <QSqlRecord>

void putVarUint(QByteArray& out, quint64 x) {
    while (x >= 0x80) { out.append(char((x & 0x7F) | 0x80)); x >>= 7; }
    out.append(char(x));
}

void putI64(QByteArray& out, qint64 v) {
    for (int i = 0; i < 8; ++i) out.append(char((quint64(v) >> (8*i)) & 0xFF));
}

void putF64(QByteArray& out, double d) {
    quint64 bits; static_assert(sizeof(double) == 8, "double must be 8 bytes");
    memcpy(&bits, &d, 8);
    for (int i = 0; i < 8; ++i) out.append(char((bits >> (8*i)) & 0xFF));
}

QByteArray serializeRow(const QSqlQuery& q) {
    QByteArray out;
    out.append(char(0x7E));                 // row marker
    const int n = q.record().count();
    for (int i = 0; i < n; ++i) {
        if (q.isNull(i)) { out.append(char(0x00)); continue; }         // NULL
        const QVariant v = q.value(i);
        switch (v.metaType().id()) {
        case QMetaType::Int:
        case QMetaType::LongLong:
        case QMetaType::UInt:
        case QMetaType::ULongLong:
        case QMetaType::Short:
        case QMetaType::Long:
            out.append(char(0x01));                                  // int tag
            putI64(out, v.toLongLong());
            break;
        case QMetaType::Double:
        case QMetaType::Float:
            out.append(char(0x02));                                  // float tag
            putF64(out, v.toDouble());
            break;
        case QMetaType::QByteArray: {
            out.append(char(0x04));                                  // blob tag
            QByteArray b = v.toByteArray();
            putVarUint(out, quint64(b.size()));
            out.append(b);
            break;
        }
        default: {                                                   // text tag
            out.append(char(0x03));
            QByteArray b = v.toString().toUtf8();
            putVarUint(out, quint64(b.size()));
            out.append(b);
            break;
        }
        }
    }
    return out;
}

QByteArray blobFromQuery(QSqlDatabase db, const QString& sql)
{
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (!q.exec(sql)) return QByteArray();

    QByteArray all;
    // Optional: include column count so schema changes alter the blob
    quint32 cols = q.record().count();
    all.append(char(0x55)); all.append(char(0xAA));
    all.append(char(cols & 0xFF));
    all.append(char((cols >> 8) & 0xFF));
    while (q.next())
        all += serializeRow(q);
    return all;
}

bool sha3OfQuery(QCryptographicHash& h, QSqlDatabase db, const QString& sql)
{
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (!q.exec(sql)) {
        LOG_WARN_N << "Failed to execute query: " << sql
                   << ", db=" << q.lastError().databaseText()
                   << ", driver=" << q.lastError().driverText();
        return false;
    }

    // Include column count (force a defined byte order)
    quint32 cols = q.record().count();
    const quint32 colsLE = qToLittleEndian(cols);
    h.addData(QByteArrayView(reinterpret_cast<const char*>(&colsLE),
                             qsizetype(sizeof colsLE)));

    while (q.next()) {
        const QByteArray row = serializeRow(q);     // your existing serializer
        // Either of these is fine; both are non-deprecated:
        // h.addData(row);
        h.addData(QByteArrayView(row));
    }
    return true;
}


} // anon ns

DbStore::DbStore(QObject *parent)
    : QObject{parent}, thread_{new QThread{this}}
{
    mutex_.lock();
    if constexpr (use_worker_thread) {
        QObject::connect(thread_, &QThread::started, this, &DbStore::start);
        QObject::connect(thread_, &QThread::finished, this, []() {
            LOG_TRACE_N << "Worker thread finished";
        });
        thread_->start();
        moveToThread(thread_);

        LOG_TRACE_N << "Now attached to worker thread";
    }
}

DbStore::~DbStore()
{
    LOG_TRACE_N << "Destroying db store";
}

QCoro::Task<tl::expected<nextapp::pb::UserDataInfo, DbStore::Error> > DbStore::getDbDataInfo()
{
    // wrap to worker-thread
    co_return co_await runInWorkerThread<tl::expected<nextapp::pb::UserDataInfo, DbStore::Error>>([this]()
            -> tl::expected<nextapp::pb::UserDataInfo, DbStore::Error> {
        nextapp::pb::UserDataInfo info;
        if (const auto& res = getDbDataHash()) {
            info.setHash(res.value());
        } else {
            return tl::make_unexpected(res.error());
        }

        auto get_count = [&](const QString& table) {
            if (auto res = executeQuery(QString{"SELECT COUNT(*) FROM %1"}.arg(table), {})) {
                if (!res->rows.empty() && !res->rows.front().empty()) {
                    bool ok = false;
                    auto count = res->rows.front().front().toUInt(&ok);
                    if (ok) {
                        return count;
                    }
                    throw std::runtime_error("Failed to convert count to uint");
                } else {
                    throw std::runtime_error("No rows returned when counting table " + table.toStdString());
                }
            } else {
                throw std::runtime_error("Failed to count table " + table.toStdString() + ": "
                                         + to_string(static_cast<int>(res.error())));
            }
        };

        try {
            info.setNumActions(get_count("action"));
            info.setNumActionCategories(get_count("action_category"));
            info.setNumNodes(get_count("node"));
            info.setNumDays(get_count("day"));
            info.setNumWorkSessions(get_count("work_session"));
            info.setNumTimeBlocks(get_count("time_block"));

        } catch (const std::exception& e) {
            LOG_ERROR << "Exception when getting DB info: " << e.what();
            return tl::make_unexpected(GENERIC_ERROR);
        }

        return info;
    });
}

void DbStore::start() {

    if constexpr (use_worker_thread) {
        LOG_TRACE_N << "Now running in worker thread";

        {
            // Wait for init() to be called
            lock_guard lock{mutex_};
        }
    }

    try {
        createDbObject();
    } catch (const std::exception &e) {
        LOG_ERROR << "Failed to create db object: " << e.what();
        emit error(GENERIC_ERROR);
        assert(false);
        return;
    }
    connect(this, &DbStore::doQuery, this, &DbStore::queryImpl, Qt::QueuedConnection);
    emit initialized();
}

void DbStore::createDbObject()
{
    LOG_TRACE_N << "Initializing db store";
    bool is_retry{false};

again:
    db_ = make_unique<QSqlDatabase>(QSqlDatabase::addDatabase("QSQLITE"));
    data_dir_ = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + AppInstanceMgr::instance()->name() + "/";
    LOG_INFO << "Using data dir: " << data_dir_;
    if (!QFile::exists(data_dir_)) {
        LOG_INFO << "Database directory does not exist, creating it";
        if (!QDir{}.mkpath(data_dir_)) {
            LOG_ERROR << "Failed to create database directory: " << data_dir_;
            emit error(GENERIC_ERROR);
            assert(false);
            return;
        }
    }

    db_path_ = data_dir_ + "/db.sqlite";

    if (clear_pending_) {
        QFile file(db_path_);
        if (file.exists()) {
            LOG_WARN_N << "Deleting the existing database. Clear db was pending.";
            if (!file.remove()) {
                LOG_WARN << "Failed to remove database file: " << db_path_;
            }
        }
        clear_pending_ = false;
    }

    db_was_initialized_ = !QFile{db_path_}.exists();

    db_->setDatabaseName(db_path_);
    if (!db_->open()) {
        LOG_ERROR << "Failed to open database: \"" << db_path_ << "\", error=" << db_->lastError().text();
        emit error(GENERIC_ERROR);
        assert(false);
        return;
    }

    {
        QSqlQuery query{*db_};
        query.exec("PRAGMA foreign_keys = ON");
    }

    {
        QSqlQuery query{*db_};
        query.exec("SELECT sqlite_version()");
        if (query.next()) {
            LOG_INFO << "SQLite version: " << query.value(0).toString();
        }
    }

    const auto version = getDbVersion();

    if (!updateSchema(version)) {
        if (is_retry) {
            LOG_ERROR_N << "Failed to update the database schema, and retrying with a clean database failed.";
            throw std::runtime_error("Failed to update the database schema and retrying with a clean database failed.");
        }
        is_retry = true;
        clear_pending_ = true;
        LOG_WARN_N << "Failed to update the database schema, retrying with a clean database...";
        goto again;
    }
}

QCoro::Task<DbStore::rval_t> DbStore::legacyQuery(const QString &sql, const QList<QVariant> *params)
{
    QPromise<rval_t> promise;

    if constexpr (use_worker_thread) {
        emit doQuery(sql, params, &promise);
    } else {
        queryImpl(sql, params, &promise);
    }

    auto future = promise.future();

    co_return co_await qCoro(future).takeResult();
}

QCoro::Task<bool> DbStore::init() {
    // Once called, the database will be initialized in the worker thread.
    if constexpr (use_worker_thread) {
        mutex_.unlock();
        //co_await
    } else {
        start();
    }

    // TODO: Handle errors
    co_return true;
}

void DbStore::close()
{
    LOG_DEBUG_N << "Closing the database";
    QMetaObject::invokeMethod(this, [&]() {
        if (db_ && db_->isOpen()) {
            db_->close();
            db_.reset();
        }
    });
    // We need to waut for the worker thread to finish
    if constexpr (use_worker_thread) {
        LOG_DEBUG_N << "Waiting for the DB worker thread to finish";
        if (thread_ && thread_->isRunning()) {
            thread_->quit();
            thread_->wait();
        }
    }
    LOG_DEBUG_N << "Database closed";
}

QCoro::Task<bool> DbStore::clear()
{
    co_return co_await runInWorkerThread<bool>([this]() -> bool {
        return clear_();
    });
}

bool DbStore::clear_()
{
    LOG_INFO_N << "Clearing the database. All data will be deleted.";
    if (!db_) {
        clear_pending_ = true;
        return false;
    }

    if (db_->isOpen()) {
        LOG_DEBUG_N << "Closing the database before clearing it";
        db_->close();
        db_.reset();
    }

    // delete the file pointed to by db_path
    QFile file(db_path_);
    if (file.exists()) {
        LOG_DEBUG_N << "Removing the database file: " << db_path_;
        if (!file.remove()) {
            LOG_WARN << "Failed to remove database file: " << db_path_;
        }
    }

    LOG_DEBUG_N << "Re-creating the database object";
    createDbObject();
    return true;
}

QCoro::Task<void> DbStore::closeAndDeleteDb()
{
    LOG_DEBUG_N << "Closing and deleting the database";
    QMetaObject::invokeMethod(this, [&]() {
        if (db_ && db_->isOpen()) {
            db_->close();
            db_.reset();
            if constexpr (use_worker_thread) {
                if (thread_ && thread_->isRunning()) {
                    LOG_DEBUG_N << "Stopping the worker thread";
                    thread_->quit();
                }
            }
        }
    });

    // Wait for the worker thread to finish
    if constexpr (use_worker_thread) {
        LOG_DEBUG_N << "Waiting for the DB worker thread to finish";
        if (thread_ && thread_->isRunning()) {
            LOG_DEBUG_N << "Worker thread is running, waiting for it to finish";
            co_await QCoro::sleepFor(500ms);
        }
    }

    QFile file(db_path_);
    if (file.exists()) {
        if (!file.remove()) {
            LOG_WARN << "Failed to remove database file: " << db_path_;
        }
    }

    LOG_DEBUG_N << "Database closed and deleted";

    co_return;
}

tl::expected<QString, DbStore::Error> DbStore::getDbDataHash()
{
    QCryptographicHash h(QCryptographicHash::Sha1); // We don't need cryptographic strength here

    array<pair<QString, QString>, 6> tables = {{
        {"node", "SELECT * FROM node ORDER BY uuid"},
        {"action_category", "SELECT * FROM action_category ORDER BY id"},
        // For actions, we need to remove the dynamic fields we update locally so the hash is reliable between devices.
        {"action", "SELECT id, node, origin, category, priority, dyn_importance, dyn_urgency, dyn_score, status, favorite, name, descr, created_date, due_kind, start_time, due_by_time, due_timezone, completed_time, time_estimate, difficulty, repeat_kind, repeat_unit, repeat_when, repeat_after, kind, version, tags, tags_hash FROM action ORDER BY id"},
        {"day", "SELECT * FROM day ORDER BY date"},
        {"work_session", "SELECT * FROM work_session ORDER BY id"},
        {"time_block", "SELECT * FROM time_block ORDER BY id"},
    }};

    for(const auto& [table, query] : tables) {
        if (!sha3OfQuery(h, *db_, query)) {
            LOG_WARN_N << "Failed to get hash for table: " << table;
            return tl::make_unexpected(Error::QUERY_FAILED);
        }
    }

    return h.result().toHex();
}

DbStore::qrval_t DbStore::executeQuery(const QString &sql, const QList<QVariant> &params) {
    QSqlQuery query{*db_};
    if (!query.prepare(sql)) {
        LOG_ERROR_N << "Failed to prepare statement: \"" << sql
                    << "\", db=" << query.lastError().databaseText()
                    << ", driver=" << query.lastError().driverText()
                    << ", params=#" << params.size();
        return tl::make_unexpected(Error::PREPARE_STATEMENT_FAILED);
    }

    for (const auto &param : params) {
        query.addBindValue(param);
    }

    if (!query.exec()) {
        LOG_ERROR_N << "Failed to execute: \"" << sql
                    << "\", db=" << query.lastError().databaseText()
                    << ", driver=" << query.lastError().driverText()
                    << ", params=#" << params.size();
        return tl::make_unexpected(Error::QUERY_FAILED);
    }

    LOG_TRACE_N << "Executing query: " << sql << " with " << params.size() << " params";

    QueryResult result;
    while (query.next()) {
        QList<QVariant> row;
        for (int i = 0; i < query.record().count(); ++i) {
            row.append(query.value(i));
        }
        result.rows.append(row);
    }

    if (auto num = query.numRowsAffected(); num > 0) {
        result.affected_rows = num;
    };

    if (query.lastInsertId().isValid()) {
        result.insert_id = query.lastInsertId().toUInt();
    }

    return result;
}

QCoro::Task<DbStore::qrval_t> DbStore::runQueryInWorker(const QString &sql, const QList<QVariant> &params) {
    QMetaObject::Connection conn;

    if (QThread::currentThread() == thread_) {
        // Run immediately if already in the correct thread
        co_return executeQuery(sql, params);
    }

    // Use a promise to wait for the result asynchronously
    auto promise = QSharedPointer<QPromise<qrval_t>>::create();

    // Schedule the query execution in the worker thread
    auto future = promise->future();
    QMetaObject::invokeMethod(this, [this, promise, sql, params]() mutable {
        qrval_t result = executeQuery(sql, params);
        promise->start();
        promise->addResult(result);
        promise->finish();
    }, Qt::QueuedConnection);

    QFutureWatcher<qrval_t> watcher;
    watcher.setFuture(future);

    // Move the watcher to the correct thread
    //watcher.moveToThread(thread_);

    // Wait for completion in a coroutine-safe way
    (void) co_await watcher.future();
    co_return future.result();
}

bool DbStore::batchQueryImpl(QSqlQuery &query)
{
    if (query.exec()) {
        return true;
    }

    LOG_ERROR_N << "Failed to execute: \"" << query.executedQuery()
                << "\", db=" << query.lastError().databaseText()
                << ", driver=" << query.lastError().driverText()
                << ", params=#" << query.boundValues().size();
    return false;
}

DbStore::rval_t DbStore::queryImpl_(const QString &sql, const param_t *params)
{
    const auto args_count = params ? params->size() : 0u;
    LOG_TRACE_N << "Querying db: \"" << sql
                << "\" with " << args_count << " args";
    QSqlQuery query{*db_};
    rval_t rval;

    bool success{false};

    if (!params || params->empty()) {
        success = query.exec(sql);
    } else {
        query.prepare(sql);
        for (int i = 0; i < params->size(); ++i) {
            query.bindValue(i, params->at(i));
        }
        success = query.exec();
    }

    if (success) {
        QList<QList<QVariant>> rows;
        if (auto num_rows = query.size(); num_rows > 0) {
            rows.reserve(num_rows);
        }
        const auto num_cols = query.record().count();
        while (query.next()) {
            QList<QVariant> cols;
            cols.reserve(num_cols);
            for (int i = 0; i < num_cols; ++i) {
                cols.append(query.value(i));
            }

            rows.emplace_back(std::move(cols));
        }
        rval = std::move(rows);
    } else {
        LOG_ERROR_N << "Failed to execute: \"" << sql
                    << "\", db=" << query.lastError().databaseText()
                    << ", driver=" << query.lastError().driverText()
                    << ", params=#" << args_count;
        rval = tl::unexpected(QUERY_FAILED);
    }
    return rval;
}

void DbStore::queryImpl(const QString &sql, const QList<QVariant>* params, QPromise<rval_t> *promise)
{
    assert(promise);
    promise->start();
    promise->addResult(queryImpl_(sql, params));
    promise->finish();
}

bool DbStore::updateSchema(uint version)
{
    static constexpr auto v1_bootstrap = to_array<string_view>({
        "CREATE TABLE nextapp (id INTEGER NOT NULL, version INTEGER NOT NULL, data_epoc INTEGER NOT NULL) ",
        "INSERT INTO nextapp (id, version, data_epoc) values(1, 0, 0)",

        R"(CREATE TABLE IF NOT EXISTS "day" (
            "date" DATE NOT NULL,
            "color" VARCHAR(32),
            "notes" TEXT,
            "report" TEXT,
            "updated" INTEGER NOT NULL,
            PRIMARY KEY("date")
        ))",

        "CREATE INDEX IF NOT EXISTS day_updated_ix ON day(updated)",

        R"(CREATE TABLE IF NOT EXISTS "day_colors" (
            "id" VARCHAR(32) NOT NULL,
            "score" INTEGER NOT NULL,
            "color" VARCHAR(32) NOT NULL,
            "name" VARCHAR(255) NOT NULL,
            "updated" INTEGER NOT NULL,
            PRIMARY KEY("id")
        ))",

        "CREATE INDEX IF NOT EXISTS day_colors_updated_ix ON day_colors(updated)",

        // The nodes will be cached in memory, so no need to store much data for indexing
        R"(CREATE TABLE IF NOT EXISTS "node" (
            "uuid" VARCHAR(32) NOT NULL,
            "parent" VARCHAR(32),
            "active" BOOLEAN NOT NULL,
            "updated" INTEGER NOT NULL,
            "name" VARCHAR(256) NOT NULL,
            "exclude_from_wr" BOOLEAN NOT NULL,
            "data" BLOB NOT NULL,
            PRIMARY KEY("uuid")
        ))",

        "CREATE INDEX IF NOT EXISTS node_updated_ix ON node(updated)",

        R"(CREATE TABLE IF NOT EXISTS "action_category" (
            "id" VARCHAR(32) NOT NULL,
            "version" INTEGER NOT NULL,
            "name" VARCHAR(255) NOT NULL,
            "data" BLOB NOT NULL,
            PRIMARY KEY("id")
        ))",

        R"(CREATE TABLE IF NOT EXISTS "action" (
            "id" VARCHAR(32) NOT NULL,
            "node" VARCHAR(32) NOT NULL,
            "origin" VARCHAR(32),
            "category" VARCHAR(32),
            "priority" INTEGER NULL,
            "dyn_importance" INT NULL,
            "dyn_urgency" INT NULL,
            "dyn_score" INT NULL,
            "status" INTEGER NOT NULL,
            "favorite" BOOLEAN NOT NULL,
            "name" VARCHAR(255) NULL DEFAULT 'Unnamed',
            "descr" TEXT,
            "created_date" DATETIME NOT NULL,
            "due_kind" INTEGER NOT NULL,
            "start_time" DATETIME,
            "due_by_time" DATETIME,
            "due_timezone" VARCHAR(32),
            "completed_time" DATETIME,
            "time_estimate" INTEGER,
            "difficulty" INTEGER,
            "repeat_kind" INTEGER,
            "repeat_unit" INTEGER,
            "repeat_when" INTEGER,
            "repeat_after" INTEGER,
            "kind" INTEGER NOT NULL,
            "version" INTEGER NOT NULL,
            "updated" INTEGER NOT NULL,
            "time_spent" INT NULL,
            "score" FLOAT NULL,
            "tags" TEXT NULL,
            "tags_hash" BLOB(32) NULL, -- Only stored locally, not sent via protobuf
            PRIMARY KEY("id")
        ))",

        "CREATE INDEX IF NOT EXISTS action_updated_ix ON action(updated)",
        "CREATE INDEX IF NOT EXISTS action_node_ix ON action(node, status)",
        "CREATE INDEX IF NOT EXISTS action_created_date_ix ON action(created_date, status)",
        "CREATE INDEX IF NOT EXISTS action_start_time_ix ON action(start_time, status)",
        "CREATE INDEX IF NOT EXISTS action_due_by_time_ix ON action(due_by_time, status)",
        "CREATE INDEX IF NOT EXISTS action_completed_time_ix ON action(completed_time, status)",

        R"(CREATE TABLE IF NOT EXISTS tag (
            name VARCHAR(32) NOT NULL,
            action VARCHAR(32) NOT NULL,
            PRIMARY KEY (action, name),
            FOREIGN KEY (action) REFERENCES action(id) ON DELETE CASCADE
        ))",

        "CREATE INDEX IF NOT EXISTS idx_tag_name_action ON tag(name, action)",

        R"(CREATE TABLE IF NOT EXISTS work_session (
            "id" VARCHAR(32) NOT NULL,
            "action" VARCHAR(32) NOT NULL,
            "state" INTEGER NOT NULL,
            "start_time" DATETIME,
            "end_time" DATETIME,
            "duration" INTEGER,
            "paused" INTEGER,
            "data" BLOB NOT NULL,
            "updated" INTEGER NOT NULL,
            PRIMARY KEY("id"),
            FOREIGN KEY(action) REFERENCES action(id) ON DELETE CASCADE)
        )",

        "CREATE INDEX IF NOT EXISTS work_session_action_state ON work_session(state, start_time)",
        "CREATE INDEX IF NOT EXISTS work_session_updated ON work_session(updated, state)",
        "CREATE INDEX IF NOT EXISTS work_session_action ON work_session(action, state)",
        "CREATE INDEX IF NOT EXISTS work_session_start_time ON work_session(start_time, state)",

        R"(CREATE TABLE IF NOT EXISTS time_block (
            "id" VARCHAR(32) NOT NULL,
            "start_time" DATETIME,
            "end_time" DATETIME,
            "kind" INTEGER,
            "data" BLOB NOT NULL,
            "updated" INTEGER NOT NULL,
            PRIMARY KEY("id"))
        )",

        "CREATE INDEX IF NOT EXISTS time_block_start_time ON time_block(start_time)",
        "CREATE INDEX IF NOT EXISTS time_block_end_time ON time_block(end_time)",
        "CREATE INDEX IF NOT EXISTS time_block_updated ON time_block(updated)",

        R"(CREATE TABLE IF NOT EXISTS time_block_actions (
            time_block VARCHAR(32) NOT NULL,
            action VARCHAR(32) NOT NULL,
            PRIMARY KEY(time_block, action),
            FOREIGN KEY(time_block) REFERENCES time_block(id) ON DELETE CASCADE,
            FOREIGN KEY(action) REFERENCES action(id) ON DELETE CASCADE)
        )",

        R"(CREATE TABLE IF NOT EXISTS "requests" (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            uuid VARCHAR(32) NOT NULL,
            tries INTEGER NOT NULL DEFAULT 0,
            time INTEGER NOT NULL,
            rpcid INTEGER NOT NULL,
            data BLOB NOT NULL)
        )",

        R"(CREATE TABLE IF NOT EXISTS "notification" (
            id INTEGER PRIMARY KEY,
            uuid VARCHAR(32) NOT NULL,
            time INTEGER NOT NULL,
            kind INTEGER NOT NULL,
            updated INTEGER NOT NULL,
            data BLOB NOT NULL)
        )",

    });

    static constexpr auto versions = to_array<span<const string_view>>({
        v1_bootstrap,
    });

    if (version == latest_version) {
        LOG_INFO << "The local database is at the current schema #" << version;
        return true;
    }

    LOG_INFO << "Will upgrade the database schema from #" << version
             << " to #" << latest_version;

    // Here we will run all SQL queries for upgrading from the specified version to the current version.
    auto relevant = std::ranges::drop_view(versions, version);
    for (auto& group : relevant) {
        for (std::string_view query : group) {
            QSqlQuery q{*db_};
            auto sql = QString::fromStdString(std::string{query});
            LOG_TRACE_N << "Executing: " << sql;
            if (!q.exec(sql)) {
                LOG_ERROR_N << "Failed to execute: \"" << sql
                            << "\", db=" << q.lastError().databaseText()
                            << ", driver=" << q.lastError().driverText();
                return false;
            }
        }
    }

    {
        QSqlQuery q{*db_};
        if (!q.exec(QString::asprintf("UPDATE nextapp SET version = %d, data_epoc = %d WHERE id=1",
                                      latest_version, nextapp::app_data_epoc))) {
            return false;
        }
    }

    return true;
}

uint DbStore::getDbVersion()
{
    QSqlQuery query{*db_};
    query.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name=:tableName");
    query.bindValue(":tableName", "nextapp");

    if (!query.exec()) {
        LOG_ERROR << "Cannot execute query:" << query.lastError().text();
    } else {
        if (query.next()) {
            LOG_DEBUG << "Table nextapp exists";

            query.clear();
            query.prepare("SELECT version FROM nextapp WHERE id=1");
            if (!query.exec()) {
                LOG_ERROR << "Cannot execute query: db=" << query.lastError().databaseText() << ", driver=" << query.lastError().driverText();
            } else {
                if (query.next()) {
                    bool ok = false;
                    const auto version = query.value(0).toInt(&ok);
                    if (ok) {
                        LOG_TRACE_N << "Existing database version: " << version;
                        return version;
                    } else {
                        LOG_WARN_N << "Failed to get version from the database";
                    }
                } else {
                    LOG_WARN_N << "No rows returned when querying about version";
                }
            }
        } else {
            LOG_DEBUG << "Table nextapp does not exist";
        }
    }

    return 0;
}


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

#include "DbStore.h"

#include "logging.h"

using namespace std;

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

void DbStore::start() {

    if constexpr (use_worker_thread) {
        LOG_TRACE_N << "Now running in worker thread";

        {
            // Wait for init() to be called
            lock_guard lock{mutex_};
        }
    }

    LOG_TRACE_N << "Initializing db store";

    db_ = new QSqlDatabase{QSqlDatabase::addDatabase("QSQLITE")};
    data_dir_ = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    LOG_INFO << "Using data dir: " << data_dir_;
    if (!QFile::exists(data_dir_)) {
        LOG_INFO << "Database directory does not exist, creating it";
        if (!QDir{}.mkpath(data_dir_)) {
            LOG_ERROR << "Failed to create database directory: " << data_dir_;
            emit error(GENERIC_ERROR);
            return;
        }
    }
    auto db_path = data_dir_ + "/db.sqlite";

    db_->setDatabaseName(db_path);
    if (!db_->open()) {
        LOG_ERROR << "Failed to open database: \"" << db_path << "\", error=" << db_->lastError().text();
        emit error(GENERIC_ERROR);
        return;
    }
    const auto version = getDbVersion();

    if (!updateSchema(version)) {
        // TODO: Do something!
    }

    connect(this, &DbStore::doQuery, this, &DbStore::queryImpl, Qt::QueuedConnection);
    emit initialized();
}

QCoro::Task<DbStore::rval_t> DbStore::query(const QString &sql, const QList<QVariant> *params)
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

void DbStore::queryImpl(const QString &sql, const QList<QVariant>* params, QPromise<rval_t> *promise)
{
    assert(promise);
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

    promise->start();
    promise->addResult(std::move(rval));
    promise->finish();
}

bool DbStore::updateSchema(uint version)
{
    static constexpr auto v1_bootstrap = to_array<string_view>({
        "CREATE TABLE nextapp (id INTEGER NOT NULL, version INTEGER NOT NULL) ",
        "INSERT INTO nextapp (id, version) values(1, 0)",

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
            "data" BLOB NOT NULL,
            PRIMARY KEY("uuid")
        ))",

        "CREATE INDEX IF NOT EXISTS node_updated_ix ON node(updated)",

        R"(CREATE TABLE IF NOT EXISTS "action_category" (
            "id" VARCHAR(32) NOT NULL,
            "version" INTEGER NOT NULL,
            "data" BLOB NOT NULL,
            PRIMARY KEY("id")
        ))",

        R"(CREATE TABLE IF NOT EXISTS "action" (
            "id" VARCHAR(32) NOT NULL,
            "node" VARCHAR(32) NOT NULL,
            "origin" VARCHAR(32),
            "category" VARCHAR(32),
            "priority" INTEGER NOT NULL,
            "status" INTEGER NOT NULL,
            "favorite" BOOLEAN NOT NULL,
            "name" VARCHAR(255) NOT NULL,
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
            "deleted" BOOLEAN NOT NULL,
            PRIMARY KEY("id")
        ))",

        "CREATE INDEX IF NOT EXISTS action_updated_ix ON action(updated)",
        "CREATE INDEX IF NOT EXISTS action_node_ix ON action(node, status)",
        "CREATE INDEX IF NOT EXISTS action_created_date_ix ON action(created_date, status)",
        "CREATE INDEX IF NOT EXISTS action_start_time_ix ON action(start_time, status)",
        "CREATE INDEX IF NOT EXISTS action_due_by_time_ix ON action(due_by_time, status)",
        "CREATE INDEX IF NOT EXISTS action_completed_time_ix ON action(completed_time, status)",

        R"(CREATE TABLE IF NOT EXISTS work_session (
            "id" VARCHAR(32) NOT NULL,
            "action" VARCHAR(32) NOT NULL,
            "state" INTEGER NOT NULL,
            "start_time" DATETIME,
            "end_time" DATETIME,
            "duration" INTEGER,
            "paused" INTEGER,
            "data" BLOB NOT NULL,
            "upated" INTEGER NOT NULL,
            PRIMARY KEY("id")
        )",
    });

    static constexpr auto versions = to_array<span<const string_view>>({
        v1_bootstrap,
    });

    LOG_INFO << "Will upgrade the database structure from version " << version
             << " to version " << latest_version;

    // Here we will run all SQL queries for upgrading from the specified version to the current version.
    auto relevant = ranges::drop_view(versions, version);
    for(string_view query : relevant | std::views::join) {
        QSqlQuery q{*db_};
        auto sql = QString::fromStdString(string{query});
        LOG_TRACE_N << "Executing: " << sql;
        if (!q.exec(sql)) {
            LOG_ERROR_N << "Failed to execute: \"" << sql << "\",  db=" << q.lastError().databaseText() << ", driver=" << q.lastError().driverText();
            return false;
        }
    }

    {
        QSqlQuery q{*db_};
        if (!q.exec(QString::asprintf("UPDATE nextapp SET version = %d WHERE id=1", latest_version))) {
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


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

#include "DbStore.h"

#include "logging.h"

using namespace std;

DbStore::DbStore(QObject *parent)
    : QObject{parent}, thread_{new QThread{this}}
{

    QObject::connect(thread_, &QThread::started, this, &DbStore::start);

    thread_->start();
    moveToThread(thread_);

    LOG_TRACE_N << "Now attached to worker thread";

    mutex_.lock();

    // connect(this, &DbStore::doInit, this, &DbStore::initImpl);
    // connect(this, &DbStore::doQuery, this, &DbStore::queryImpl);
}

void DbStore::start() {
    LOG_TRACE_N << "Now running in worker thread";

    {
        // Wait for init() to be called
        lock_guard lock{mutex_};
    }

    //connect(this, &DbStore::doInit, this, &DbStore::initImpl);

    LOG_TRACE_N << "Initing db store";

    db_ = new QSqlDatabase{QSqlDatabase::addDatabase("QSQLITE")};
    data_dir_ = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    LOG_INFO << "Using data dir: " << data_dir_;
    if (!QFile::exists(data_dir_)) {
        LOG_INFO << "Database directory does not exist, creating it";
        if (!QDir{}.mkpath(data_dir_)) {
            LOG_ERROR << "Failed to create database directory";
            emit error(GENERIC_ERROR);
            return;
        }
    }
    data_dir_ += "/db.sqlite";

    db_->setDatabaseName(data_dir_);
    if (!db_->open()) {
        LOG_ERROR << "Failed to open database: " << db_->lastError().text();
        emit error(GENERIC_ERROR);
        return;
    }
    const auto version = getDbVersion();

    updateSchema(version);

    connect(this, &DbStore::doQuery, this, &DbStore::queryImpl);
    emit initialized();
}

QCoro::Task<DbStore::rval_t> DbStore::query(const QString &sql, const QList<QVariant>& params)
{
    QPromise<rval_t> promise;
    emit doQuery(sql, params, promise);

    auto future = promise.future();
    co_return co_await qCoro(future).takeResult();
}

void DbStore::queryImpl(const QString &sql, const QList<QVariant>& params, QPromise<rval_t> &promise)
{
    LOG_TRACE_N << "Querying db: " << sql;
}

void DbStore::updateSchema(uint version)
{
    static constexpr auto v1_bootstrap = to_array<string_view>({
        "CREATE TABLE nextapp (id INTEGER NOT NULL, version INTEGER NOT NULL) ",
        "INSERT INTO nextapp (id, version) values(1, 0)"
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
            throw std::runtime_error{"Failed to upgrade database"};
        }
    }
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
                const auto version = query.value(0).toUInt();
                LOG_TRACE_N << "Existing database version: " << version;
                return version;
            }
        } else {
            LOG_DEBUG << "Table nextapp does not exist";
        }
    }

    return 0;
}

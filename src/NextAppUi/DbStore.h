#pragma once

#include "tl/expected.hpp"

#include <atomic>
#include <mutex>
#include <QObject>
#include <QVariant>
#include <QThread>
#include <QFuture>
#include <QPromise>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QFutureWatcher>
#include <QtConcurrent>
#include "qcorotask.h"

class DbStore : public QObject
{
    Q_OBJECT
public:
    // Useful for debugging weird behaviour and performance testing
    static auto constexpr use_worker_thread = true;

    static constexpr uint latest_version = 1;
    enum Error {
        OK,
        GENERIC_ERROR,
        QUERY_FAILED,
        EMPTY_RESULT
    };

    using rval_t = tl::expected<QList<QList<QVariant>>, Error>;
    using param_t = QList<QVariant>;

    explicit DbStore(QObject *parent = nullptr);
    ~DbStore();

    QCoro::Task<rval_t> query(const QString& sql, const param_t *params = {});

    template <typename T>
    QCoro::Task<tl::expected<T, Error>> queryOne(const QString& sql, const param_t *params = {}) {
        auto vals = co_await query(sql, params);
        if (vals) {
            auto& value = vals.value();
            if (value.empty()) {
                co_return tl::unexpected(EMPTY_RESULT);
            }
            co_return value.front().front().template value<T>();
        }
        co_return vals.error();
    }

    /*! Process a batch of data-records in the database-thread.
     *
     *  This is an optimization to avoid the overhead of sending
     *  each record individually to the database-thread.
     */
    template <typename T, typename P, typename D, typename I> QCoro::Task<bool> queryBatch(
        const QString& insertQurey,
        const QString& deleteQuery,
        const T& data,
        const P& getParams,
        const D& isDeleted,
        const I& getId) {

        // Wrap the operation in a lambda that will be executed in the DBs thread
        QFuture<bool> future = QtConcurrent::run([this, insertQurey, deleteQuery, data, getParams, isDeleted, getId]() -> bool {
            auto success = true;
            QSqlQuery query{*db_};
            query.prepare(insertQurey);

            for (const auto &row : data) {
                if (isDeleted(row)) {
                    QList<QVariant> params;
                    params << getId(row);
                    queryImpl_(deleteQuery, &params);
                    continue;
                };

                uint param_ix = 0;
                const auto params = getParams(row);
                for(const auto& param : params) {
                    query.bindValue(param_ix++, param);
                }

                if (!batchQueryImpl(query)) {
                    success = false;
                }
            }
            return success;
        });

        // Move the watcher to the existing thread and suspend until it completes
        QFutureWatcher<bool> watcher;
        watcher.setFuture(future);

        // Move the watcher to the correct thread
        watcher.moveToThread(thread_);

        // Wait for completion in a coroutine-safe way
        co_await watcher.future();
        co_return future.result();
    }

    QCoro::Task<bool> init();

    // Re-create the database. Deletes all the data.
    QCoro::Task<bool> clear();

    void queryImpl(const QString& sql, const param_t *params, QPromise<rval_t> *promise);

signals:
    // Emitted from the main thread to query the database.
    void doQuery(const QString& sql, const param_t *params, QPromise<rval_t> *promise);

    // Emitted from the worker thread when the database is ready.
    void initialized();

    // Emitted from the worker thread when an error occurs.
    void error(Error error);

private:
    void start();
    void createDbObject();
    void initImpl(QPromise<rval_t>& promise);
    bool updateSchema(uint version);
    uint getDbVersion();
    rval_t queryImpl_(const QString& sql, const param_t *params);
    bool batchQueryImpl(QSqlQuery& query);

    QThread* thread_{};
    std::unique_ptr<QSqlDatabase> db_;
    bool ready_{false};
    QString data_dir_;
    QString db_path_;
    bool clear_pending_{false};
    std::mutex mutex_;
};


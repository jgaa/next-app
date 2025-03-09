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
#include "qcorofuture.h"

class DbStore : public QObject
{
    Q_OBJECT

    template<typename T>
    void bindParams(QSqlQuery& query, const T& params) {
        uint param_ix = 0;

        // If the type is a QString, bind it directly
        if constexpr (std::is_same_v<T, QString>) {
            query.bindValue(param_ix++, params);
        }
        // Otherwise, assume it's a container and iterate through it
        else {
            for (const auto& param : params) {
                query.bindValue(param_ix++, param);
            }
        }
    }

public:
    // Useful for debugging weird behaviour and performance testing
    static auto constexpr use_worker_thread = true;

    static constexpr uint latest_version = 1;
    enum Error {
        OK,
        GENERIC_ERROR,
        QUERY_FAILED,
        EMPTY_RESULT,
        PREPARE_STATEMENT_FAILED
    };

    using rval_t = tl::expected<QList<QList<QVariant>>, Error>;
    using param_t = QList<QVariant>;

    struct DbRval {

    };

    explicit DbStore(QObject *parent = nullptr);
    ~DbStore();

    //[[deprecated]]
    QCoro::Task<rval_t> legacyQuery(const QString& sql, const param_t *params = {});

    template <typename T>
    QCoro::Task<tl::expected<T, Error>> queryOne(const QString& sql, const param_t *params = {}) {
        auto vals = co_await legacyQuery(sql, params);
        if (vals) {
            auto& value = vals.value();
            if (value.empty()) {
                co_return tl::unexpected(EMPTY_RESULT);
            }
            co_return value.front().front().template value<T>();
        }
        co_return vals.error();
    }

    struct QueryResult {
        QList<QList<QVariant>> rows;
        std::optional<uint> affected_rows;
        std::optional<uint> insert_id;
    };

    using qrval_t = tl::expected<QueryResult, Error>;

    template<typename... Args>
    QCoro::Task<qrval_t> query(const QString &sql, Args&&... args) {
        QList<QVariant> params = { QVariant(std::forward<Args>(args))... };
        co_return co_await runQueryInWorker(sql, params);
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

        auto promise = QSharedPointer<QPromise<bool>>::create();
        auto future = promise->future();

        // Wrap the operation in a lambda that will be executed in the DBs thread
        QMetaObject::invokeMethod(this, [&]() {
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

                bindParams(query, getParams(row));

                if (!batchQueryImpl(query)) {
                    success = false;
                }
            }
            promise->start();
            promise->addResult(success);
            promise->finish();
        });

        QFutureWatcher<bool> watcher;
        watcher.setFuture(future);
        co_await watcher.future();
        co_return future.result();
    }

    QCoro::Task<bool> init();
    void close();

    // Re-create the database. Deletes all the data.
    QCoro::Task<bool> clear();

    void queryImpl(const QString& sql, const param_t *params, QPromise<rval_t> *promise);

    /*! The database was created and initialized.
     */
    bool dbWasInitialized() const noexcept {
        return db_was_initialized_;
    }

    void clearDbInitializedFlag() {
        db_was_initialized_ = false;
    }

    std::filesystem::path dataDir() const noexcept {
        return data_dir_.toStdString();
    }

signals:
    // Emitted from the main thread to query the database.
    void doQuery(const QString& sql, const param_t *params, QPromise<rval_t> *promise);

    // Emitted from the worker thread when the database is ready.
    void initialized();

    // Emitted from the worker thread when an error occurs.
    void error(Error error);

private:
    QCoro::Task<qrval_t> runQueryInWorker(const QString &sql, const QList<QVariant> &params);

    qrval_t executeQuery(const QString &sql, const QList<QVariant> &params);

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
    bool db_was_initialized_{false};
    std::mutex mutex_;
};


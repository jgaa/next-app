#pragma once

#include "tl/expected.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <exception>
#include <QObject>
#include <QVariant>
#include <QThread>
#include <QFuture>
#include <QPromise>
#include <QSharedPointer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QFutureWatcher>
#include <QtConcurrent>
#include "qcorotask.h"
#include "qcorofuture.h"

#include "logging.h"
#include "nextapp.qpb.h"

#ifndef NEXTAPP_DB_COPY_BATCH_DATA
#define NEXTAPP_DB_COPY_BATCH_DATA 0
#endif

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

    static constexpr uint latest_version = 3;
    enum Error {
        OK,
        GENERIC_ERROR,
        QUERY_FAILED,
        EMPTY_RESULT,
        PREPARE_STATEMENT_FAILED
    };

    using rval_t = tl::expected<QList<QList<QVariant>>, Error>;
    using param_t = QList<QVariant>;
    using transaction_token_t = quint64;
    using transaction_result_t = tl::expected<transaction_token_t, Error>;

    explicit DbStore(const QString& db_file_name = QStringLiteral("db.sqlite"),
                     QObject *parent = nullptr);
    ~DbStore();

    //[[deprecated]]
    QCoro::Task<rval_t> legacyQuery(const QString& sql, const param_t *params = {});
    QCoro::Task<rval_t> legacyQueryInTransaction(transaction_token_t token, const QString& sql,
                                                 const param_t *params = {});

    template <typename T, typename... Args>
    QCoro::Task<tl::expected<T, Error>> queryOne(const QString& sql, Args&&... args) {
        auto res = co_await query(sql,std::forward<Args>(args)...);
        if (!res) {
            co_return tl::unexpected(QUERY_FAILED);
        }
        if (res.value().rows.empty() || res.value().rows.front().empty()) {
            co_return tl::unexpected(EMPTY_RESULT);
        }
        co_return res.value().rows.front().front().template value<T>();
    }

    template <typename T, typename... Args>
    QCoro::Task<tl::expected<T, Error>> queryOneInTransaction(transaction_token_t token,
                                                              const QString& sql,
                                                              Args&&... args) {
        auto res = co_await queryInTransaction(token, sql, std::forward<Args>(args)...);
        if (!res) {
            co_return tl::unexpected(QUERY_FAILED);
        }
        if (res.value().rows.empty() || res.value().rows.front().empty()) {
            co_return tl::unexpected(EMPTY_RESULT);
        }
        co_return res.value().rows.front().front().template value<T>();
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
        co_return co_await runQueryInWorker(sql, params, std::nullopt);
    }

    QCoro::Task<qrval_t> query(const QString &sql, QList<QVariant> params) {
        co_return co_await runQueryInWorker(sql, std::move(params), std::nullopt);
    }

    template<typename... Args>
    QCoro::Task<qrval_t> queryInTransaction(transaction_token_t token, const QString &sql, Args&&... args) {
        QList<QVariant> params = { QVariant(std::forward<Args>(args))... };
        co_return co_await runQueryInWorker(sql, params, token);
    }

    QCoro::Task<qrval_t> queryInTransaction(transaction_token_t token, const QString &sql, QList<QVariant> params) {
        co_return co_await runQueryInWorker(sql, std::move(params), token);
    }

    QCoro::Task<tl::expected<nextapp::pb::UserDataInfo, Error>> getDbDataInfo();

    /*! Process a batch of data-records in the database-thread.
     *
     *  This is an optimization to avoid the overhead of sending
     *  each record individually to the database-thread.
     */
    template <typename T, typename P, typename D, typename I, typename E = std::nullptr_t> QCoro::Task<bool> queryBatch(
        const QString& insertQurey,
        const QString& deleteQuery,
        const T& data,
        const P& getParams,
        const D& isDeleted,
        const I& getId,
        const E& perRowFn = nullptr) {
        co_return co_await queryBatchImpl(std::nullopt, insertQurey, deleteQuery, data,
                                          getParams, isDeleted, getId, perRowFn);
    }

    template <typename T, typename P, typename D, typename I, typename E = std::nullptr_t> QCoro::Task<bool> queryBatchInTransaction(
        transaction_token_t token,
        const QString& insertQurey,
        const QString& deleteQuery,
        const T& data,
        const P& getParams,
        const D& isDeleted,
        const I& getId,
        const E& perRowFn = nullptr) {
        co_return co_await queryBatchImpl(token, insertQurey, deleteQuery, data,
                                          getParams, isDeleted, getId, perRowFn);
    }

    QCoro::Task<transaction_result_t> beginExclusiveTransaction();
    QCoro::Task<bool> commitExclusiveTransaction(transaction_token_t token);
    QCoro::Task<bool> rollbackExclusiveTransaction(transaction_token_t token);

private:
    template <typename T, typename P, typename D, typename I, typename E = std::nullptr_t> QCoro::Task<bool> queryBatchImpl(
        const std::optional<transaction_token_t>& transaction_token,
        const QString& insertQurey,
        const QString& deleteQuery,
        const T& data,
        const P& getParams,
        const D& isDeleted,
        const I& getId,
        const E& perRowFn = nullptr) {
        auto promise = QSharedPointer<QPromise<bool>>::create();
        auto future = promise->future();
#if NEXTAPP_DB_COPY_BATCH_DATA
        auto data_copy = QSharedPointer<T>::create(data);
        const T *data_ptr = data_copy.data();
#else
        QSharedPointer<T> data_copy;
        const T *data_ptr = &data;
#endif
        const auto row_count = data_ptr->size();

        auto finish = [](const QSharedPointer<QPromise<bool>>& promise, bool success) {
            promise->start();
            promise->addResult(success);
            promise->finish();
        };

        LOG_TRACE_N << "Preparing database batch operation. rows=" << row_count
                    << ", copy_data=" << static_cast<bool>(NEXTAPP_DB_COPY_BATCH_DATA);

        co_await waitForTransactionAccess(transaction_token);

        // Wrap the operation in a lambda that will be executed in the DBs thread
        LOG_TRACE_N << "Queueing database batch operation. rows=" << row_count;
        const auto queued = QMetaObject::invokeMethod(this,
            [this, promise, finish, insertQurey, deleteQuery, data_copy, data_ptr,
             getParams, isDeleted, getId, perRowFn, row_count]() mutable {
                try {
                    LOG_TRACE_N << "Executing database batch operation. rows=" << row_count;
                    auto success = true;
                    QSqlQuery query{*db_};
                    if (!query.prepare(insertQurey)) {
                        LOG_ERROR_N << "Failed to prepare database batch query: " << insertQurey
                                    << ", db=" << query.lastError().databaseText()
                                    << ", driver=" << query.lastError().driverText();
                        finish(promise, false);
                        return;
                    }

                    for (const auto &row : *data_ptr) {
                        if (isDeleted(row)) {
                            QList<QVariant> params;
                            params << getId(row);
                            (void) queryImpl_(deleteQuery, &params);
                            continue;
                        };

                        bindParams(query, getParams(row));
                        if (batchQueryImpl(query)) {
                            if constexpr (!std::is_same_v<E, std::nullptr_t>) {
                                if (!perRowFn(row)) {
                                    success = false;
                                }
                            }
                        } else {
                            success = false;
                        }
                    }
                    if (success) {
                        LOG_TRACE_N << "Database batch operation completed. rows=" << row_count;
                    } else {
                        LOG_DEBUG_N << "Database batch operation completed with failures. rows=" << row_count;
                    }
                    finish(promise, success);
                } catch (const std::exception& e) {
                    LOG_ERROR_N << "Exception while executing database batch: " << e.what();
                    finish(promise, false);
                } catch (...) {
                    LOG_ERROR_N << "Unknown exception while executing database batch";
                    finish(promise, false);
                }
            },
            Qt::QueuedConnection);
        if (!queued) {
            LOG_ERROR_N << "Failed to queue database batch operation. rows=" << row_count;
            finish(promise, false);
        }

        QFutureWatcher<bool> watcher;
        watcher.setFuture(future);
        co_await watcher.future();
        co_return future.result();
    }

public:
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

    QString dbPath() const noexcept {
        return db_path_;
    }

    QSqlDatabase& db() {
        if (!db_) {
            throw std::runtime_error("Database not initialized");
        }
        return *db_;
    }

    QCoro::Task<void> closeAndDeleteDb();
    QCoro::Task<bool> replaceWithDatabaseFile(const QString& source_db_path);



signals:
    // Emitted from the main thread to query the database.
    void doQuery(const QString& sql, const param_t *params, QPromise<rval_t> *promise);

    // Emitted from the worker thread when the database is ready.
    void initialized();

    // Emitted from the worker thread when an error occurs.
    void error(Error error);

private:
    QCoro::Task<void> waitForTransactionAccess(const std::optional<transaction_token_t>& transaction_token);
    QCoro::Task<qrval_t> runQueryInWorker(const QString &sql, const QList<QVariant> &params,
                                          const std::optional<transaction_token_t>& transaction_token);
    tl::expected<QString, DbStore::Error> getDbDataHash();

    template <typename T, typename F>
    QCoro::Task<T> runInWorkerThread(F&& func) {
        if (QThread::currentThread() == thread_) {
            // If already in the correct thread, run immediately
            co_return std::forward<F>(func)();
        }

        auto promise = QSharedPointer<QPromise<T>>::create();
        auto future = promise->future();

        QMetaObject::invokeMethod(this, [promise, func = std::forward<F>(func)]() mutable {
            try {
                auto result = func();
                promise->start();
                promise->addResult(result);
            } catch (...) {
                promise->setException(std::current_exception());
            }
            promise->finish();
        }, Qt::QueuedConnection);

        QFutureWatcher<T> watcher;
        watcher.setFuture(future);
        auto r = co_await watcher.future();
        co_return future.result();
    }

    qrval_t executeQuery(const QString &sql, const QList<QVariant> &params);

    void start();
    void createDbObject();
    void destroyDbObject();
    void initImpl(QPromise<rval_t>& promise);
    bool updateSchema(uint version);
    uint getDbVersion();
    rval_t queryImpl_(const QString& sql, const param_t *params);
    bool batchQueryImpl(QSqlQuery& query);
    bool clear_();

    QThread* thread_{};
    std::unique_ptr<QSqlDatabase> db_;
    bool ready_{false};
    QString data_dir_;
    QString db_path_;
    QString db_file_name_;
    QString connection_name_;
    bool clear_pending_{false};
    bool db_was_initialized_{false};
    std::mutex mutex_;
    std::mutex transaction_mutex_;
    std::optional<transaction_token_t> active_transaction_token_;
    transaction_token_t next_transaction_token_{1};
};

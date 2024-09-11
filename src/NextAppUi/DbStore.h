#pragma once

#include "tl/expected.hpp"

#include <mutex>
#include <QObject>
#include <QVariant>
#include <QThread>
#include <QFuture>
#include <QPromise>
#include <QSqlDatabase>
#include "qcorotask.h"


class DbStore : public QObject
{
    Q_OBJECT
public:
    static constexpr uint latest_version = 1;
    enum Error {
        OK,
        GENERIC_ERROR,
        QUERY_FAILED
    };

    using rval_t = tl::expected<QList<QList<QVariant>>, Error>;

    explicit DbStore(QObject *parent = nullptr);

    QCoro::Task<rval_t> query(const QString& sql, const QList<QVariant>& params = {});

    void init() {
        // Once called, the database will be initialized in the worker thread.
        mutex_.unlock();
    }

signals:
    // Emitted from the main thread to query the database.
    void doQuery(const QString& sql, const QList<QVariant>& params, QPromise<rval_t>& promise);

    // Emitted from the worker thread when the database is ready.
    void initialized();

    // Emitted from the worker thread when an error occurs.
    void error(Error error);

private:
    void start();
    void initImpl(QPromise<rval_t>& promise);
    void queryImpl(const QString& sql, const QList<QVariant>& params, QPromise<rval_t>& promise);
    bool updateSchema(uint version);
    uint getDbVersion();

    QThread* thread_{};
    QSqlDatabase *db_{};
    bool ready_{false};
    QString data_dir_;
    std::mutex mutex_;
};

#pragma once

#include "qcorotask.h"
#include "nextapp.qpb.h"

#include "NextAppCore.h"
#include "RuntimeServices.h"
#include "ServerComm.h"
#include "logging.h"
#include "format_wrapper.h"

/*! Server sync helper
 *
 */
template<typename T, typename B>
class ServerSynchedCahce
{
public:
    enum class State {
        LOCAL,
        SYNCHING,
        LOADING,
        APPLYING_UPDATES,
        VALID,
        ERROR
    };

    ServerSynchedCahce() = default;
    explicit ServerSynchedCahce(RuntimeServices& runtime)
        : runtime_{&runtime} {}
    virtual ~ServerSynchedCahce() = default;

    // Interface
    virtual QCoro::Task<void> pocessUpdate(const std::shared_ptr<nextapp::pb::Update> update) = 0;
    virtual QCoro::Task<bool> save(const QProtobufMessage& item) = 0;
    virtual QCoro::Task<bool> saveBatch(const QList<T>& items) {co_return false;}
    virtual bool haveBatch() const noexcept { return false; }
    virtual QCoro::Task<bool> finalizeSyncPersistence() { co_return true; }
    virtual QCoro::Task<bool> loadFromCache() = 0;
    virtual bool hasItems(const nextapp::pb::Status& status) const noexcept = 0;
    virtual QList<T> getItems(const nextapp::pb::Status& status) = 0;
    virtual bool isRelevant(const nextapp::pb::Update& update) const noexcept = 0;
    virtual std::string_view itemName() const noexcept = 0;
    virtual std::shared_ptr<GrpcIncomingStream> openServerStream(nextapp::pb::GetNewReq req) = 0;
    virtual void clear() = 0;

    void onUpdate(const std::shared_ptr<nextapp::pb::Update>& update) {
        assert(update);
        if (isRelevant(*update)) {
            if (valid()) {
                pocessUpdate(update);
                return;
            }

            pending_updates_.emplace_back(update);
        }
    }

    virtual QCoro::Task<bool> synch(bool isFullSync = false) {
        full_sync_ = isFullSync;
        if (state() > State::LOCAL && state() < State::VALID) {
            // Already synching
            co_return false;
        }

        setState(State::SYNCHING);
        if (load_after_sync_) {
            clear();
        }
        const bool use_transaction = !isFullSync;
        auto& db = syncDb();

        if (use_transaction) {
            auto started = co_await db.beginExclusiveTransaction();
            if (!started) {
                LOG_ERROR_N << "Failed to start sync transaction for " << itemName();
                setState(State::ERROR);
                co_return false;
            }
            sync_transaction_token_ = started.value();
        }

        bool persisted = false;
        if (co_await synchFromServer() && co_await finalizeSyncPersistence()) {
            if (use_transaction) {
                persisted = co_await db.commitExclusiveTransaction(*sync_transaction_token_);
                sync_transaction_token_.reset();
            } else {
                persisted = true;
            }
        } else if (use_transaction) {
            (void) co_await db.rollbackExclusiveTransaction(*sync_transaction_token_);
            sync_transaction_token_.reset();
        }

        if (persisted) {
            if (!load_after_sync_) {
                setState(State::LOCAL);
                co_return true;
            }

            if (co_await loadLocally(false)) {
                co_return true;
            }
        }

        setState(State::ERROR);
        co_return false;
    }

    virtual QCoro::Task<bool> loadLocally(bool doClear = true) {
        setState(State::LOADING);
        if (doClear) {
            clear();
        }
        if (co_await loadFromCache()) {
            setState(State::APPLYING_UPDATES);

            do {
                auto pending = std::move(pending_updates_);
                for (const auto& update : pending) {
                    co_await pocessUpdate(update);
                }

            } while(!pending_updates_.empty());

            setState(State::VALID);
            co_return true;
        }

        co_return false;
    }

    virtual QCoro::Task<qlonglong> getLastUpdate()
    {
        auto& db = syncDb();
        static const QString query = QString::fromStdString(::nextapp::format("SELECT MAX(updated) FROM {}", itemName()));
        const auto last_updated = sync_transaction_token_
            ? co_await db.template queryOneInTransaction<qlonglong>(*sync_transaction_token_, query)
            : co_await db.template queryOne<qlonglong>(query);
        if (last_updated) {
            co_return last_updated.value();
        }
        co_return 0;
    }

    virtual QCoro::Task<qulonglong> getLastUpdatedId()
    {
        auto& db = syncDb();
        static const QString query = QString::fromStdString(::nextapp::format("SELECT MAX(updated_id) FROM {}", itemName()));
        const auto last_updated_id = sync_transaction_token_
            ? co_await db.template queryOneInTransaction<qulonglong>(*sync_transaction_token_, query)
            : co_await db.template queryOne<qulonglong>(query);
        if (last_updated_id) {
            co_return last_updated_id.value();
        }
        co_return 0;
    }

    virtual QCoro::Task<bool> synchFromServer()
    {
        nextapp::pb::GetNewReq req;
        if (runtime().serverComm().shouldUseUpdatedIdSync()) {
            req.setProtocolVersion(nextapp::pb::ProtopcolVersionGadget::ProtopcolVersion::USE_UPDATED_ID);
            req.setSince(static_cast<qlonglong>(co_await getLastUpdatedId()));
        } else if (auto last_updated = co_await getLastUpdate(); last_updated > 0) {
            req.setSince(last_updated);
        }

        auto stream = openServerStream(req);

        LOG_TRACE_N << "Entering message-loop";
        while (auto update = co_await stream->template next<nextapp::pb::Status>()) {

            LOG_TRACE_N << "next returned something";
            if (update.has_value()) {
                auto &u = update.value();
                LOG_TRACE_N << "next has value";
                if (u.error() == nextapp::pb::ErrorGadget::Error::OK) {
                    LOG_TRACE_N << "Got OK from server: " << u.message();
                    if (hasItems(u)) {
                        const auto& items = getItems(u);
                        LOG_TRACE_N << "Got " << itemName() << " from server. Count=" << items.size();
                        if (haveBatch()) {
                            if (!co_await saveBatch(items)) {
                                LOG_ERROR_N << "Failed to persist " << itemName()
                                            << " batch locally. Aborting sync.";
                                co_return false;
                            }
                        } else {
                            for(const auto& item : items) {
                                if (!co_await save(item)) {
                                    LOG_ERROR_N << "Failed to persist " << itemName()
                                                << " item locally. Aborting sync.";
                                    co_return false;
                                }
                            }
                        }
                    }
                } else {
                    LOG_DEBUG_N << "Got error from server. err=" << u.error()
                    << " msg=" << u.message();
                    co_return false;
                }
            } else {
                LOG_TRACE_N << "Stream returned nothing. Done. err="
                            << update.error().err_code();
                co_return false;
            }
        }


        co_return true;
    }

    void setState(State state) noexcept {
        if (state_ != state) {
            LOG_TRACE_N << "State changed from " << static_cast<int>(state_)
            << " to " << static_cast<int>(state);
            state_ = state;
            emit dynamic_cast<B&>(*this).stateChanged();
        }
    }

    State state() const noexcept {
        return state_;
    }

    bool valid() const noexcept {
        return state_ == State::VALID;
    }

    virtual std::string_view tableName() const noexcept {
        return itemName();
    }

    bool isFullSync() const noexcept {
        return full_sync_;
    }

protected:
    RuntimeServices& runtime() const noexcept {
        assert(runtime_);
        return *runtime_;
    }

    DbStore& syncDb() const noexcept {
        return sync_db_override_ ? *sync_db_override_ : runtime().db();
    }

    bool shouldLoadAfterSync() const noexcept {
        return load_after_sync_;
    }

    std::optional<DbStore::transaction_token_t> syncTransactionToken() const noexcept {
        return sync_transaction_token_;
    }

public:
    void setSyncDbOverride(DbStore* db) noexcept {
        sync_db_override_ = db;
    }

    void setRuntimeServices(RuntimeServices& runtime) noexcept {
        runtime_ = &runtime;
    }

    void setLoadAfterSync(bool load_after_sync) noexcept {
        load_after_sync_ = load_after_sync;
    }

private:
    State state_;
    std::vector<std::shared_ptr<nextapp::pb::Update>> pending_updates_;
    bool full_sync_{};
    bool load_after_sync_{true};
    RuntimeServices* runtime_{};
    DbStore* sync_db_override_{};
    std::optional<DbStore::transaction_token_t> sync_transaction_token_;

};

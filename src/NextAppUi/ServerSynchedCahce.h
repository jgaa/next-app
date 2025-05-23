#pragma once

#include "qcorotask.h"
#include "nextapp.qpb.h"

#include "NextAppCore.h"
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
    virtual ~ServerSynchedCahce() = default;

    // Interface
    virtual QCoro::Task<void> pocessUpdate(const std::shared_ptr<nextapp::pb::Update> update) = 0;
    virtual QCoro::Task<bool> save(const QProtobufMessage& item) = 0;
    virtual QCoro::Task<bool> saveBatch(const QList<T>& items) {co_return false;}
    virtual bool haveBatch() const noexcept { return false; }
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
        clear();
        if (co_await synchFromServer() && co_await loadLocally(false)) {
            co_return true;
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
        auto& db = NextAppCore::instance()->db();
        static const QString query = QString::fromStdString(::nextapp::format("SELECT MAX(updated) FROM {}", itemName()));
        const auto last_updated = co_await db.queryOne<qlonglong>(query);
        if (last_updated) {
            co_return last_updated.value();
        }
        co_return 0;
    }

    QCoro::Task<bool> synchFromServer()
    {
        nextapp::pb::GetNewReq req;
        if (auto last_updated = co_await getLastUpdate(); last_updated > 0) {
            req.setSince(last_updated);
        }

        auto stream = openServerStream(req);

        bool looks_ok = false;
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
                            co_await saveBatch(items);
                        } else {
                            for(const auto& item : items) {
                                co_await save(item);
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

private:
    State state_;
    std::vector<std::shared_ptr<nextapp::pb::Update>> pending_updates_;
    bool full_sync_{};

};

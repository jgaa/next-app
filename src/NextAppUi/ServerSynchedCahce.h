#pragma once

#include "qcorotask.h"
#include "nextapp.qpb.h"

#include "NextAppCore.h"
#include "logging.h"

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

    virtual QCoro::Task<bool> synch() {
        if (state() > State::LOCAL && state() < State::ERROR) {
            // Already synching
            co_return false;
        }

        setState(State::SYNCHING);
        clear();
        if (co_await synchFromServer()) {
            setState(State::LOADING);
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

            co_return true;
        } else {
            setState(State::ERROR);
        }

        co_return false;
    }

    QCoro::Task<bool> synchFromServer()
    {
        static const QString version_query = QString::fromStdString(format("SELECT MAX(updated) FROM {}", itemName()));
        // Get a stream of updates from servercomm.
        auto& db = NextAppCore::instance()->db();
        const auto last_updated = co_await db.queryOne<qlonglong>(version_query);

        nextapp::pb::GetNewReq req;
        if (last_updated) {
            req.setSince(last_updated.value());
        }

        auto stream = openServerStream(req);

        bool looks_ok = false;
        LOG_TRACE_N << "Entering message-loop";
        while (auto update = co_await stream->template next<nextapp::pb::Status>()) {

            LOG_TRACE_N << "next returned something";
            if (update.has_value()) {
                auto &u = update.value();
                LOG_TRACE_N << "next has value";
                if (u.error() == nextapp::pb::ErrorGadget::OK) {
                    LOG_TRACE_N << "Got OK from server: " << u.message();
                    //if (u.hasNodes()) {
                    if (hasItems(u)) {
                        const auto& items = getItems(u);
                        LOG_TRACE_N << "Got " << itemName() << " from server. Count=" << items.size();
                        for(const auto& item : items) {
                            co_await save(item);
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

private:
    State state_;
    std::vector<std::shared_ptr<nextapp::pb::Update>> pending_updates_;

};

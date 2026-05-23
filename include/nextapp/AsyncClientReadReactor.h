#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>

#include <boost/asio.hpp>
#include <grpcpp/grpcpp.h>

#include "nextapp/logging.h"

namespace nextapp {

// Reactor for consuming outbound server-streaming RPCs one message at a time.
template <typename ReqT, typename RespT>
class AsyncClientReadReactor
    : public std::enable_shared_from_this<AsyncClientReadReactor<ReqT, RespT>>
    , public ::grpc::ClientReadReactor<RespT>
{
public:
    using starter_t = std::function<void(::grpc::ClientContext&, const ReqT*, ::grpc::ClientReadReactor<RespT>*)>;
    using on_connected_fn_t = std::function<void(bool ok)>;

    AsyncClientReadReactor(boost::asio::io_context& asio,
                           ReqT request,
                           starter_t starter,
                           on_connected_fn_t on_connected_fn = {})
        : asio_{asio}, timer_{asio}, request_{std::move(request)}
        , starter_{std::move(starter)}, on_connected_fn_{std::move(on_connected_fn)}
    {
    }

    void start()
    {
        self_ = this->shared_from_this();
        starter_(ctx_, &request_, this);

        this->StartRead(&temp_msg_);
        this->StartCall();
    }

    void cancel()
    {
        ctx_.TryCancel();
    }

    std::optional<::grpc::Status> status() const
    {
        std::scoped_lock lock{mutex_};
        return status_;
    }

    void OnReadInitialMetadataDone(bool ok) override
    {
        LOG_DEBUG_N << "Initial metadata read completed with status " << ok;
        if (on_connected_fn_) {
            std::scoped_lock lock{mutex_};
            on_connected_fn_(ok);
        }
        timer_.cancel();
    }

    boost::asio::awaitable<::grpc::Status> waitForDone()
    {
        for (;;) {
            {
                std::scoped_lock lock{mutex_};
                if (status_) {
                    co_return *status_;
                }
            }

            try {
                timer_.expires_at(std::chrono::steady_clock::time_point::max());
                co_await timer_.async_wait(boost::asio::use_awaitable);
            } catch (const boost::system::system_error& e) {
                if (e.code() != boost::asio::error::operation_aborted) {
                    throw;
                }
            }
        }
    }

    boost::asio::awaitable<std::optional<RespT>> read()
    {
        for (;;) {
            std::unique_lock lock{mutex_};
            if (buffer_) {
                RespT msg = std::move(*buffer_);
                buffer_.reset();
                this->StartRead(&temp_msg_);
                co_return msg;
            }
            if (done_) {
                co_return std::nullopt;
            }
            lock.unlock();

            try {
                co_await timer_.async_wait(boost::asio::use_awaitable);
            } catch (const boost::system::system_error& e) {
                if (e.code() != boost::asio::error::operation_aborted) {
                    throw;
                }
            }
        }
    }

    void OnReadDone(bool ok) override
    {
        {
            std::scoped_lock lock{mutex_};
            if (ok) {
                buffer_.emplace(std::move(temp_msg_));
            } else {
                done_ = true;
            }
        }
        timer_.cancel();
    }

    void OnDone(const ::grpc::Status& status) override
    {
        {
            std::scoped_lock lock{mutex_};
            done_ = true;
            status_ = status;
            if (on_connected_fn_ && !status.ok()) {
                on_connected_fn_(false);
            }
        }
        timer_.cancel();
        self_.reset();
    }

private:
    boost::asio::io_context& asio_;
    boost::asio::steady_timer timer_;
    ::grpc::ClientContext ctx_;
    ReqT request_;
    starter_t starter_;
    mutable std::mutex mutex_;
    std::optional<RespT> buffer_;
    std::optional<::grpc::Status> status_;
    bool done_{false};
    RespT temp_msg_;
    on_connected_fn_t on_connected_fn_;
    std::shared_ptr<AsyncClientReadReactor> self_;
};

} // namespace nextapp

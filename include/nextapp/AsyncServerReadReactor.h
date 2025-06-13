#pragma once

#include <memory>
#include <mutex>
#include <optional>

#include <boost/asio.hpp>
#include <grpcpp/grpcpp.h>

#include "nextapp/logging.h"
#include "nextapp/util.h"
//#include "nextapp/async_server_write_reactor.h"  // your existing write reactor

namespace nextapp {

// Reactor for pulling client-stream messages (one-at-a-time)
template <ProtoMessage ReqT, typename A, typename G>
class AsyncServerReadReactor
    : public std::enable_shared_from_this<AsyncServerReadReactor<ReqT, A, G>>
    , public ::grpc::ServerReadReactor<ReqT>
{
public:
    enum class IoState : uint8_t  {
        READY,
        WAITING_ON_READ,
        DONE_PENDING,
        WAITING_ON_DONE,
        DONE
    };

    AsyncServerReadReactor(::grpc::CallbackServerContext* ctx, A& asio, G& grpc)
        : context_(ctx), asio_(asio), timer_(asio), grpc_(grpc)
    {
        LOG_TRACE_N << "Read‐stream " << uuid_ << " created";
        this->StartRead(&temp_msg_);
    }

    ~AsyncServerReadReactor() {
        LOG_TRACE_N << "Read‐stream " << uuid_ << " destroyed";
    }

    // Awaitable: returns next message, or nullopt when client closed
    boost::asio::awaitable<std::optional<ReqT>> read()
    {
        for (;;) {
            std::unique_lock lock{mutex_};
            if (buffer_) {
                // consume and trigger next gRPC read
                ReqT msg = std::move(*buffer_);
                buffer_.reset();
                this->StartRead(&temp_msg_);
                co_return msg;
            }
            if (done_) {
                co_return std::nullopt;
            }
            pending_exec_ = co_await boost::asio::this_coro::executor;
            lock.unlock();

            try {
                co_await timer_.async_wait(
                    boost::asio::bind_executor(*pending_exec_, boost::asio::use_awaitable));
            } catch (const boost::system::system_error& e) {
                if (e.code() != boost::asio::error::operation_aborted) {
                    throw;
                }
            }
        }
    }

    // Called by user to begin the lifecycle
    void start() { self_ = this->shared_from_this(); }

    // gRPC callback: end of RPC
    void OnDone() override {
        {
            std::lock_guard lock{mutex_};
            done_ = true;
        }
        timer_.cancel();
        self_.reset();
    }

    // gRPC callback: read completed
    void OnReadDone(bool ok) override {
        {
            std::lock_guard lock{mutex_};
            if (ok) buffer_.emplace(std::move(temp_msg_));
            else      done_ = true;
        }
        timer_.cancel();
    }

private:
    IoState state_{IoState::READY};
    ::grpc::CallbackServerContext*             context_;
    A&                                         asio_;
    G&                                         grpc_;
    boost::asio::steady_timer                  timer_;
    std::recursive_mutex                       mutex_;

    std::optional<boost::asio::any_io_executor> pending_exec_;
    std::optional<ReqT>                        buffer_;
    bool                                       done_{false};
    ReqT                                       temp_msg_;

    std::shared_ptr<AsyncServerReadReactor>    self_;
    const boost::uuids::uuid                   uuid_{ newUuid() };
};

// Convenience factory
template <ProtoMessage ReqT, typename A, typename G>
auto make_client_streamer(::grpc::CallbackServerContext* ctx, A& asio, G& grpc) {
    return make_shared<AsyncServerReadReactor<ReqT,A,G>>(ctx, asio, grpc);
}

} // namespace nextapp

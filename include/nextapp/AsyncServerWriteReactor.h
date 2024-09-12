#pragma once

#include <atomic>
#include <memory>
#include <climits>
#include <mutex>

#include <boost/asio.hpp>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
//#include <google/protobuf/message.h>

#include "nextapp/util.h"
#include "nextapp/logging.h"

namespace nextapp {

template <ProtoMessage reqT, typename A, typename G>
class AsyncServerWriteReactor
    : public std::enable_shared_from_this<AsyncServerWriteReactor<reqT, A, G>>
    , public ::grpc::ServerWriteReactor<reqT>
{
public:
    enum class IoState : uint8_t  {
        READY,
        WAITING_ON_WRITE,
        DONE_PENDING,
        WAITING_ON_DONE,
        DONE
    };

    AsyncServerWriteReactor(::grpc::CallbackServerContext *context, A& asio, G& grpc)
        : context_{context}, asio_{asio}, timer_{asio_}, grpc_{grpc} {
        LOG_TRACE_N << "Stream " << uuid_ << " is created.";
    }

    ~AsyncServerWriteReactor() {
        LOG_TRACE_N << "Stream " << uuid_ << " is gone..."
                    << " state=" << static_cast<int>(state_);
    }

    /*! Asyncroneously write a message to the client
     *
     *  The function returns when the message is accepted for delivery by gRPC.
     *  The actual delivery may happen at a later time.
     *
     *  NB: this function assumes a one producer/one consumer model.
     *      The function can not be called again before it has completed.
     */
    template <typename CompletionToken>
    auto sendMessage(reqT && message, CompletionToken&& token) {
        return boost::asio::async_compose<CompletionToken, void(boost::system::error_code)>(
            [this, message=std::move(message)](auto& self) mutable {
            {
                // Happy path
                std::scoped_lock lock{mutex_};
                if (state_ == IoState::READY) {
                    write(std::move(message));
                    self.complete({});
                    return;
                }
            }

            boost::asio::co_spawn(asio_,
                                  [this,
                                   // Avoid a race condition where `this` is destroyed before the co-routine is
                                   // finished and still access its mutex or state.
                                    instance=this->shared_from_this(),
                                    message=std::move(message), self=std::move(self)]
                                  () mutable -> boost::asio::awaitable<void> {
                for(;;) {
                    {
                        std::scoped_lock lock{mutex_};
                        if (state_ == IoState::READY) {
                            LOG_DEBUG << "sendMessage/co_spawn/before write " << grpc_.toJsonForLog(message);
                            write(std::move(message));
                            self.complete({});
                            co_return;
                        } else if (state_ >= IoState::DONE_PENDING) {
                            self.complete(boost::system::errc::make_error_code(boost::system::errc::operation_canceled));
                            co_return;
                        }

                        LOG_TRACE << "sendMessage() called while state is not READY. Going to sleep. "
                                  << "state = " << static_cast<int>(state_);

                        // Use a relatively short wait for now in case we have a race-conditions
                        // that leads to nobody cancelling the timer.
                        timer_.expires_from_now(boost::posix_time::seconds(5));
                    }

                    try {
                        co_await timer_.async_wait(boost::asio::use_awaitable);
                    } catch (const boost::system::system_error& e) {
                        if (e.code() != boost::asio::error::operation_aborted) {
                            LOG_WARN << "Caught exception while waiting for write() to be ready: "
                                     << e.what();
                            self.complete(e.code());
                            co_return;
                        }
                    }
                } // retry loop

            }, boost::asio::detached);
        }, token);
    }

    void close(::grpc::Status status) {
        return close_(std::move(status));
    }

    bool isOpen() {
        std::scoped_lock lock{mutex_};
        return state_ != this->State::DONE;
    }

    void start() {
        self_ = this->shared_from_this();
    }

    /*! Callback event when the RPC is completed */
    void OnDone() override {
        {
            std::lock_guard lock{mutex_};
            if (state_ == IoState::WAITING_ON_DONE) {
                LOG_WARN << "OnDone() called while state is not WAITING_ON_DONE. state="
                         << static_cast<int>(state_);
            }

            setState(IoState::DONE);
        }

        // Release all awaiting coroutines
        boost::system::error_code ec;
        timer_.cancel(ec);
        self_.reset();
    }

    /*! Callback event when a write operation is complete */
    void OnWriteDone(bool ok) override {
        LOG_TRACE_N << "OnWriteDone() called with " << (ok ? "OK" : "NOT OK") << " on stream " << uuid_ << " state=" << static_cast<int>(state_);

        if (!ok) [[unlikely]] {
            LOG_WARN << "The write-operation failed.";

            // We still need to call Finish or the request will remain stuck!
            close_({::grpc::StatusCode::CANCELLED, "Write failed"}, true);
        } else {
            std::scoped_lock lock{mutex_};

            switch(state_) {
                case IoState::WAITING_ON_WRITE:
                    setState(IoState::READY);
                    break;
                case IoState::DONE_PENDING:
                    assert(finish_status_.has_value());
                    LOG_TRACE_N << "Closing stream " << uuid_ << " (was pending)  with status: " << finish_status_->error_message()
                                << " code=" << finish_status_->error_code();
                    this->Finish(*finish_status_);
                    setState(IoState::WAITING_ON_DONE);
                    break;
                default:
                    LOG_WARN_N << "Unexpected state in OnWriteDone(): " << static_cast<int>(state_);
            }
        }

        // Release any awaiting coroutine
        boost::system::error_code ec;
        timer_.cancel(ec);
    }

private:
    // Must be called with mutex_ locked
    void write(reqT && message) {
        assert(state_ == IoState::READY);
        current_message_ = std::move(message);
        LOG_TRACE_N << "Writing message to stream " << uuid_
                    << ' ' << grpc_.toJsonForLog(current_message_);

        // Seems like StartWrite may call into our OnWriteDone() immediately, causing a deadlock with a normal mutex.
        this->StartWrite(&current_message_);
        setState(IoState::WAITING_ON_WRITE);
    }

    void close_(::grpc::Status status, bool force = false) {
        finish_status_ = std::move(status);
        std::scoped_lock lock{mutex_};

        if (force) {
            goto forced;
        }

        if (state_ == IoState::DONE) {
            return;
        }

        if (state_ == IoState::READY) {
forced:
            assert(finish_status_.has_value());
            LOG_TRACE_N << "Closing stream " << uuid_ << " with status: " << finish_status_->error_message()
                << " code=" << finish_status_->error_code();
            this->Finish(*finish_status_);
            setState(IoState::WAITING_ON_DONE);
            return;
        }

        setState(IoState::DONE_PENDING);
        boost::system::error_code ec;
        timer_.cancel(ec);
    }

    void setState(IoState state) {
        LOG_TRACE_N << "On stream " << uuid_ << " state changed from "
                    << static_cast<int>(state_) << " to " << static_cast<int>(state);
        state_ = state;
    }


    IoState state_{IoState::READY};
    ::grpc::CallbackServerContext *context_;
    A& asio_;
    G& grpc_;
    reqT current_message_;
    boost::asio::deadline_timer timer_;
    std::optional<::grpc::Status> finish_status_;
    std::shared_ptr<AsyncServerWriteReactor> self_;
    std::recursive_mutex mutex_; // TODO: Clean up locking!
    const boost::uuids::uuid uuid_{newUuid()};
};

template <ProtoMessage T, typename A, typename G>
auto make_write_dstream(::grpc::CallbackServerContext *context, A &asio, G& grpc) {
    return make_shared<AsyncServerWriteReactor<T, A, G>>(context, asio, grpc);
}


} // namespace nextapp

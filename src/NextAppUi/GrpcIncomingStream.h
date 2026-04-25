#pragma once

#include <queue>

#include "tl/expected.hpp"

#include <QObject>
#include <QElapsedTimer>
#include <QGrpcServerStream>
#include "qcorotask.h"
#include "qcorosignal.h"
#include "logging.h"
#include "nextapp.qpb.h"
#include "util.h"

template <typename T>
concept ProtoMessage = std::is_base_of_v<QProtobufMessage, T>;

/*! \brief A coroutine-compatible class for reading incoming gRPC messages from a stream.
 *
 * This class is designed to be used in a coroutine, where the next() method is called
 * to read the next message from the stream. The method will return a tl::expected
 * with either the message or an error code.
 *
 * The class will emit a signal haveMessage() when a new message is available.
 *
 * Note that with the current implementation of QT grpc streams, we have to
 * queue the incoming messages in memory as soon as they arrive. Unlike
 * the original gRPC streams, QT streams don't wait for us to call `read<>()` before
 * they discard the message. We have to read it as soon as we get a message signal.
 *
 * The USE_GRPC_STREAMING_FLOW_CONTROL macro allows us to apply the original gRPC streaming
 * assumptions, in case QT fix this.
 */


class GrpcIncomingStream : public QObject
{
    Q_OBJECT
public:
    enum class State {
        IDLE,
        STREAMING,
        DONE
    };

    struct Error {
        enum class Code {
            OK,
            READ_FAILED,
            CLOSED
        };

        Code code{Code::OK};

        bool isClosed() const noexcept {
            return code == Code::CLOSED;
        }

        bool readFailed() const noexcept {
            return code == Code::READ_FAILED;
        }

        bool ok() const noexcept {
            return code == Code::OK;
        }

        int err_code() const noexcept {
            return static_cast<int>(code);
        }
    };

    struct Stats {
        qint64 wait_ns{};
        qint64 message_received_ns{};
        uint64_t wait_iterations{};
        uint64_t message_received_signals{};
        uint64_t queued_messages{};
        uint64_t read_failures{};
    };

    GrpcIncomingStream(std::unique_ptr<QGrpcServerStream>&& stream)
        : stream_{std::move(stream)}
    {
#ifndef USE_GRPC_STREAMING_FLOW_CONTROL
        connect(stream_.get(), &QGrpcServerStream::messageReceived, this, &GrpcIncomingStream::messageReceived);
#endif
        connect(stream_.get(), &QGrpcServerStream::finished, this, [this]() {
            LOG_TRACE_N << "Stream finished";
            setState(State::DONE);
            emit haveMessage();
        });

        setState(State::STREAMING);
    }


    template <ProtoMessage T>
    QCoro::Task<tl::expected<T, Error>> next()
    {
        LOG_TRACE_N << "Next message";
        QElapsedTimer wait_timer;
        wait_timer.start();
        qint64 last_wait_log_ms = 0;

        while(true) {
            LOG_TRACE_N << "Next message / top of loop";

#ifdef USE_GRPC_STREAMING_FLOW_CONTROL
            if (state_ == State::DONE) {
                LOG_TRACE_N << "Stream is closed";
                co_return tl::unexpected(Error{Error::Code::CLOSED});
            }
            if (auto message = stream_->read<nextapp::pb::Status>()) {
                LOG_TRACE_N << "Read message.";
                co_return std::move(message.value());
            }
#else
            if (!messages_.empty()) {
                auto message = std::move(messages_.front());
                messages_.pop();
                LOG_TRACE_N << "Returning message to caller";
                co_return std::move(message);
            }

            if (read_failed_) {
                LOG_DEBUG_N << "Stream message read failed";
                co_return tl::unexpected(Error{Error::Code::READ_FAILED});
            }

            if (state_ == State::DONE) {
                LOG_TRACE_N << "Stream is closed";
                co_return tl::unexpected(Error{Error::Code::CLOSED});
            }
#endif
            assert(state_ != State::DONE);

            // We have to wait for the next message
            LOG_TRACE_N << "Waiting for event";
            QElapsedTimer wait_iteration_timer;
            wait_iteration_timer.start();
            co_await qCoro(this, &GrpcIncomingStream::haveMessage);
            stats_.wait_ns += wait_iteration_timer.nsecsElapsed();
            ++stats_.wait_iterations;
            LOG_TRACE_N << "Event received";
            if (wait_timer.elapsed() - last_wait_log_ms >= 30000) {
                last_wait_log_ms = wait_timer.elapsed();
                LOG_DEBUG_N << "Still waiting for gRPC stream message after "
                           << (last_wait_log_ms / 1000) << "s";
            }
        }
    }

    const auto& stream() const noexcept {
        assert(stream_);
        return *stream_;
    }

    Stats stats() const noexcept {
        return stats_;
    }

signals:
    void haveMessage();

private:

    // The QT stream implementation does not support reading messages asynchrouneosly.
    // As soon as the signal is received, we need to read the message. Else it's gone
    // if there is a new incoming message pending. So unless we block the thread,
    // there is no way for the client to limit the speed of the incoming messages.
    void messageReceived() {
        QElapsedTimer signal_timer;
        signal_timer.start();
        ++stats_.message_received_signals;
        ScopedExit record_elapsed{[this, &signal_timer] {
            stats_.message_received_ns += signal_timer.nsecsElapsed();
        }};

        LOG_TRACE_N << "Message received";
        if (state_ == State::IDLE) {
            setState(State::STREAMING);
        }        
#ifdef USE_GRPC_STREAMING_FLOW_CONTROL
        emit haveMessage();
#else
        LOG_TRACE_N << "Reading message";
        if (auto message = stream_->read<nextapp::pb::Status>()) {
            LOG_TRACE_N << "Read message. Queuing it.";
            messages_.emplace(std::move(message.value()));
            ++stats_.queued_messages;
            emit haveMessage();
            return;
        }
        ++stats_.read_failures;
        read_failed_ = true;
        LOG_WARN_N << "Failed to read message from gRPC stream";
        emit haveMessage();
#endif
    }

    void setState(State state) noexcept {
        if (state_ != state) {
            LOG_TRACE_N << "State changed from " << static_cast<int>(state_) << " to " << static_cast<int>(state);
            state_ = state;
        }
    }

    std::unique_ptr<QGrpcServerStream> stream_;
    State state_{State::IDLE};
    Stats stats_;
    bool read_failed_{false};

#ifndef USE_GRPC_STREAMING_FLOW_CONTROL
    // QObject's cant be templates, so we have to use a queue with a known type.
    std::queue<nextapp::pb::Status> messages_;
#endif
};

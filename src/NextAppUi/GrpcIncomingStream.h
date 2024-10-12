#pragma once

#include <queue>

#include "tl/expected.hpp"

#include <QObject>
#include <QGrpcServerStream>
#include "qcorotask.h"
#include "qcorosignal.h"
#include "logging.h"
#include "nextapp.qpb.h"

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

    GrpcIncomingStream(std::unique_ptr<QGrpcServerStream>&& stream)
        : stream_{std::move(stream)}
    {
        connect(stream_.get(), &QGrpcServerStream::messageReceived, this, &GrpcIncomingStream::messageReceived);
        connect(stream_.get(), &QGrpcServerStream::finished, this, [this]() {
            LOG_TRACE_N << "Stream finished";
            setState(State::DONE);
            emit haveMessage();
        });
    }


    template <ProtoMessage T>
    QCoro::Task<tl::expected<T, Error>> next()
    {
        LOG_TRACE_N << "Next message";

        while(true) {
            LOG_TRACE_N << "Next message / top of loop";
            if (!messages_.empty()) {
                auto message = std::move(messages_.front());
                messages_.pop();
                LOG_TRACE_N << "Returning message to caller";
                co_return std::move(message);
            }

            if (state_ == State::DONE) {
                LOG_TRACE_N << "Stream is closed";
                co_return tl::unexpected(Error{Error::Code::CLOSED});
            }

            assert(state_ != State::DONE);

            // We have to wait for the next message
            LOG_TRACE_N << "Waiting for event";
            co_await qCoro(this, &GrpcIncomingStream::haveMessage);
            LOG_TRACE_N << "Event received";
        }
    }

    const auto& stream() const noexcept {
        assert(stream_);
        return *stream_;
    }

signals:
    void haveMessage();

private:

    // The QT stream implementation does not support reading messages asynchrouneosly.
    // As soon as the signal is received, we need to read the message. Else it's gone
    // if there is a new incoming message pending. So unless we block the thread,
    // there is no way for the client to limit the speed of the incoming messages.
    void messageReceived() {
        LOG_TRACE_N << "Message received";
        if (state_ == State::IDLE) {
            setState(State::STREAMING);
        }

        LOG_TRACE_N << "Reading message";
        if (auto message = stream_->read<nextapp::pb::Status>()) {
            LOG_TRACE_N << "Read message. Queuing it.";
            messages_.emplace(std::move(message.value()));
            emit haveMessage();
            return;
        }
        LOG_TRACE_N << "Failed to read message";
    }

    void setState(State state) noexcept {
        if (state_ != state) {
            LOG_TRACE_N << "State changed from " << static_cast<int>(state_) << " to " << static_cast<int>(state);
            state_ = state;
        }
    }

    std::unique_ptr<QGrpcServerStream> stream_;
    State state_{State::IDLE};

    // QObject's cant be templates, so we have to use a queue with a known type.
    std::queue<nextapp::pb::Status> messages_;
};

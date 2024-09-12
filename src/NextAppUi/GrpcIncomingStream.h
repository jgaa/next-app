#pragma once

#include "tl/expected.hpp"

#include <QObject>
#include <QGrpcServerStream>
#include "qcorotask.h"
#include "qcorosignal.h"
#include "logging.h"

template <typename T>
concept ProtoMessage = std::is_base_of_v<QProtobufMessage, T>;

/*! \brief A coroutine-compatible class for reading incoming gRPC messages from a stream.
 *
 * This class is designed to be used in a coroutine, where the next() method is called
 * to read the next message from the stream. The method will return a tl::expected
 * with either the message or an error code.
 *
 * The class will emit a signal haveMessage() when a new message is available.
 */

class GrpcIncomingStream : public QObject
{
    Q_OBJECT
public:
    enum class State {
        IDLE,
        HAVE_MESSAGE,
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

    GrpcIncomingStream(std::shared_ptr<QGrpcServerStream> stream)
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
        if (state_ == State::DONE) {
            LOG_TRACE_N << "Stream is closed";
            co_return tl::unexpected(Error{Error::Code::CLOSED});
        }

        if (stream_->isFinished()) {
            LOG_TRACE_N << "Stream is closed";
            setState(State::DONE);
            co_return tl::unexpected(Error{Error::Code::CLOSED});
        }

        assert(state_ != State::HAVE_MESSAGE);
        assert(state_ != State::DONE);

        // We have to wait for the next message
        LOG_TRACE_N << "Waiting for message";
        co_await qCoro(this, &GrpcIncomingStream::haveMessage);
        LOG_TRACE_N << "Message received";
        if (state_ == State::DONE) {
            LOG_TRACE_N << "Stream is closed";
            co_return tl::unexpected(Error{Error::Code::CLOSED});
        }

        LOG_TRACE_N << "Reading message after await.";
        co_return read<T>();
    }

    const auto& stream() const noexcept {
        assert(stream_);
        return *stream_;
    }

signals:
    void haveMessage();

private:

    // The assumption is that a stream will not get another `messageReceived` signal until read() has been called.
    void messageReceived()
    {
        LOG_TRACE_N << "Message received";
        assert(state_ == State::IDLE);
        setState(State::HAVE_MESSAGE);
        emit haveMessage();
    }

    void setState(State state) noexcept
    {
        if (state_ != state) {
            LOG_TRACE_N << "State changed from " << static_cast<int>(state_) << " to " << static_cast<int>(state);
            state_ = state;
        }
    }

    template <typename T>
    tl::expected<T, Error> read() {
        assert(state_ == State::HAVE_MESSAGE);
        setState(State::IDLE);

        LOG_TRACE_N << "Reading message";
        if (auto message = stream_->read<T>()) {
            LOG_TRACE_N << "Read message";
            return message.value();
        }

        LOG_TRACE_N << "Failed to read message";

        if (stream_->isFinished()) {
            LOG_TRACE_N << "Stream is finished";
            setState(State::DONE);
            return tl::unexpected(Error{Error::Code::CLOSED});
        }

        LOG_TRACE_N << "returning Read failed";
        return tl::unexpected(Error{Error::Code::READ_FAILED});
    }

    std::shared_ptr<QGrpcServerStream> stream_;
    State state_{State::IDLE};
};

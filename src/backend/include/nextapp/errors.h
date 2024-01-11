#pragma once

#include <string>
#include <stdexcept>
#include "nextapp.pb.h"

namespace nextapp {

enum class ErrorCode {
    OK
};

struct aborted : public std::runtime_error {
    aborted() noexcept : std::runtime_error{"aborted"} {};

    template <typename T>
    aborted(T message) : std::runtime_error{message} {};
};

struct db_err : public std::runtime_error {

    db_err(pb::Error error, std::string message) noexcept
        :  std::runtime_error{std::move(message)}, error_{error} {}

    pb::Error error() const noexcept {
        return error_;
    }

 private:
    pb::Error error_;
};


} //ns

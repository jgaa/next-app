#pragma once

#include <stdexcept>

namespace nextapp {

enum class ErrorCode {
    OK
};

struct aborted : public std::runtime_error {
    aborted() noexcept : std::runtime_error{"aborted"} {};

    template <typename T>
    aborted(T message) : std::runtime_error{message} {};
};

} //ns

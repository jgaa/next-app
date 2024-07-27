#pragma once

#include "boost/system/error_category.hpp"

#include "nextapp.pb.h"
#include "grpcpp/grpcpp.h"


namespace boost::system {

template <>
struct is_error_code_enum<nextapp::pb::Error> : std::true_type {};

template <>
struct is_error_code_enum<::grpc::StatusCode> : std::true_type {};

} // ns

namespace nextapp {

class ErrorCategory : public boost::system::error_category {
    const char *name() const noexcept override {
        return "signup-grpc";
    }

    std::string message(int ev) const override;
};

class GrpcStatusErrorCategory : public boost::system::error_category {
    const char *name() const noexcept override {
        return "grpc-status";
    }

    std::string message(int ev) const override;
};

} // ns

boost::system::error_code make_error_code(const nextapp::pb::Error& e) noexcept;

boost::system::error_code make_error_code(const ::grpc::StatusCode& code) noexcept;

#include "nextapp/error_mapping.h"

using namespace std;

namespace nextapp {
string ErrorCategory::message(int ev) const
{
    static constexpr auto msgs = to_array<string_view>({
        "OK",
        "Missing tenant name",
        "Missing user name",
        "Missing user email",
        "Already exist",
        "Invalid parent",
        "Database update failed",
        "Database request failed",
        "Not found",
        "Different parent",
        "No changes",
        "Constraint failed",
        "Generic error",
        "Invalid action",
        "Invalid request",
        "Page size too small",
        "Auth missing session id",
        "No relevant data",
        "Missing CSR",
        "Invalid CSR",
        "Permission denied",
        "Tenant suspended",
        "User suspended",
        "Missing user id",
        "Auth failed",
        "Conflict",
    });

    if (ev < 0 || static_cast<size_t>(ev) >= msgs.size()) {
        return format("Unknown error nextapp::pb::Error={}", ev);
    }
    return string{msgs[ev]};
}


string GrpcStatusErrorCategory::message(int ev) const
{
    static constexpr auto msgs = to_array<string_view>({
        "OK",
        "CANCELLED",
        "UNKNOWN",
        "INVALID_ARGUMENT",
        "DEADLINE_EXCEEDED",
        "NOT_FOUND",
        "ALREADY_EXISTS",
        "PERMISSION_DENIED",
        "RESOURCE_EXHAUSTED",
        "FAILED_PRECONDITION",
        "ABORTED",
        "OUT_OF_RANGE",
        "UNIMPLEMENTED",
        "INTERNAL",
        "UNAVAILABLE",
        "DATA_LOSS",
        "UNAUTHENTICATED"
    });

    if (ev < 0 || static_cast<size_t>(ev) >= msgs.size()) {
        return format("Unknown error ::grpc::StatusCode={}", ev);
    }
    return string{msgs[ev]};
}

} // ns

boost::system::error_code make_error_code(const nextapp::pb::Error &e) noexcept {
    static constexpr nextapp::ErrorCategory category;
    return {static_cast<int>(e), category};
}

boost::system::error_code make_error_code(const grpc::StatusCode &code) noexcept {
    static constexpr nextapp::GrpcStatusErrorCategory category;
    return {static_cast<int>(code), category};
}

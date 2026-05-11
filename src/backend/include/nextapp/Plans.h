#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <map>
#include <string>
#include <unordered_map>

#include <boost/asio.hpp>

#include <grpcpp/grpcpp.h>

#include "payments/v1/payments.grpc.pb.h"

#include "nextapp/config.h"
#include "nextapp/error_mapping.h"
#include "nextapp/logging.h"
#include "nextapp/util.h"

namespace nextapp {

class Server;

class Plans {
public:
    struct PlanProperties {
        int64_t max_users = 1;
        int64_t max_devices = 5;
        int64_t max_nodes = 2000;
        int64_t max_actions = 1240;
        int64_t max_worksessions = 1240;
        int64_t max_time_blocks = 1240;
        bool mobile_only = false;
    };

    struct ActivePlans {
        std::map<std::string, PlanProperties> plans;
        std::string default_for_signup;
        std::string default_for_free;
    };

    explicit Plans(Server& server);
    ~Plans();

    boost::asio::awaitable<void> connect();
    void shutdown();

    bool isConnected() const noexcept {
        return static_cast<bool>(stub_);
    }

    boost::asio::awaitable<payments::v1::CreateCheckoutContextResponse>
    createCheckoutContext(payments::v1::CreateCheckoutContextRequest request);

    boost::asio::awaitable<payments::v1::GetEntitlementResponse>
    getEntitlement(payments::v1::GetEntitlementRequest request);

    boost::asio::awaitable<payments::v1::GetEntitlementsResponse>
    getEntitlements(payments::v1::GetEntitlementsRequest request);

    boost::asio::awaitable<payments::v1::ConfirmExternalPurchaseResponse>
    confirmExternalPurchase(payments::v1::ConfirmExternalPurchaseRequest request);

    boost::asio::awaitable<payments::v1::RegisterGooglePlayPurchaseResponse>
    registerGooglePlayPurchase(payments::v1::RegisterGooglePlayPurchaseRequest request);

    boost::asio::awaitable<payments::v1::GetPlansResponse>
    getPlans(payments::v1::GetPlansRequest request);


    boost::asio::awaitable<void> syncPlans();

    std::shared_ptr<const ActivePlans> activePlans() const noexcept {
        return active_plans_.load();
    }

    std::string getPlanForSignup() const;

    boost::asio::awaitable<void> loadActivePlans();

private:
    template <ProtoMessage ReplyT, ProtoMessage ReqT, typename T>
    struct CallData {
        CallData(ReqT&& req, T& self)
            : request{std::forward<ReqT>(req)}, self_{std::move(self)} {}

        ReqT request;
        ::grpc::ClientContext ctx;
        ReplyT reply;
        std::remove_cvref_t<T> self_;
    };

    template <ProtoMessage ReplyT, ProtoMessage ReqT, typename CompletionToken>
    auto callRpc(
        ReqT request,
        void (::payments::v1::PaymentsService::Stub::async::*call)(
            ::grpc::ClientContext* context,
            const ReqT* request,
            ReplyT* response,
            std::function<void(::grpc::Status)>),
        CompletionToken&& token) {

        return boost::asio::async_compose<CompletionToken, void(boost::system::error_code, ReplyT)>(
            [this, request = std::move(request), call](auto& self) mutable {
                auto cd = std::make_shared<CallData<ReplyT, ReqT, decltype(self)>>(std::move(request), self);

                auto fn = [this, cd](const ::grpc::Status& status) mutable {
                    boost::system::error_code ec;
                    if (!status.ok()) {
                        ec = make_error_code(status.error_code());
                        LOG_WARN_N << "Payment RPC failed. Status code: "
                                   << static_cast<int>(status.error_code())
                                   << ", message: " << status.error_message();
                    } else {
                        LOG_TRACE << "Payment RPC completed. Status: " << status.error_message();
                        LOG_TRACE << "Reply: " << toJson(cd->reply, logProtobufMode());
                    }

                    cd->self_.complete(ec, cd->reply);
                };

                if (!stub_) {
                    cd->self_.complete(make_error_code(::grpc::StatusCode::UNAVAILABLE), cd->reply);
                    return;
                }

                (stub_->async()->*call)(&cd->ctx, &cd->request, &cd->reply,
                                        [fn = std::move(fn)](const ::grpc::Status& status) mutable {
                                            fn(status);
                                        });
            },
            token);
    }

    const PaymentOptions& config() const noexcept;
    int logProtobufMode() const noexcept;
    std::string grpcServerAddress() const;
    std::shared_ptr<::grpc::ChannelCredentials> createCredentials() const;

    Server& server_;
    std::shared_ptr<::grpc::Channel> channel_;
    std::unique_ptr<payments::v1::PaymentsService::Stub> stub_;
    std::atomic_bool is_syncing_plans_{false};
    std::atomic_bool is_loading_plans_{false};
    std::unordered_map<std::string, int32_t> synced_plan_versions_;
    std::atomic<std::shared_ptr<const ActivePlans>> active_plans_;
};

} // namespace nextapp

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <unordered_set>
#include <unordered_map>

#include <boost/asio.hpp>

#include <grpcpp/grpcpp.h>

#include "payments/v1/payments.grpc.pb.h"
#include "payments/v1/notifications.grpc.pb.h"

#include "nextapp/logging.h"

#include "mysqlpool/mysqlpool.h"

#include "nextapp/AsyncClientReadReactor.h"
#include "nextapp/config.h"
#include "nextapp/error_mapping.h"
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
        uint32_t trial_days = 0;
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

    boost::asio::awaitable<payments::v1::EnsureTenantInitializedResponse>
    ensureTenantInitialized(payments::v1::EnsureTenantInitializedRequest request);

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

    std::tuple<std::string/* plan id*/, bool /* is trial */> getPlanForSignup() const;

    boost::asio::awaitable<void> loadActivePlans();
    boost::asio::awaitable<bool> refreshTenantEntitlement(
        jgaa::mysqlpool::Mysqlpool::Handle& dbh,
        const boost::uuids::uuid& tenant_id);
    boost::asio::awaitable<void> refreshTenantSubscription(
        jgaa::mysqlpool::Mysqlpool::Handle& dbh,
        const boost::uuids::uuid& tenant_id);
    boost::asio::awaitable<void> queueTenantRegistration(const boost::uuids::uuid& tenant_id);
    boost::asio::awaitable<void> onServerReady();

private:
    using EntitlementStream = AsyncClientReadReactor<
        payments::v1::SubscribeEntitlementChangesRequest,
        payments::v1::EntitlementChangeEvent>;

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
    void startEntitlementSubscription();
    boost::asio::awaitable<void> runEntitlementSubscriptionLoop();
    boost::asio::awaitable<void> runTenantRegistrationLoop();
    boost::asio::awaitable<void> processPendingTenantRegistrations(std::string_view reason);
    boost::asio::awaitable<void> reconcileLocalOnlyTenants();
    boost::asio::awaitable<void> applyEntitlementChange(const payments::v1::EntitlementChangeEvent& event);
    boost::asio::awaitable<bool> updatePlanFromEntitlement(
        jgaa::mysqlpool::Mysqlpool::Handle& dbh,
        const boost::uuids::uuid& tenant_id,
        const payments::v1::Entitlement& entitlement);
    boost::asio::awaitable<std::string> getTenantRegistrationState(
        jgaa::mysqlpool::Mysqlpool::Handle& dbh,
        const boost::uuids::uuid& tenant_id);
    boost::asio::awaitable<bool> ensureTenantRegistered(
        jgaa::mysqlpool::Mysqlpool::Handle& dbh,
        const boost::uuids::uuid& tenant_id,
        bool publish_changes);
    boost::asio::awaitable<void> downgradeTenantToFreePlan(
        jgaa::mysqlpool::Mysqlpool::Handle& dbh,
        const boost::uuids::uuid& tenant_id,
        std::string_view reason);
    bool tryBeginTenantRegistration(const boost::uuids::uuid& tenant_id);
    void endTenantRegistration(const boost::uuids::uuid& tenant_id) noexcept;

    Server& server_;
    std::shared_ptr<::grpc::Channel> channel_;
    std::unique_ptr<payments::v1::PaymentsService::Stub> stub_;
    std::unique_ptr<payments::v1::EntitlementNotificationsService::Stub> notifications_stub_;
    std::atomic_bool is_syncing_plans_{false};
    std::atomic_bool is_loading_plans_{false};
    std::atomic_bool stopping_{false};
    std::atomic_bool entitlement_subscription_running_{false};
    std::atomic_bool tenant_registration_loop_running_{false};
    std::unordered_map<std::string, int32_t> synced_plan_versions_;
    std::atomic<std::shared_ptr<const ActivePlans>> active_plans_;
    mutable std::mutex entitlement_stream_mutex_;
    std::shared_ptr<EntitlementStream> entitlement_stream_;
    mutable std::mutex tenant_registration_mutex_;
    std::unordered_set<std::string> tenant_registrations_in_flight_;
};

} // namespace nextapp

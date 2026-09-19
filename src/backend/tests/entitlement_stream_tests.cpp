#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

#include <boost/asio.hpp>
#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include "nextapp/AsyncClientReadReactor.h"
#include "payments/v1/notifications.grpc.pb.h"

using namespace std::chrono_literals;

namespace {

using Stream = nextapp::AsyncClientReadReactor<
    payments::v1::SubscribeEntitlementChangesRequest,
    payments::v1::EntitlementChangeEvent>;

class NotificationService final : public payments::v1::EntitlementNotificationsService::Service {
public:
    grpc::Status SubscribeEntitlementChanges(
        grpc::ServerContext* context,
        const payments::v1::SubscribeEntitlementChangesRequest*,
        grpc::ServerWriter<payments::v1::EntitlementChangeEvent>* writer) override
    {
        {
            std::scoped_lock lock{mutex_};
            ++subscriptions_;
        }
        subscriptions_changed_.notify_all();
        while (!context->IsCancelled()) {
            std::unique_lock lock{mutex_};
            events_changed_.wait_for(lock, 10ms, [this, context] {
                return !events_.empty() || context->IsCancelled();
            });
            if (context->IsCancelled()) {
                break;
            }
            if (events_.empty()) {
                continue;
            }
            auto event = std::move(events_.front());
            events_.pop_front();
            lock.unlock();
            if (!writer->Write(event)) {
                break;
            }
        }
        return grpc::Status::OK;
    }

    void publish(std::string id, uint64_t version)
    {
        payments::v1::EntitlementChangeEvent event;
        event.set_event_id(std::move(id));
        event.set_subject_id("tenant");
        event.mutable_entitlement()->set_product_id("nextapp");
        event.mutable_entitlement()->set_version(version);
        {
            std::scoped_lock lock{mutex_};
            events_.push_back(std::move(event));
        }
        events_changed_.notify_all();
    }

    bool waitForSubscriptions(size_t count, std::chrono::milliseconds timeout)
    {
        std::unique_lock lock{mutex_};
        return subscriptions_changed_.wait_for(lock, timeout, [this, count] {
            return subscriptions_ >= count;
        });
    }

    size_t subscriptions() const
    {
        std::scoped_lock lock{mutex_};
        return subscriptions_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable events_changed_;
    std::condition_variable subscriptions_changed_;
    std::deque<payments::v1::EntitlementChangeEvent> events_;
    size_t subscriptions_{};
};

class EntitlementStreamTest : public testing::Test {
protected:
    void SetUp() override
    {
        work_.emplace(io_.get_executor());
        io_thread_ = std::thread([this] { io_.run(); });
        startServer("127.0.0.1:0");
    }

    void TearDown() override
    {
        stopServer();
        work_.reset();
        io_.stop();
        io_thread_.join();
    }

    void startServer(const std::string& address)
    {
        service_ = std::make_unique<NotificationService>();
        grpc::ServerBuilder builder;
        builder.AddListeningPort(address, grpc::InsecureServerCredentials(), &port_);
        builder.RegisterService(service_.get());
        server_ = builder.BuildAndStart();
        ASSERT_NE(server_, nullptr);
        channel_ = grpc::CreateChannel(
            "127.0.0.1:" + std::to_string(port_), grpc::InsecureChannelCredentials());
        stub_ = payments::v1::EntitlementNotificationsService::NewStub(channel_);
    }

    void stopServer()
    {
        if (server_) {
            server_->Shutdown(std::chrono::system_clock::now() + 2s);
            server_->Wait();
            server_.reset();
        }
    }

    std::shared_ptr<Stream> makeStream()
    {
        payments::v1::SubscribeEntitlementChangesRequest request;
        request.set_backend_instance_id("test-backend");
        auto stream = std::make_shared<Stream>(
            io_, std::move(request),
            [this](grpc::ClientContext& context,
                   const payments::v1::SubscribeEntitlementChangesRequest* request,
                   grpc::ClientReadReactor<payments::v1::EntitlementChangeEvent>* reactor) {
                stub_->async()->SubscribeEntitlementChanges(&context, request, reactor);
            });
        stream->start();
        return stream;
    }

    template <typename Awaitable>
    auto wait(Awaitable awaitable)
    {
        return boost::asio::co_spawn(io_, std::move(awaitable), boost::asio::use_future).get();
    }

    boost::asio::io_context io_;
    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_;
    std::thread io_thread_;
    std::unique_ptr<NotificationService> service_;
    std::unique_ptr<grpc::Server> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<payments::v1::EntitlementNotificationsService::Stub> stub_;
    int port_{};
};

TEST_F(EntitlementStreamTest, DeliversEventsNormally)
{
    auto stream = makeStream();
    ASSERT_TRUE(service_->waitForSubscriptions(1, 2s));
    service_->publish("event-1", 1);

    const auto result = wait(stream->readFor(2s));
    ASSERT_EQ(result.outcome, Stream::ReadOutcome::message);
    ASSERT_TRUE(result.message);
    EXPECT_EQ(result.message->event_id(), "event-1");
    EXPECT_EQ(result.message->entitlement().version(), 1);
    stream->cancel();
    EXPECT_FALSE(wait(stream->waitForDone()).ok());
}

TEST_F(EntitlementStreamTest, ServerShutdownTerminatesStream)
{
    auto stream = makeStream();
    ASSERT_TRUE(service_->waitForSubscriptions(1, 2s));

    stopServer();
    const auto result = wait(stream->readFor(2s));
    EXPECT_EQ(result.outcome, Stream::ReadOutcome::done);
    EXPECT_FALSE(wait(stream->waitForDone()).ok());
}

TEST_F(EntitlementStreamTest, ServerRestartAllowsResubscriptionAndDelivery)
{
    auto original = makeStream();
    ASSERT_TRUE(service_->waitForSubscriptions(1, 2s));
    service_->publish("before-restart", 1);
    ASSERT_EQ(wait(original->readFor(2s)).outcome, Stream::ReadOutcome::message);

    const auto address = "127.0.0.1:" + std::to_string(port_);
    stopServer();
    EXPECT_EQ(wait(original->readFor(2s)).outcome, Stream::ReadOutcome::done);
    EXPECT_FALSE(wait(original->waitForDone()).ok());

    startServer(address);
    auto replacement = makeStream();
    ASSERT_TRUE(service_->waitForSubscriptions(1, 2s));
    service_->publish("after-restart", 2);
    const auto result = wait(replacement->readFor(2s));
    ASSERT_EQ(result.outcome, Stream::ReadOutcome::message);
    ASSERT_TRUE(result.message);
    EXPECT_EQ(result.message->event_id(), "after-restart");
    EXPECT_EQ(result.message->entitlement().version(), 2);
    replacement->cancel();
    (void)wait(replacement->waitForDone());
}

TEST_F(EntitlementStreamTest, SilentStreamIsCancelledAndResubscribed)
{
    auto stale = makeStream();
    ASSERT_TRUE(service_->waitForSubscriptions(1, 2s));
    EXPECT_EQ(wait(stale->readFor(100ms)).outcome, Stream::ReadOutcome::timeout);
    stale->cancel();
    EXPECT_FALSE(wait(stale->waitForDone()).ok());

    auto replacement = makeStream();
    ASSERT_TRUE(service_->waitForSubscriptions(2, 2s));
    service_->publish("event-after-reconnect", 2);
    const auto result = wait(replacement->readFor(2s));
    ASSERT_EQ(result.outcome, Stream::ReadOutcome::message);
    ASSERT_TRUE(result.message);
    EXPECT_EQ(result.message->event_id(), "event-after-reconnect");
    EXPECT_EQ(result.message->entitlement().version(), 2);
    replacement->cancel();
    (void)wait(replacement->waitForDone());
}

TEST_F(EntitlementStreamTest, CleanCancellationDoesNotCreateAnotherSubscription)
{
    auto stream = makeStream();
    ASSERT_TRUE(service_->waitForSubscriptions(1, 2s));
    stream->cancel();
    EXPECT_FALSE(wait(stream->waitForDone()).ok());
    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(service_->subscriptions(), 1);
}

} // namespace

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

#include "gtest/gtest.h"

#include "signup/Server.h"

namespace nextapp {
namespace {

Config makeConfig()
{
    Config config;
    config.options.enable_http = false;
    return config;
}

TEST(SignupServerTests, SameEmailReservationBlocksConcurrentSignupAndAllowsRestartAfterRelease)
{
    Server server{makeConfig()};

    EXPECT_TRUE(server.tryReserveEmailSignup("email-hash"));
    EXPECT_FALSE(server.tryReserveEmailSignup("email-hash"));

    server.releaseEmailSignup("email-hash");

    EXPECT_TRUE(server.tryReserveEmailSignup("email-hash"));
    server.releaseEmailSignup("email-hash");
}

TEST(SignupServerTests, SignupRateLimitUsesPeerAndEmailBucketsIndependently)
{
    Server server{makeConfig()};
    server.testSetSignupRateLimitConfig({
        .ip_burst = 2,
        .ip_refill_per_second = 0.0,
        .email_burst = 3,
        .email_refill_per_second = 0.0,
    });

    EXPECT_TRUE(server.testAllowSignupAttemptLocal("ipv4:203.0.113.10:5000", "one@example.com"));
    EXPECT_TRUE(server.testAllowSignupAttemptLocal("ipv4:203.0.113.10:5001", "two@example.com"));
    EXPECT_FALSE(server.testAllowSignupAttemptLocal("ipv4:203.0.113.10:5002", "three@example.com"));
}

TEST(SignupServerTests, SignupRateLimitUsesEmailBucketAcrossDifferentPeers)
{
    Server server{makeConfig()};
    server.testSetSignupRateLimitConfig({
        .ip_burst = 10,
        .ip_refill_per_second = 0.0,
        .email_burst = 2,
        .email_refill_per_second = 0.0,
    });

    EXPECT_TRUE(server.testAllowSignupAttemptLocal("ipv4:203.0.113.10:5000", "same@example.com"));
    EXPECT_TRUE(server.testAllowSignupAttemptLocal("ipv4:203.0.113.11:5000", "same@example.com"));
    EXPECT_FALSE(server.testAllowSignupAttemptLocal("ipv4:203.0.113.12:5000", "same@example.com"));
}

} // namespace
} // namespace nextapp

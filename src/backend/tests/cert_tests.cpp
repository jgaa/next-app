#include "gtest/gtest.h"
#include "nextapp/certs.h"

TEST(ServerCertificate, UsesTypedSansAndPreservesThemForRenewal)
{
    const nextapp::CaOptions options{.key_bytes = 2048};
    const auto root = nextapp::createCaCert("Test CA", 30, options.key_bytes);
    nextapp::CertAuthority authority{root, options};
    const auto issued = authority.createServerCert({
        "nextapp.example.test", "192.0.2.10", "2001:db8::10", "nextappd"
    });
    const auto cert = nextapp::loadCertFromBuffer(issued.cert);

    EXPECT_EQ(X509_check_host(cert.get(), "nextapp.example.test", 0, 0, nullptr), 1);
    EXPECT_EQ(X509_check_host(cert.get(), "nextappd", 0, 0, nullptr), 1);
    EXPECT_EQ(X509_check_host(cert.get(), "unrelated.example.test", 0, 0, nullptr), 0);
    EXPECT_EQ(X509_check_ip_asc(cert.get(), "192.0.2.10", 0), 1);
    EXPECT_EQ(X509_check_ip_asc(cert.get(), "2001:db8::10", 0), 1);
    EXPECT_EQ(X509_check_ip_asc(cert.get(), "192.0.2.11", 0), 0);
    // Numeric names must be encoded as IP SANs, never DNS SANs.
    EXPECT_EQ(X509_check_host(cert.get(), "192.0.2.10", 0, 0, nullptr), 0);

    const auto inspected = nextapp::inspectCert(issued.cert);
    EXPECT_EQ(inspected.subject_alt_names, (std::vector<std::string>{
        "nextapp.example.test", "192.0.2.10", "2001:db8::10", "nextappd"
    }));
    std::vector<std::string_view> renewal_names{
        inspected.subject_alt_names.begin(), inspected.subject_alt_names.end()
    };
    const auto renewed = authority.createServerCert(renewal_names);
    const auto renewed_cert = nextapp::loadCertFromBuffer(renewed.cert);
    EXPECT_EQ(X509_check_ip_asc(renewed_cert.get(), "192.0.2.10", 0), 1);
    EXPECT_EQ(X509_check_ip_asc(renewed_cert.get(), "2001:db8::10", 0), 1);
    EXPECT_EQ(X509_check_host(renewed_cert.get(), "nextappd", 0, 0, nullptr), 1);
    EXPECT_EQ(authority.rootCert(), root.cert);
    const auto ca = nextapp::loadCertFromBuffer(root.cert);
    EXPECT_EQ(X509_verify(renewed_cert.get(), X509_get0_pubkey(ca.get())), 1);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

#pragma once

#include <string>
#include <vector>
#include <cstring>
#include <memory>

#include <boost/uuid/uuid.hpp>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

namespace nextapp {

template<typename T>
concept IntOrUUID = std::is_same_v<T, int> || std::is_same_v<T, long> || std::is_same_v<T, boost::uuids::uuid>;

struct CaOptions {
    unsigned lifetime_days_certs = 356;
    unsigned key_bytes = 4096;
};

struct X509_Deleter {
    void operator()(X509* x) const {
        X509_free(x);
    }
};

using X509_Ptr = std::unique_ptr<X509, X509_Deleter>;

struct EVP_PKEY_Deleter {
    void operator()(EVP_PKEY* p) const {
        EVP_PKEY_free(p);
    }
};

using EVP_PKEY_Ptr = std::unique_ptr<EVP_PKEY, EVP_PKEY_Deleter>;


struct CertData {
    boost::uuids::uuid id; // Unique id for the cert, except for root CA cert which is serial '1'
    std::string cert;
    std::string key;

    ~CertData() {
        std::memset(key.data(), 0, key.size());
    }
};

CertData createCaCert(
    const std::string& caName = "CA Root Authority",
    unsigned lifetimeDays = 356 * 10,
    unsigned keyBytes = 4096);

class CertAuthority {
public:
    CertAuthority(const CertData& rootCaCert, const CaOptions& options);

    CertData createServerCert(const std::string& serverSubject);
    CertData createClientCert(const std::string& clientSubject, unsigned lifetimeDays = 356, unsigned keyBytes = 4096);
    CertData signCert(const std::string_view& csr, const std::string& subject, unsigned lifetimeDays = 356, unsigned keyBytes = 4096);

private:
    const CaOptions& options_;
    X509_Ptr rootCa_;
    EVP_PKEY_Ptr rootKey_;
    std::string ca_name_;
};

} // ns

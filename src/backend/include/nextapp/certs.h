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

#include "nextapp/util.h"

namespace nextapp {

template<typename T>
concept IntOrUUID = std::is_same_v<T, int> || std::is_same_v<T, long> || std::is_same_v<T, boost::uuids::uuid>;

struct CaOptions {
    unsigned lifetime_days_certs = 356;
    unsigned key_bytes = 4096;
    std::string ca_name = "CA Nextapp Root Authority";
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
    CertData createClientCert(const std::string& clientSubject);
    CertData signCert(const std::string_view& csr, const std::string& subject);

    const auto& caName() const noexcept {
        return ca_name_;
    }

    const auto& rootCert() const noexcept {
        return root_cert_;
    }

private:
    const CaOptions& options_;
    X509_Ptr rootCa_;
    EVP_PKEY_Ptr rootKey_;
    std::string ca_name_;
    std::string root_cert_;
};

X509_Ptr loadCertFromBuffer(const auto& buffer) {
    if (auto* bio = BIO_new_mem_buf(buffer.data(), buffer.size())) {
        ScopedExit bio_cleanup{[bio] {
            BIO_free(bio);
        }};
        if (auto cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr)) {
            return X509_Ptr{cert};
        } else {
            throw std::runtime_error{"PEM_read_bio_X509"};
        }
    } else {
        throw std::runtime_error{"BIO_new_mem_buf"};
    }
}

EVP_PKEY_Ptr loadKeyFromBuffer(const auto& buffer) {
    if (auto* bio = BIO_new_mem_buf(buffer.data(), buffer.size())) {
        ScopedExit bio_cleanup{[bio] {
            BIO_free(bio);
        }};
        if (auto key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr)) {
            return EVP_PKEY_Ptr{key};
        } else {
            throw std::runtime_error{"PEM_read_bio_X509"};
        }
    } else {
        throw std::runtime_error{"BIO_new_mem_buf"};
    }
}


} // ns

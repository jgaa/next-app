#include "nextapp/certs.h"

#include <string>
#include <string_view>
#include <format>
#include <filesystem>
#include <iostream>
#include <fstream>

#include <boost/algorithm/string.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

// #include <openssl/bio.h>
// #include <openssl/err.h>
// #include <openssl/pem.h>
// #include <openssl/x509.h>
// #include <openssl/x509v3.h>

#include "nextapp/logging.h"
#include "nextapp/util.h"

using namespace std;
using namespace std::string_literals;

namespace nextapp {

namespace {

void setIssuerOrg(X509 *cert, const std::string& issuerOrg) {
    if (auto ca_name = X509_NAME_new()) {

        ScopedExit cleaner{[ca_name] {
            X509_NAME_free(ca_name);
        }};

        X509_NAME_add_entry_by_txt(
            ca_name, "O", MBSTRING_ASC,
            reinterpret_cast<const unsigned char *>(issuerOrg.c_str()), -1, -1, 0);
        X509_set_issuer_name(cert, ca_name);
        return;
    }
    throw runtime_error{"X509_get_issuer_name"};
}

template <typename T>
void setSubjects(X509 *cert, const T& subjects) {
    if (auto name = X509_NAME_new()) {

        ScopedExit cleaner{[name] {
            X509_NAME_free(name);
        }};

        int loc = 0;

        for(const auto [subj, section] : subjects) {
            X509_NAME_add_entry_by_txt(
                name, section.c_str(), MBSTRING_ASC,
                reinterpret_cast<const unsigned char *>(subj.data()), subj.size(), loc++, 0);
        }

        X509_set_subject_name(cert, name);
        return;
    }
    throw runtime_error{"X509_get_subject_name"};
}

// auto openFile(const std::filesystem::path& path) {

//     struct Closer {
//         void operator()(FILE *fp) const {
//             fclose(fp);
//         }
//     };

//     if (auto fp = fopen(path.c_str(), "wb")) {
//         return unique_ptr<FILE, Closer>(fp);
//     }

//     const auto why = strerror(errno);

//     throw runtime_error{format("Failed to open file {} for binary write: {}",
//                                path.string(), why)};
// }

// auto openFile(std::filesystem::path path, const std::string& name) {
//     path /= name;

//     LOG_DEBUG << "Creating file: " << path;
//     return openFile(path);
// }


// https://stackoverflow.com/questions/60476336/programmatically-generate-a-ca-certificate-with-openssl-in-c
void addExt(X509 *cert, int nid, const char *value)
{
    X509V3_CTX ctx = {};

    /* This sets the 'context' of the extensions. */
    /* No configuration database */
    X509V3_set_ctx_nodb(&ctx);

    /* Issuer and subject certs: both the target since it is self signed,
     * no request and no CRL
     */
    X509V3_set_ctx(&ctx, cert, cert, NULL, NULL, 0);
    if (auto ex = X509V3_EXT_conf_nid(NULL, &ctx, nid, value)) {
        X509_add_ext(cert,ex,-1);
        X509_EXTENSION_free(ex);
    } else {
        throw runtime_error{"X509V3_EXT_conf_nid failed."};
    }
}

// struct X509_Deleter {
//     void operator()(X509* x) const {
//         X509_free(x);
//     }
// };

// using X509_Ptr = std::unique_ptr<X509, X509_Deleter>;

// struct EVP_PKEY_Deleter {
//     void operator()(EVP_PKEY* p) const { EVP_PKEY_free(p); }
// };

// using EVP_PKEY_Ptr = std::unique_ptr<EVP_PKEY, EVP_PKEY_Deleter>;

template <typename T, IntOrUUID U>
std::pair<X509_Ptr, EVP_PKEY_Ptr> createCert(
    const std::string& caName,
    const U serial,
    unsigned lifetimeDays,
    unsigned keyBytes,
    const T& subjects) {

    if (auto cert = X509_new()) {

        X509_Ptr rcert{cert};

        // Set cert serial
        if constexpr (std::is_same_v<U, int> || std::is_same_v<U, long>) {
            ASN1_INTEGER_set(X509_get_serialNumber(cert), serial);
        } else if constexpr (is_same_v<U, boost::uuids::uuid>) {
            if (BIGNUM* bn = BN_new()) {
                ScopedExit bn_cleanup{[bn] {
                    BN_free(bn);
                }};
                BN_bin2bn(serial.data, serial.size(), bn);
                if (ASN1_INTEGER* asn1_serial = BN_to_ASN1_INTEGER(bn, nullptr)) {
                    ScopedExit asn1_serial_cleanup{[asn1_serial] {
                        ASN1_INTEGER_free(asn1_serial);
                    }};
                    X509_set_serialNumber(cert, asn1_serial);
                } else {
                    throw runtime_error{"BN_to_ASN1_INTEGER failed"};
                }
            } else {
                throw runtime_error{"BN_new failed"};
            }
        } else {
            assert(false && "Unsupported type.");
        }

        // set cert version
        X509_set_version(cert, X509_VERSION_3);

        // Set lifetime
        // From now
        X509_gmtime_adj(X509_get_notBefore(cert), 0);

        // To ...
        X509_gmtime_adj(X509_get_notAfter(cert), 60 * 60 * 24 * lifetimeDays);

        setIssuerOrg(cert, caName);

        setSubjects(cert, subjects);

        EVP_PKEY_Ptr rkey;

        // Generate and set a RSA private key
        if (auto rsa_key = EVP_PKEY_new()) {
            rkey.reset(rsa_key);

            if (auto ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL)) {
                ScopedExit ctx_cleanup{[&ctx] {
                    EVP_PKEY_CTX_free(ctx);
                }};

                EVP_PKEY_keygen_init(ctx);
                EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, keyBytes);
                if (EVP_PKEY_keygen(ctx, &rsa_key) <= 0) {
                    throw runtime_error{"Error generating EVP_PKEY_RSA key for the certificate."};
                }
            } else {
                throw runtime_error{"EVP_PKEY_CTX_new_id"};
            }

            X509_set_pubkey(cert, rsa_key);
        } else {
            throw runtime_error{"EVP_PKEY_new()"};
        }

        return make_pair(std::move(rcert), std::move(rkey));
    }

    throw runtime_error{"X509_new failed"};
}

std::pair<X509_Ptr, EVP_PKEY_Ptr> createCaCert(
    const std::string& caName,
    unsigned lifetimeDays,
    unsigned keyBytes,
    const std::filesystem::path& keyPath,
    const std::filesystem::path& certPath) {

    const array<pair<const string&, const string&>, 1> s = {
        make_pair(caName, "O"s),
    };

    auto [cert, key] = createCert(caName, 1, lifetimeDays, keyBytes, s);

    addExt(cert.get(), NID_basic_constraints, "critical,CA:TRUE");
    addExt(cert.get(), NID_key_usage, "critical,keyCertSign,cRLSign");
    addExt(cert.get(), NID_subject_key_identifier, "hash");

    if (!X509_sign(cert.get(), key.get(), EVP_sha256())) {
        throw runtime_error{"Error signing CA certificate."};
    }

    // // Write the CA cert
    // PEM_write_X509(openFile(certPath).get(), cert.get());

    // // Write the CA key, if we were given a path to it
    // if (!keyPath.empty()) {
    //     PEM_write_PrivateKey(openFile(keyPath).get(),
    //                          key.get(), NULL, NULL, 0, 0, NULL);
    // }

    return make_pair(std::move(cert), std::move(key));
}

void createClientCert(const std::string& caName,
                      const std::string& name,
                      EVP_PKEY *ca_rsa_key,
                      long serial,
                      unsigned lifetimeDays,
                      unsigned keyBytes,
                      const std::filesystem::path& keyPath,
                      const std::filesystem::path& certPath) {

    const array<pair<const string&, const string&>, 2> s = {
        make_pair(caName, "O"s),
        make_pair(name, "CN"s)
    };

    auto [cert, key] = createCert(caName, serial, lifetimeDays, keyBytes, s);

    if (!X509_sign(cert.get(), ca_rsa_key, EVP_sha256())) {
        throw runtime_error{format("Error signing client certificate {}.", serial)};
    }

    // PEM_write_PrivateKey(openFile(keyPath).get(), key.get(), NULL, NULL, 0, 0, NULL);
    // PEM_write_X509(openFile(certPath).get(), cert.get());
}

void createServerCert(const std::string& caName,
                      const std::string& subject,
                      EVP_PKEY *ca_rsa_key,
                      long serial,
                      unsigned lifetimeDays,
                      unsigned keyBytes,
                      const std::filesystem::path& keyPath,
                      const std::filesystem::path& certPath) {

    const array<pair<const string&, const string&>, 2> s = {
        make_pair(caName, "O"s),
        make_pair(subject, "CN"s)
    };

    auto [cert, key] = createCert(caName, serial, lifetimeDays, keyBytes, s);

    if (!X509_sign(cert.get(), ca_rsa_key, EVP_sha256())) {
        throw runtime_error{format("Error signing client certificate {}.", serial)};
    }

    // PEM_write_PrivateKey(openFile(keyPath).get(), key.get(), NULL, NULL, 0, 0, NULL);
    // PEM_write_X509(openFile(certPath).get(), cert.get());
}

string expand(string what, bool kindIsCert, unsigned count = 0) {
    static const array<string_view, 2> kind = {"cert", "key"};

    boost::replace_all(what, "{kind}", kind[kindIsCert ? 0 : 1]);
    boost::replace_all(what, "{count}", to_string(count));

    return what;
}

template <typename T, typename U>
void toBuffer(const T& cert, U& dest) {
    auto * bio = BIO_new(BIO_s_mem());
    if (!bio) {
        throw runtime_error{"BIO_new"};
    }

    ScopedExit scoped{ [bio] {
        BIO_free(bio); }
    };

    if constexpr (is_same_v<T, X509>) {
        PEM_write_bio_X509(bio, &cert);
    } else if constexpr (is_same_v<T, EVP_PKEY>){
        PEM_write_bio_PrivateKey(bio, &cert, NULL, NULL, 0, 0, NULL);
    } else {
        assert(false && "Unsupported type.");
        throw runtime_error{"toBuffer: Unsupported type."};
    }

    char * data{};
    if (const auto len = BIO_get_mem_data(bio, &data); len > 0) {
        dest = {data, data + len};
    } else {
        throw runtime_error{"BIO_get_mem_data"};
    }
}

// X509_Ptr loadCertFromBuffer(const auto& buffer) {
//     if (auto* bio = BIO_new_mem_buf(buffer.data(), buffer.size())) {
//         ScopedExit bio_cleanup{[bio] {
//             BIO_free(bio);
//         }};
//         if (auto cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr)) {
//             return X509_Ptr{cert};
//         } else {
//             throw runtime_error{"PEM_read_bio_X509"};
//         }
//     } else {
//         throw runtime_error{"BIO_new_mem_buf"};
//     }
// }

// EVP_PKEY_Ptr loadKeyFromBuffer(const auto& buffer) {
//     if (auto* bio = BIO_new_mem_buf(buffer.data(), buffer.size())) {
//         ScopedExit bio_cleanup{[bio] {
//             BIO_free(bio);
//         }};
//         if (auto key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr)) {
//             return EVP_PKEY_Ptr{key};
//         } else {
//             throw runtime_error{"PEM_read_bio_X509"};
//         }
//     } else {
//         throw runtime_error{"BIO_new_mem_buf"};
//     }
// }

string getSubject(const X509& cert) {
    if (auto sname = X509_get_subject_name(&cert)) {
        if (auto *name = X509_NAME_oneline(sname, nullptr, 0)) {
            ScopedExit name_cleanup{[name] {
                OPENSSL_free(name);
            }};
            string rval{name};
            return rval;
        } else {
            throw runtime_error{"X509_NAME_oneline"};
        }
    } else {
        throw runtime_error{"X509_get_subject_name"};
    }
}

} // anon ns


CertData createCaCert(
    const std::string& caName,
    unsigned lifetimeDays,
    unsigned keyBytes) {

    const array<pair<const string&, const string&>, 1> s = {
        make_pair(caName, "O"s),
    };

    auto [cert, key] = createCert(caName, 1, lifetimeDays, keyBytes, s);

    addExt(cert.get(), NID_basic_constraints, "critical,CA:TRUE");
    addExt(cert.get(), NID_key_usage, "critical,keyCertSign,cRLSign");
    addExt(cert.get(), NID_subject_key_identifier, "hash");

    if (!X509_sign(cert.get(), key.get(), EVP_sha256())) {
        throw runtime_error{"Error signing CA certificate."};
    }

    CertData data;
    toBuffer(*cert, data.cert);
    toBuffer(*key, data.key);

    return data;
}

CertAuthority::CertAuthority(const CertData& rootCaCert, const CaOptions &options)
: options_{options}
{
    rootCa_ = loadCertFromBuffer(rootCaCert.cert);
    rootKey_ = loadKeyFromBuffer(rootCaCert.key);
    ca_name_ = getSubject(*rootCa_);
    root_cert_ = rootCaCert.cert;
}

CertData CertAuthority::createServerCert(const std::string &subject)
{
    const array<pair<const string&, const string&>, 2> s = {
        make_pair(ca_name_, "O"s),
        make_pair(subject, "CN"s)
    };

    CertData rval;
    rval.id = newUuid();

    auto [cert, key] = createCert(ca_name_, rval.id, options_.lifetime_days_certs, options_.key_bytes, s);

    if (!X509_sign(cert.get(), rootKey_.get(), EVP_sha256())) {
        throw runtime_error{format("Error signing server certificate {}.",
                                   boost::uuids::to_string(rval.id))};
    }

    toBuffer(*cert, rval.cert);
    toBuffer(*key, rval.key);

    LOG_DEBUG_N << "Created server cert for " << subject << " with id " << rval.id
                << " and " <<  options_.lifetime_days_certs << " days lifetime.";
    return rval;
}

CertData CertAuthority::createClientCert(const std::string &subject)
{
    const array<pair<const string&, const string&>, 2> s = {
        make_pair(ca_name_, "O"s),
        make_pair(subject, "CN"s)
    };

    CertData rval;
    rval.id = newUuid();
    auto [cert, key] = createCert(ca_name_, rval.id, options_.lifetime_days_certs, options_.key_bytes, s);

    if (!X509_sign(cert.get(), rootKey_.get(), EVP_sha256())) {
        throw runtime_error{format("Error signing client certificate {}.",
                                   boost::uuids::to_string(rval.id))};
    }

    toBuffer(*cert, rval.cert);
    toBuffer(*key, rval.key);

    LOG_DEBUG_N << "Created client cert for " << subject << " with id " << rval.id
                << " and " <<  options_.lifetime_days_certs << " days lifetime.";
    return rval;
}

} // ns

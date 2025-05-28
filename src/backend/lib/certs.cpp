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

template <typename T>
void setIssuerOrg(T *cert, const std::string& issuerOrg) {
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
            const string sect{section};
            X509_NAME_add_entry_by_txt(
                name, sect.c_str(), MBSTRING_ASC,
                reinterpret_cast<const unsigned char *>(subj.data()), subj.size(), loc++, 0);
        }

        X509_set_subject_name(cert, name);
        return;
    }
    throw runtime_error{"X509_get_subject_name"};
}

string getTextField(const X509& cert, int nid) {
    if (auto name = X509_get_subject_name(&cert)) {
        array<char, 4096> buf{};
        if (auto res = X509_NAME_get_text_by_NID(name, nid, buf.data(), buf.size()); res >= 0) {
            return string{buf.data()};
        } else {
            throw runtime_error{"X509_NAME_get_text_by_NID"};
        }
    } else {
        throw runtime_error{"X509_get_subject_name"};
    }
}

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

template <typename T, IntOrUUID U>
void setSerial(T *cert, const U serial) {
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
}


template <typename T, IntOrUUID U>
std::pair<X509_Ptr, EVP_PKEY_Ptr> createCert(
    const X509_NAME* issuer,
    const U serial,
    unsigned lifetimeDays,
    unsigned keyBytes,
    const T& subjects) {

    if (auto cert = X509_new()) {

        X509_Ptr rcert{cert};

        // Set cert serial
        setSerial(cert, serial);

        // set cert version
        X509_set_version(cert, X509_VERSION_3);

        // Set lifetime
        // From now
        X509_gmtime_adj(X509_get_notBefore(cert), 0);

        // To ...
        X509_gmtime_adj(X509_get_notAfter(cert), 60 * 60 * 24 * lifetimeDays);

        if (issuer) {
            X509_set_issuer_name(cert, issuer);
        }

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

    const array<pair<string, string>, 1> s = {
        make_pair(caName, "O"s),
    };

    auto [cert, key] = createCert({}, 1, lifetimeDays, keyBytes, s);

    addExt(cert.get(), NID_basic_constraints, "critical,CA:TRUE");
    addExt(cert.get(), NID_key_usage, "critical,keyCertSign,cRLSign");
    addExt(cert.get(), NID_subject_key_identifier, "hash");

    if (auto name = X509_get_subject_name(cert.get())) {
        X509_set_issuer_name(cert.get(), name);
    } else {
        throw runtime_error{"X509_get_subject_name"};
    }

    if (!X509_sign(cert.get(), key.get(), EVP_sha256())) {
        throw runtime_error{"Error signing CA certificate."};
    }

    return make_pair(std::move(cert), std::move(key));
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

string getSubject(const X509& cert) {
    if (auto sname = X509_get_subject_name(&cert)) {
        array<char, 4096> buf{};
        if (auto *name = X509_NAME_oneline(sname, buf.data(), buf.size())) {
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

    const array<pair<const string, const string>, 2> s = {
        make_pair(caName, "O"s),
        {"nextappd"s, "OU"s}
    };

    auto [cert, key] = createCert({}, 1, lifetimeDays, keyBytes, s);

    addExt(cert.get(), NID_basic_constraints, "critical,CA:TRUE");
    addExt(cert.get(), NID_key_usage, "critical,keyCertSign,cRLSign");
    addExt(cert.get(), NID_subject_key_identifier, "hash");

    if (auto name = X509_get_subject_name(cert.get())) {
        X509_set_issuer_name(cert.get(), name);
    } else {
        throw runtime_error{"X509_get_subject_name"};
    }

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
    ca_name_ = getTextField(*rootCa_, NID_organizationName);
    ca_issuer_ = X509_get_issuer_name(rootCa_.get());
    root_cert_ = rootCaCert.cert;
}

CertData CertAuthority::createServerCert(const std::vector<std::string_view>& serverSubjects)
{
    if (serverSubjects.empty()) {
        throw runtime_error{"No subjects given for server cert!"};
    }

    const array<pair<string_view, string_view>, 2> s = {
        make_pair("Nextapp", "O"s),
        make_pair(serverSubjects.front(), "CN"s)
    };

    CertData rval;
    rval.id = newUuid();

    auto [cert, key] = createCert(ca_issuer_, rval.id, options_.lifetime_days_certs, options_.key_bytes, s);

    STACK_OF(GENERAL_NAME)* san_names = sk_GENERAL_NAME_new_null();
    ScopedExit san_cleanup{[san_names] {
        sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
    }};
    for(const auto& dns : serverSubjects) {
        auto san = GENERAL_NAME_new();
        san->type = GEN_DNS;
        san->d.dNSName = ASN1_IA5STRING_new();
        ASN1_STRING_set(san->d.dNSName, dns.data(), dns.size());
        sk_GENERAL_NAME_push(san_names, san);
    }
    X509_EXTENSION* ext = X509V3_EXT_i2d(NID_subject_alt_name, 0, san_names);
    ScopedExit ext_cleanup{[ext] {
        X509_EXTENSION_free(ext);
    }};
    X509_add_ext(cert.get(), ext, -1);

    if (!X509_sign(cert.get(), rootKey_.get(), EVP_sha256())) {
        throw runtime_error{format("Error signing server certificate {}.",
                                   boost::uuids::to_string(rval.id))};
    }

    toBuffer(*cert, rval.cert);
    toBuffer(*key, rval.key);

    LOG_DEBUG_N << "Created server cert for " << serverSubjects.front() << " with id " << rval.id
                << " and " <<  options_.lifetime_days_certs << " days lifetime.";
    return rval;
}

CertData CertAuthority::createClientCert(const std::string &subject, const std::string &userUuid)
{
    const array<pair<const string, const string>, 3> s = {
        make_pair(ca_name_, "O"s),
        make_pair(subject, "CN"s),
        make_pair(userUuid, "OU"s)
    };

    CertData rval;
    rval.id = newUuid();
    auto [cert, key] = createCert(ca_issuer_, rval.id, options_.lifetime_days_certs, options_.key_bytes, s);

    if (!X509_sign(cert.get(), rootKey_.get(), EVP_sha256())) {
        throw runtime_error{format("Error signing client certificate {}.",
                                   boost::uuids::to_string(rval.id))};
    }

    toBuffer(*cert, rval.cert);
    toBuffer(*key, rval.key);

    {
        const auto hash = X509_get0_pubkey_bitstr(cert.get());
        if (hash && hash->length > 0) {
            rval.hash.assign(reinterpret_cast<const char *>(hash->data), hash->length);
        } else {
            throw runtime_error{"X509_get0_pubkey_bitstr"};
        }
    }

    LOG_DEBUG_N << "Created client cert for " << subject << " with id " << rval.id
                << " and " <<  options_.lifetime_days_certs << " days lifetime.";
    return rval;
}

CertData CertAuthority::signCert(const std::string_view &csr, const std::string& cSubject, std::string *certHash)
{
    auto req = loadReqFromBuffer(csr);

    // Validate that the subject is the expected value
    if (!cSubject.empty()) {
        auto* subject_name = X509_REQ_get_subject_name(req.get());
        if (!subject_name) {
            throw runtime_error{"X509_REQ_get_subject_name"};
        }

        const int index = X509_NAME_get_index_by_NID(subject_name, NID_commonName, -1);
        if (index < 0) {
            throw runtime_error{"X509_NAME_get_index_by_NID"};
        }

        const auto* name_entry = X509_NAME_get_entry(subject_name, index);
        if (!name_entry) {
            throw runtime_error{"X509_NAME_get_entry"};
        }

        auto* ans1_str = X509_NAME_ENTRY_get_data(name_entry);
        if (!ans1_str) {
            throw runtime_error{"X509_NAME_ENTRY_get_data"};
        }

        unsigned char* utf8;
        int length = ASN1_STRING_to_UTF8(&utf8, ans1_str);
        if (length < 0) {
            throw runtime_error{"ASN1_STRING_to_UTF8"};
        }
        ScopedExit utf8_cleanup{[utf8] {
            OPENSSL_free(utf8);
        }};

        const string_view cn{reinterpret_cast<char*>(utf8), static_cast<size_t>(length)};
        if (cn != cSubject) {
            LOG_DEBUG_N << "CSR subject does not match expected value: " << cn << " != " << cSubject;
            throw runtime_error{format("CSR subject does not match expected value: {} != {}",
                                       cSubject, cn)};
        }
    }

    X509_Ptr cert{X509_new()};
    if (!cert) {
        throw runtime_error{"X509_new"};
    }

    const auto uuid = newUuid();
    X509_set_version(cert.get(), X509_VERSION_3);
    setSerial(cert.get(), uuid);
    const auto* subject = X509_REQ_get_subject_name(req.get());
    assert(subject);
    X509_set_subject_name(cert.get(), subject);

    const auto issuer = X509_get_subject_name(rootCa_.get());
    assert(issuer);
    X509_set_issuer_name(cert.get(), issuer);

    auto req_pubkey = X509_REQ_get_pubkey(req.get());
    ScopedExit pkey_clean {[req_pubkey] { EVP_PKEY_free(req_pubkey); }};
    X509_set_pubkey(cert.get(), req_pubkey);

    X509_gmtime_adj(X509_get_notBefore(cert.get()), 0);
    X509_gmtime_adj(X509_get_notAfter(cert.get()), (long)60*60*24*options_.lifetime_days_certs);

    if (!X509_sign(cert.get(), rootKey_.get(), EVP_sha256())) {
        throw runtime_error{"Error signing certificate."};
    }

    if (certHash) {
        const auto hash = X509_get0_pubkey_bitstr(cert.get());
        if (hash && hash->length > 0) {
            certHash->assign(reinterpret_cast<const char *>(hash->data), hash->length);
        } else {
            throw runtime_error{"X509_get0_pubkey_bitstr"};
        }
    }

    string rval;
    toBuffer(*cert, rval);

    return {uuid, rval, {}};
}

} // ns

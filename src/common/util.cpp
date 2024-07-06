
#include <openssl/evp.h>

#include "nextapp/util.h"
#include "nextapp/logging.h"


using namespace std;

std::ostream& operator << (std::ostream& out, const std::optional<std::string>& v) {
    if (v) {
        return out << *v;
    }

    return out << "[empty]";
}

namespace nextapp {

std::string getEnv(const char *name, std::string def) {
    if (auto var = std::getenv(name)) {
        return var;
    }

    return def;
}

string readFileToBuffer(const std::filesystem::path &path)
{
    if (!std::filesystem::exists(path)) {
        throw runtime_error{format("File {} does not exist", path.string())};
    }

    LOG_TRACE_N << "Reading file: " << path;
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        throw runtime_error{format("Faied to open file {} for read", path.string())};
    }

    auto len = std::filesystem::file_size(path);
    string b;
    b.resize(len);
    file.read(b.data(), b.size());
    return b;
}

boost::uuids::uuid newUuid()
{
    static boost::uuids::random_generator uuid_gen_;
    return uuid_gen_();
}

string newUuidStr()
{
    return boost::uuids::to_string(newUuid());
}

const string& validatedUuid(const string& uuid) {
    using namespace boost::uuids;

    try {
        auto result = string_generator()(uuid);
        return uuid;
    } catch(const runtime_error&) {

    }

    throw runtime_error{"invalid uuid"};
}



// Modified from ChatGPT generated code
string Base64Encode(const std::span<uint8_t> in)
{
    static constexpr std::string_view base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    string base64_data;
    base64_data.reserve(((in.size() + 2) / 3) * 4);

    for (std::size_t i = 0; i < in.size(); i += 3) {
        const auto a = in[i];
        const auto b = i + 1 < in.size() ? in[i + 1] : 0;
        const auto c = i + 2 < in.size() ? in[i + 2] : 0;

        const auto index1 = (a >> 2) & 0x3f;
        const auto index2 = ((a & 0x3) << 4) | ((b >> 4) & 0xf);
        const auto index3 = ((b & 0xf) << 2) | ((c >> 6) & 0x3);
        const auto index4 = c & 0x3f;

        base64_data.push_back(base64_chars[index1]);
        base64_data.push_back(base64_chars[index2]);
        base64_data.push_back(i + 1 < in.size() ? base64_chars[index3] : '=');
        base64_data.push_back(i + 2 < in.size() ? base64_chars[index4] : '=');
    }

    return base64_data;
}


// Impl. based from https://stackoverflow.com/questions/2262386/generate-sha256-with-openssl-and-c
string sha256(const std::span<uint8_t> in, bool encodeToBase64)
{
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    const ScopedExit bye{[context] {
        EVP_MD_CTX_free(context);
    }};

    if (context != nullptr) {
        if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 0) {
            if (EVP_DigestUpdate(context, in.data(), in.size()) != 0) {
                array<uint8_t, EVP_MAX_MD_SIZE> hash{};
                unsigned int lengthOfHash = 0;
                if (EVP_DigestFinal_ex(context, hash.data(), &lengthOfHash) != 0) {
                    if (encodeToBase64) {
                        return Base64Encode({hash.data(), lengthOfHash});
                    }
                    return {reinterpret_cast<const char *>(hash.data()), lengthOfHash};
                }
            }
        }
    }
    throw runtime_error{"sha256 failed!"};
}


boost::uuids::uuid toUuid(std::string_view uuid)
{
    using namespace boost::uuids;
    try {
        return string_generator()(string{uuid});
    } catch(const runtime_error&) {
        ;
    }

    throw runtime_error{"invalid uuid"};
}


} // ns

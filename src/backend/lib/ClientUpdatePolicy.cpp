#include "nextapp/config.h"
#include "nextapp/logging.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <string_view>

namespace nextapp {
namespace {

constexpr size_t kMaxVersionLength = 128;

std::string_view trimManifestValue(std::string_view value) {
    const auto first = value.find_first_not_of(" \r\n");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \r\n");
    return value.substr(first, last - first + 1);
}

std::string_view valueOf(std::string_view line) {
    const auto hash = line.find('#');
    line = trimManifestValue(line.substr(0, hash));
    const auto colon = line.find(':');
    return colon == std::string_view::npos ? std::string_view{} : trimManifestValue(line.substr(colon + 1));
}

bool parseString(std::string_view value, std::string& out) {
    value = trimManifestValue(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value.remove_prefix(1);
        value.remove_suffix(1);
    }
    if (value.empty() || value.size() > kMaxVersionLength
        || std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; })) {
        return false;
    }
    out.assign(value);
    return true;
}

std::optional<int> platformFor(std::string_view name) {
    static constexpr std::pair<std::string_view, pb::DevicePlatform> platforms[] = {
        {"linux_native_x64", pb::DevicePlatform::LINUX_NATIVE_X64},
        {"linux_flatpak_x64", pb::DevicePlatform::LINUX_FLATPAK_X64},
        {"macos_x64", pb::DevicePlatform::MACOS_X64},
        {"macos_arm64", pb::DevicePlatform::MACOS_ARM64},
        {"windows_x64", pb::DevicePlatform::WINDOWS_X64},
        {"android", pb::DevicePlatform::ANDROID},
    };
    for (const auto& [key, value] : platforms) if (key == name) return static_cast<int>(value);
    return std::nullopt;
}

struct Entry {
    std::string name;
    uint32_t version_code{};
    std::string version;
    bool required{};
    bool has_version_code{};
    bool has_version{};
};

std::optional<uint32_t> versionCodeFor(std::string_view version)
{
    uint64_t parts[3]{};
    size_t offset = 0;
    for (auto& part : parts) {
        const auto separator = version.find('.', offset);
        const auto text = version.substr(offset, separator == std::string_view::npos
            ? std::string_view::npos : separator - offset);
        const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), part);
        if (text.empty() || ec != std::errc{} || ptr != text.data() + text.size()) return std::nullopt;
        if (separator == std::string_view::npos) {
            if (&part != &parts[2]) return std::nullopt;
            offset = version.size();
        } else {
            offset = separator + 1;
        }
    }
    if (offset != version.size() || parts[1] > 99 || parts[2] > 99) return std::nullopt;
    const auto code = parts[0] * 10000 + parts[1] * 100 + parts[2];
    if (code > std::numeric_limits<uint32_t>::max()) return std::nullopt;
    return static_cast<uint32_t>(code);
}

bool finishEntry(ClientUpdatePolicy& policy, const std::optional<Entry>& entry, std::string& error) {
    if (!entry) return true;
    if (!entry->has_version_code || !entry->has_version || entry->version_code == 0) {
        error = "entry '" + entry->name + "' requires a positive version_code and a non-empty version";
        return false;
    }
    if (const auto derived = versionCodeFor(entry->version)) {
        if (*derived != entry->version_code) {
            LOG_WARN_N << "Client update manifest entry '" << entry->name << "' has version_code "
                       << entry->version_code << " but version '" << entry->version
                       << "' derives to " << *derived;
        }
    } else {
        LOG_WARN_N << "Client update manifest entry '" << entry->name << "' uses version '"
                   << entry->version << "', which cannot be converted to a version_code.";
    }
    pb::ClientUpdate update;
    update.set_version_code(entry->version_code);
    update.set_version(entry->version);
    update.set_required(entry->required);
    return policy.add(entry->name, std::move(update), error);
}

} // namespace

std::optional<pb::ClientUpdate> ClientUpdatePolicy::forDevice(const pb::DeviceInfo& device) const {
    const auto platform = static_cast<int>(device.deviceplatform());
    if (platform == static_cast<int>(pb::DevicePlatform::UNKNOWN_PLATFORM)) return std::nullopt;
    if (const auto it = entries_.find(platform); it != entries_.end()) return it->second;
    return default_;
}

bool ClientUpdatePolicy::add(std::string_view name, pb::ClientUpdate update, std::string& error) {
    if (name == "default") {
        if (default_) {
            error = "duplicate client update platform 'default'";
            return false;
        }
        default_ = std::move(update);
        return true;
    }
    const auto platform = platformFor(name);
    if (!platform) {
        error = "unknown client update platform '" + std::string(name) + "'";
        return false;
    }
    if (!entries_.emplace(*platform, std::move(update)).second) {
        error = "duplicate client update platform '" + std::string(name) + "'";
        return false;
    }
    return true;
}

std::optional<ClientUpdatePolicy> ClientUpdatePolicy::load(const std::filesystem::path& path,
                                                            std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "cannot open manifest '" + path.string() + "'";
        return std::nullopt;
    }

    ClientUpdatePolicy policy;
    std::optional<Entry> entry;
    bool in_updates = false;
    std::string line;
    size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto content = trimManifestValue(line);
        if (content.empty() || content.front() == '#') continue;
        const auto indent = line.find_first_not_of(' ');
        if (indent == std::string::npos || line.find('\t') != std::string::npos) {
            error = "line " + std::to_string(line_number) + ": tabs are not supported";
            return std::nullopt;
        }
        if (indent == 0) {
            if (content != "client_updates:") {
                error = "line " + std::to_string(line_number) + ": expected client_updates:";
                return std::nullopt;
            }
            in_updates = true;
            continue;
        }
        if (!in_updates) {
            error = "line " + std::to_string(line_number) + ": missing client_updates root";
            return std::nullopt;
        }
        if (indent == 2 && content.ends_with(':')) {
            if (!finishEntry(policy, entry, error)) return std::nullopt;
            entry = Entry{std::string{content.substr(0, content.size() - 1)}};
            continue;
        }
        if (indent != 4 || !entry) {
            error = "line " + std::to_string(line_number) + ": invalid manifest indentation";
            return std::nullopt;
        }
        const auto colon = content.find(':');
        if (colon == std::string_view::npos) {
            error = "line " + std::to_string(line_number) + ": expected key: value";
            return std::nullopt;
        }
        const auto key = trimManifestValue(content.substr(0, colon));
        const auto value = valueOf(content);
        if (key == "version_code") {
            uint64_t parsed{};
            const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (ec != std::errc{} || ptr != value.data() + value.size() || parsed == 0
                || parsed > std::numeric_limits<uint32_t>::max()) {
                error = "line " + std::to_string(line_number) + ": invalid version_code";
                return std::nullopt;
            }
            entry->version_code = static_cast<uint32_t>(parsed);
            entry->has_version_code = true;
        } else if (key == "version") {
            if (!parseString(value, entry->version)) {
                error = "line " + std::to_string(line_number) + ": invalid version";
                return std::nullopt;
            }
            entry->has_version = true;
        } else if (key == "required") {
            if (value == "true") entry->required = true;
            else if (value == "false") entry->required = false;
            else {
                error = "line " + std::to_string(line_number) + ": required must be true or false";
                return std::nullopt;
            }
        } else {
            error = "line " + std::to_string(line_number) + ": unknown entry key '" + std::string(key) + "'";
            return std::nullopt;
        }
    }
    if (!in_updates || !finishEntry(policy, entry, error) || policy.empty()) {
        if (error.empty()) error = "manifest does not contain a client update policy";
        return std::nullopt;
    }
    return policy;
}

} // namespace nextapp

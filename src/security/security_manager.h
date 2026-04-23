#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>

#include "common/types.h"
#include "config/settings.h"
#include "security/path_sanitizer.h"
#include "security/rate_limiter.h"

namespace lan_transfer {

class SecurityManager {
public:
    SecurityManager(const std::filesystem::path& storage_directory,
                    const std::filesystem::path& download_directory,
                    const AppSettings& settings);

    const PathSanitizer& path_sanitizer() const;
    std::uint64_t max_file_size_bytes() const;

    void enforce_pair_request_allowed(const PairRequest& request, const std::string& peer_host);
    void enforce_file_request_allowed(const FileRequest& request, const std::string& peer_host);

private:
    void load_blacklist();
    bool is_blacklisted(const std::string& device_id, const std::string& peer_host) const;
    static std::string build_rate_limit_key(const std::string& device_id, const std::string& peer_host);
    static std::string extract_json_array(const std::string& content, const std::string& key);

    std::filesystem::path blacklist_path_;
    PathSanitizer path_sanitizer_;
    RateLimiter pair_request_rate_limiter_;
    RateLimiter file_request_rate_limiter_;
    std::unordered_set<std::string> blacklisted_device_ids_;
    std::unordered_set<std::string> blacklisted_hosts_;
    std::uint64_t max_file_size_bytes_ = AppSettings{}.max_file_size_bytes;
};

}  // namespace lan_transfer

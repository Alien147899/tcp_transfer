#include "security/security_manager.h"

#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace lan_transfer {

namespace {

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open file: " + path.string());
    }

    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

}  // namespace

SecurityManager::SecurityManager(const std::filesystem::path& storage_directory,
                                 const std::filesystem::path& download_directory,
                                 const AppSettings& settings)
    : blacklist_path_(storage_directory / "blacklist.json"),
      path_sanitizer_(download_directory),
      max_file_size_bytes_(settings.max_file_size_bytes) {
    load_blacklist();
}

const PathSanitizer& SecurityManager::path_sanitizer() const {
    return path_sanitizer_;
}

std::uint64_t SecurityManager::max_file_size_bytes() const {
    return max_file_size_bytes_;
}

void SecurityManager::enforce_pair_request_allowed(const PairRequest& request, const std::string& peer_host) {
    if (is_blacklisted(request.requester_device_id, peer_host)) {
        throw std::runtime_error("pair request rejected because sender is blacklisted");
    }
    if (!pair_request_rate_limiter_.allow(build_rate_limit_key(request.requester_device_id, peer_host),
                                          3,
                                          std::chrono::seconds(30))) {
        throw std::runtime_error("pair request rejected due to rate limit");
    }
}

void SecurityManager::enforce_file_request_allowed(const FileRequest& request, const std::string& peer_host) {
    if (is_blacklisted(request.sender_device_id, peer_host)) {
        throw std::runtime_error("file request rejected because sender is blacklisted");
    }
    if (request.file_size > max_file_size_bytes_) {
        throw std::runtime_error("file request exceeds maximum allowed size");
    }
    if (!file_request_rate_limiter_.allow(build_rate_limit_key(request.sender_device_id, peer_host),
                                          5,
                                          std::chrono::seconds(30))) {
        throw std::runtime_error("file request rejected due to rate limit");
    }
}

void SecurityManager::load_blacklist() {
    blacklisted_device_ids_.clear();
    blacklisted_hosts_.clear();
    if (!std::filesystem::exists(blacklist_path_)) {
        return;
    }

    const std::string content = read_text_file(blacklist_path_);
    const std::regex string_pattern("\"([^\"]+)\"");

    const std::string device_ids_json = extract_json_array(content, "device_ids");
    for (std::sregex_iterator it(device_ids_json.begin(), device_ids_json.end(), string_pattern), end; it != end;
         ++it) {
        blacklisted_device_ids_.insert((*it)[1].str());
    }

    const std::string hosts_json = extract_json_array(content, "hosts");
    for (std::sregex_iterator it(hosts_json.begin(), hosts_json.end(), string_pattern), end; it != end; ++it) {
        blacklisted_hosts_.insert((*it)[1].str());
    }
}

bool SecurityManager::is_blacklisted(const std::string& device_id, const std::string& peer_host) const {
    return (!device_id.empty() && blacklisted_device_ids_.count(device_id) > 0) ||
           (!peer_host.empty() && blacklisted_hosts_.count(peer_host) > 0);
}

std::string SecurityManager::build_rate_limit_key(const std::string& device_id, const std::string& peer_host) {
    if (!device_id.empty()) {
        return "device:" + device_id;
    }
    return "host:" + peer_host;
}

std::string SecurityManager::extract_json_array(const std::string& content, const std::string& key) {
    const std::regex pattern("\"" + key + "\"\\s*:\\s*\\[(.*?)\\]", std::regex::icase);
    std::smatch match;
    if (!std::regex_search(content, match, pattern)) {
        return {};
    }
    return match[1].str();
}

}  // namespace lan_transfer

#include "config/settings.h"

#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace lan_transfer {

namespace {

constexpr std::uint64_t kMinFileSizeLimitBytes = 1ULL;

}

AppSettings SettingsLoader::load(const std::filesystem::path& storage_directory) {
    const std::filesystem::path settings_path = storage_directory / "settings.json";
    AppSettings settings;
    if (!std::filesystem::exists(settings_path)) {
        return settings;
    }

    const std::string content = read_text_file(settings_path);
    const std::uint64_t configured_limit = extract_uint64(content, "max_file_size_bytes");
    if (configured_limit < kMinFileSizeLimitBytes) {
        throw std::runtime_error("settings.json max_file_size_bytes must be greater than 0");
    }

    settings.max_file_size_bytes = configured_limit;
    return settings;
}

std::string SettingsLoader::read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open file: " + path.string());
    }

    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

std::uint64_t SettingsLoader::extract_uint64(const std::string& content, const std::string& key) {
    const std::regex pattern("\"" + key + "\"\\s*:\\s*([0-9]+)");
    std::smatch match;
    if (!std::regex_search(content, match, pattern)) {
        return AppSettings{}.max_file_size_bytes;
    }

    return std::stoull(match[1].str());
}

}  // namespace lan_transfer

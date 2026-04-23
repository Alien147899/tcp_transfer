#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace lan_transfer {

struct AppSettings {
    std::uint64_t max_file_size_bytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;
};

class SettingsLoader {
public:
    static AppSettings load(const std::filesystem::path& storage_directory);

private:
    static std::string read_text_file(const std::filesystem::path& path);
    static std::uint64_t extract_uint64(const std::string& content, const std::string& key);
};

}  // namespace lan_transfer

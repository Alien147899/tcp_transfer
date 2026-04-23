#pragma once

#include <filesystem>
#include <string>

namespace lan_transfer {

class PathSanitizer {
public:
    explicit PathSanitizer(std::filesystem::path download_directory);

    const std::filesystem::path& download_directory() const;
    std::string sanitize_file_name(const std::string& file_name) const;
    std::filesystem::path build_output_path(const std::string& file_name) const;
    std::filesystem::path build_temp_path(const std::string& file_name) const;

private:
    std::filesystem::path download_directory_;
};

}  // namespace lan_transfer

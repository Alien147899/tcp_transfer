#pragma once

#include <filesystem>
#include <string>

namespace lan_transfer {

std::string sanitize_file_name(const std::string& file_name);
std::filesystem::path build_output_path(const std::filesystem::path& directory,
                                        const std::string& file_name);

}  // namespace lan_transfer

#include "common/file_utils.h"

#include <stdexcept>

namespace lan_transfer {

std::string sanitize_file_name(const std::string& file_name) {
    std::filesystem::path input_path(file_name);
    std::string base_name = input_path.filename().string();
    if (base_name.empty() || base_name == "." || base_name == "..") {
        throw std::runtime_error("invalid file name received");
    }
    return base_name;
}

std::filesystem::path build_output_path(const std::filesystem::path& directory,
                                        const std::string& file_name) {
    return directory / sanitize_file_name(file_name);
}

}  // namespace lan_transfer

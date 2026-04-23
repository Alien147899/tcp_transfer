#include "security/path_sanitizer.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace lan_transfer {

namespace {

bool is_illegal_windows_char(char ch) {
    switch (ch) {
    case '<':
    case '>':
    case ':':
    case '"':
    case '/':
    case '\\':
    case '|':
    case '?':
    case '*':
        return true;
    default:
        return false;
    }
}

std::string trim_trailing_dots_and_spaces(std::string value) {
    while (!value.empty() && (value.back() == '.' || value.back() == ' ')) {
        value.pop_back();
    }
    return value;
}

}  // namespace

PathSanitizer::PathSanitizer(std::filesystem::path download_directory)
    : download_directory_(std::filesystem::absolute(std::move(download_directory))) {
    std::filesystem::create_directories(download_directory_);
}

const std::filesystem::path& PathSanitizer::download_directory() const {
    return download_directory_;
}

std::string PathSanitizer::sanitize_file_name(const std::string& file_name) const {
    if (file_name.empty()) {
        throw std::runtime_error("received empty file name");
    }

    const std::filesystem::path input_path(file_name);
    if (input_path.is_absolute() || input_path.has_parent_path() || input_path.has_root_path()) {
        throw std::runtime_error("received file name contains an illegal path");
    }

    std::string sanitized;
    sanitized.reserve(file_name.size());
    for (char ch : file_name) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (std::iscntrl(byte) || is_illegal_windows_char(ch)) {
            sanitized.push_back('_');
            continue;
        }
        sanitized.push_back(ch);
    }

    sanitized = trim_trailing_dots_and_spaces(sanitized);
    if (sanitized.empty() || sanitized == "." || sanitized == "..") {
        throw std::runtime_error("received file name is invalid after sanitization");
    }

    return sanitized;
}

std::filesystem::path PathSanitizer::build_output_path(const std::string& file_name) const {
    const std::filesystem::path output_path = download_directory_ / sanitize_file_name(file_name);
    const std::filesystem::path normalized_parent = std::filesystem::weakly_canonical(output_path.parent_path());
    const std::filesystem::path normalized_root = std::filesystem::weakly_canonical(download_directory_);
    if (normalized_parent != normalized_root) {
        throw std::runtime_error("resolved output path escapes download directory");
    }
    return output_path;
}

std::filesystem::path PathSanitizer::build_temp_path(const std::string& file_name) const {
    const std::filesystem::path final_path = build_output_path(file_name);
    return final_path.string() + ".part";
}

}  // namespace lan_transfer

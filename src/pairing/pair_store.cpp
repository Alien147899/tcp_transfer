#include "pairing/pair_store.h"

#include <fstream>
#include <cstdlib>
#include <random>
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

void write_text_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("failed to write file: " + path.string());
    }
    output << content;
    if (!output) {
        throw std::runtime_error("failed to flush file: " + path.string());
    }
}

std::string read_environment_variable(const char* name) {
#ifdef _WIN32
    char* buffer = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr) {
        return {};
    }
    std::string value(buffer);
    free(buffer);
    return value;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string();
#endif
}

}  // namespace

PairStore::PairStore(const std::filesystem::path& storage_directory)
    : device_info_path_(storage_directory / "device_info.json"),
      paired_devices_path_(storage_directory / "paired_devices.json") {
    std::filesystem::create_directories(storage_directory);
}

std::string PairStore::get_or_create_local_device_id() {
    return get_or_create_local_device_info().device_id;
}

LocalDeviceInfo PairStore::get_or_create_local_device_info() {
    if (std::filesystem::exists(device_info_path_)) {
        const std::string content = read_text_file(device_info_path_);
        LocalDeviceInfo info;
        info.device_id = extract_json_string(content, "device_id");
        info.device_name = extract_json_string(content, "device_name");
        if (info.device_id.empty()) {
            throw std::runtime_error("device_info.json does not contain a valid device_id");
        }
        if (info.device_name.empty()) {
            info.device_name = determine_default_device_name();
            const std::string migrated_content = "{\n  \"device_id\": \"" + escape_json(info.device_id) +
                                                 "\",\n  \"device_name\": \"" +
                                                 escape_json(info.device_name) + "\"\n}\n";
            write_text_file(device_info_path_, migrated_content);
        }
        return info;
    }

    const LocalDeviceInfo info{generate_device_id(), determine_default_device_name()};
    const std::string content = "{\n  \"device_id\": \"" + escape_json(info.device_id) +
                                "\",\n  \"device_name\": \"" + escape_json(info.device_name) + "\"\n}\n";
    write_text_file(device_info_path_, content);
    return info;
}

std::vector<PairedDevice> PairStore::load_paired_devices() const {
    std::vector<PairedDevice> devices;
    if (!std::filesystem::exists(paired_devices_path_)) {
        return devices;
    }

    const std::string content = read_text_file(paired_devices_path_);
    const std::regex object_pattern(
        R"json(\{\s*"device_id"\s*:\s*"([^"]+)"\s*,\s*"host"\s*:\s*"([^"]*)"\s*,\s*"port"\s*:\s*([0-9]+)\s*\})json");

    for (std::sregex_iterator it(content.begin(), content.end(), object_pattern), end; it != end; ++it) {
        PairedDevice device;
        device.device_id = (*it)[1].str();
        device.host = (*it)[2].str();
        const unsigned long port_value = std::stoul((*it)[3].str());
        if (port_value > 65535UL) {
            throw std::runtime_error("paired_devices.json contains an invalid port");
        }
        device.port = static_cast<std::uint16_t>(port_value);
        devices.push_back(device);
    }

    return devices;
}

bool PairStore::is_device_paired(const std::string& device_id) const {
    return find_paired_device(device_id).has_value();
}

std::optional<PairedDevice> PairStore::find_paired_device(const std::string& device_id) const {
    const auto devices = load_paired_devices();
    for (const auto& device : devices) {
        if (device.device_id == device_id) {
            return device;
        }
    }
    return std::nullopt;
}

void PairStore::upsert_paired_device(const PairedDevice& device) {
    if (device.device_id.empty()) {
        throw std::runtime_error("paired device_id cannot be empty");
    }

    auto devices = load_paired_devices();
    bool updated = false;
    for (auto& existing : devices) {
        if (existing.device_id == device.device_id) {
            if (!device.host.empty()) {
                existing.host = device.host;
            }
            if (device.port != 0) {
                existing.port = device.port;
            }
            updated = true;
            break;
        }
    }

    if (!updated) {
        devices.push_back(device);
    }

    std::ostringstream output;
    output << "{\n  \"devices\": [\n";
    for (std::size_t index = 0; index < devices.size(); ++index) {
        const auto& entry = devices[index];
        output << "    {\n"
               << "      \"device_id\": \"" << escape_json(entry.device_id) << "\",\n"
               << "      \"host\": \"" << escape_json(entry.host) << "\",\n"
               << "      \"port\": " << entry.port << "\n"
               << "    }";
        if (index + 1 < devices.size()) {
            output << ",";
        }
        output << "\n";
    }
    output << "  ]\n}\n";

    write_text_file(paired_devices_path_, output.str());
}

std::string PairStore::generate_device_id() {
    std::random_device random_device;
    std::mt19937 generator(random_device());
    std::uniform_int_distribution<int> distribution(0, 15);

    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::string device_id = "dev-";
    device_id.reserve(36);
    for (int i = 0; i < 32; ++i) {
        device_id.push_back(kHexDigits[distribution(generator)]);
    }
    return device_id;
}

std::string PairStore::determine_default_device_name() {
    const std::string computer_name = read_environment_variable("COMPUTERNAME");
    if (!computer_name.empty()) {
        return computer_name;
    }

    const std::string host_name = read_environment_variable("HOSTNAME");
    if (!host_name.empty()) {
        return host_name;
    }
    return "LAN Device";
}

std::string PairStore::escape_json(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

std::string PairStore::extract_json_string(const std::string& content, const std::string& key) {
    const std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
    std::smatch match;
    if (!std::regex_search(content, match, pattern)) {
        return {};
    }
    return match[1].str();
}

}  // namespace lan_transfer

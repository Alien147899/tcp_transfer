#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "common/types.h"

namespace lan_transfer {

class PairStore {
public:
    explicit PairStore(const std::filesystem::path& storage_directory);

    LocalDeviceInfo get_or_create_local_device_info();
    std::string get_or_create_local_device_id();
    std::vector<PairedDevice> load_paired_devices() const;
    bool is_device_paired(const std::string& device_id) const;
    std::optional<PairedDevice> find_paired_device(const std::string& device_id) const;
    void upsert_paired_device(const PairedDevice& device);

private:
    std::filesystem::path device_info_path_;
    std::filesystem::path paired_devices_path_;

    static std::string generate_device_id();
    static std::string determine_default_device_name();
    static std::string escape_json(const std::string& value);
    static std::string extract_json_string(const std::string& content, const std::string& key);
};

}  // namespace lan_transfer

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "config/settings.h"
#include "pairing/pair_store.h"

namespace lan_transfer {

class FileSender {
public:
    FileSender(PairStore& pair_store, const AppSettings& settings);

    void send_file(const std::string& host,
                   std::uint16_t port,
                   const std::string& target_device_id,
                   const std::filesystem::path& file_path) const;

private:
    PairStore& pair_store_;
    std::uint64_t max_file_size_bytes_;
};

}  // namespace lan_transfer

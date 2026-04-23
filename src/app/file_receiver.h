#pragma once

#include <cstdint>
#include <filesystem>

#include "pairing/pair_store.h"
#include "security/security_manager.h"

namespace lan_transfer {

class FileReceiver {
public:
    FileReceiver(PairStore& pair_store, SecurityManager& security_manager);

    void run_server(std::uint16_t port, const std::filesystem::path& output_directory) const;

private:
    PairStore& pair_store_;
    SecurityManager& security_manager_;
};

}  // namespace lan_transfer

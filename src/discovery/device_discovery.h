#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "common/types.h"
#include "pairing/pair_store.h"

namespace lan_transfer {

class DeviceDiscoveryService {
public:
    explicit DeviceDiscoveryService(PairStore& pair_store);

    std::vector<DiscoveredDevice> discover(std::uint16_t listen_port,
                                           std::chrono::seconds duration,
                                           std::chrono::milliseconds broadcast_interval =
                                               std::chrono::milliseconds(1000)) const;

private:
    PairStore& pair_store_;
};

}  // namespace lan_transfer

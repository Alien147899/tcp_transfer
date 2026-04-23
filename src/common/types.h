#pragma once

#include <cstdint>
#include <chrono>
#include <string>

namespace lan_transfer {

struct LocalDeviceInfo {
    std::string device_id;
    std::string device_name;
};

struct PairedDevice {
    std::string device_id;
    std::string host;
    std::uint16_t port = 0;
};

struct FileMetadata {
    std::string sender_device_id;
    std::string recipient_device_id;
    std::string file_name;
    std::uint64_t file_size = 0;
};

struct FileRequest {
    std::string sender_device_id;
    std::string recipient_device_id;
    std::string file_name;
    std::uint64_t file_size = 0;
};

struct FileAccept {
    std::string recipient_device_id;
};

struct DiscoveredDevice {
    std::string device_id;
    std::string device_name;
    std::string host;
    std::uint16_t listen_port = 0;
    bool paired = false;
    std::chrono::steady_clock::time_point last_seen;
};

struct PairRequest {
    std::string requester_device_id;
};

struct PairAccept {
    std::string accepter_device_id;
};

struct PairFinalize {
    std::string requester_device_id;
    std::string accepter_device_id;
};

}  // namespace lan_transfer

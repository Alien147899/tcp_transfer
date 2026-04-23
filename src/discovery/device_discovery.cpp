#include "discovery/device_discovery.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace lan_transfer {

namespace {

#ifdef _WIN32
using PlatformSocket = SOCKET;
constexpr PlatformSocket kInvalidSocket = INVALID_SOCKET;
#else
using PlatformSocket = int;
constexpr PlatformSocket kInvalidSocket = -1;
#endif

constexpr std::uint16_t kDiscoveryPort = 38561;
constexpr std::size_t kMaxDiscoveryDatagramSize = 1024;

void close_socket(PlatformSocket handle) {
    if (handle == kInvalidSocket) {
        return;
    }
#ifdef _WIN32
    closesocket(handle);
#else
    close(handle);
#endif
}

std::runtime_error make_discovery_error(const std::string& message) {
#ifdef _WIN32
    return std::runtime_error(message + " (winsock error " + std::to_string(WSAGetLastError()) + ")");
#else
    return std::runtime_error(message + ": " + std::strerror(errno));
#endif
}

struct SocketGuard {
    PlatformSocket handle = kInvalidSocket;

    ~SocketGuard() {
        close_socket(handle);
    }
};

void set_broadcast_option(PlatformSocket socket_handle) {
    int broadcast = 1;
    if (setsockopt(socket_handle,
                   SOL_SOCKET,
                   SO_BROADCAST,
                   reinterpret_cast<const char*>(&broadcast),
                   static_cast<int>(sizeof(broadcast))) != 0) {
        throw make_discovery_error("failed to enable UDP broadcast");
    }
}

void set_reuse_addr_option(PlatformSocket socket_handle) {
    int reuse_addr = 1;
    if (setsockopt(socket_handle,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse_addr),
                   static_cast<int>(sizeof(reuse_addr))) != 0) {
        throw make_discovery_error("failed to enable address reuse");
    }
}

void set_receive_timeout(PlatformSocket socket_handle, std::chrono::milliseconds timeout) {
#ifdef _WIN32
    const DWORD timeout_ms = static_cast<DWORD>(timeout.count());
    if (setsockopt(socket_handle,
                   SOL_SOCKET,
                   SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout_ms),
                   static_cast<int>(sizeof(timeout_ms))) != 0) {
        throw make_discovery_error("failed to set UDP receive timeout");
    }
#else
    timeval tv{};
    tv.tv_sec = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
    if (setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
        throw make_discovery_error("failed to set UDP receive timeout");
    }
#endif
}

std::string escape_field(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        if (ch == '\\' || ch == '|') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::string unescape_field(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    bool escaping = false;
    for (char ch : value) {
        if (escaping) {
            result.push_back(ch);
            escaping = false;
            continue;
        }
        if (ch == '\\') {
            escaping = true;
            continue;
        }
        result.push_back(ch);
    }
    if (escaping) {
        throw std::runtime_error("discovery payload ends with dangling escape");
    }
    return result;
}

std::vector<std::string> split_fields(const std::string& payload) {
    std::vector<std::string> fields;
    std::string current;
    bool escaping = false;
    for (char ch : payload) {
        if (escaping) {
            current.push_back(ch);
            escaping = false;
            continue;
        }
        if (ch == '\\') {
            escaping = true;
            continue;
        }
        if (ch == '|') {
            fields.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    if (escaping) {
        throw std::runtime_error("discovery payload ends with dangling escape");
    }
    fields.push_back(current);
    return fields;
}

std::string build_discovery_payload(const LocalDeviceInfo& local_device, std::uint16_t listen_port) {
    return "LFT_DISCOVERY|1|" + escape_field(local_device.device_id) + "|" +
           escape_field(local_device.device_name) + "|" + std::to_string(listen_port);
}

DiscoveredDevice parse_discovery_payload(const std::string& payload,
                                         const std::string& host,
                                         const PairStore& pair_store) {
    const auto fields = split_fields(payload);
    if (fields.size() != 5) {
        throw std::runtime_error("invalid discovery payload field count");
    }
    if (fields[0] != "LFT_DISCOVERY") {
        throw std::runtime_error("invalid discovery payload prefix");
    }
    if (fields[1] != "1") {
        throw std::runtime_error("unsupported discovery payload version");
    }

    const unsigned long port_value = std::stoul(fields[4]);
    if (port_value > 65535UL) {
        throw std::runtime_error("invalid discovery listen_port");
    }

    DiscoveredDevice device;
    device.device_id = unescape_field(fields[2]);
    device.device_name = unescape_field(fields[3]);
    device.host = host;
    device.listen_port = static_cast<std::uint16_t>(port_value);
    device.paired = pair_store.is_device_paired(device.device_id);
    device.last_seen = std::chrono::steady_clock::now();
    return device;
}

PlatformSocket create_receive_socket() {
    PlatformSocket socket_handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_handle == kInvalidSocket) {
        throw make_discovery_error("failed to create UDP receive socket");
    }

    try {
        set_reuse_addr_option(socket_handle);
        set_receive_timeout(socket_handle, std::chrono::milliseconds(250));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(kDiscoveryPort);
        if (::bind(socket_handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            throw make_discovery_error("failed to bind UDP receive socket");
        }
        return socket_handle;
    } catch (...) {
        close_socket(socket_handle);
        throw;
    }
}

PlatformSocket create_send_socket() {
    PlatformSocket socket_handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_handle == kInvalidSocket) {
        throw make_discovery_error("failed to create UDP send socket");
    }

    try {
        set_broadcast_option(socket_handle);
        return socket_handle;
    } catch (...) {
        close_socket(socket_handle);
        throw;
    }
}

bool is_receive_timeout() {
#ifdef _WIN32
    const int error = WSAGetLastError();
    return error == WSAETIMEDOUT || error == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

void broadcast_payload(PlatformSocket socket_handle, const std::string& payload) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(kDiscoveryPort);
    address.sin_addr.s_addr = INADDR_BROADCAST;

    const int sent = ::sendto(socket_handle,
                              payload.data(),
                              static_cast<int>(payload.size()),
                              0,
                              reinterpret_cast<const sockaddr*>(&address),
                              sizeof(address));
    if (sent < 0) {
        throw make_discovery_error("failed to send discovery broadcast");
    }
}

std::string extract_sender_ip(const sockaddr_in& address) {
    char buffer[INET_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET, &address.sin_addr, buffer, sizeof(buffer)) == nullptr) {
        throw make_discovery_error("failed to convert discovery sender address");
    }
    return std::string(buffer);
}

void receive_announcements(PlatformSocket socket_handle,
                           const LocalDeviceInfo& local_device,
                           const PairStore& pair_store,
                           std::unordered_map<std::string, DiscoveredDevice>& online_devices) {
    std::array<char, kMaxDiscoveryDatagramSize> buffer{};
    sockaddr_in sender_address{};
    socklen_t sender_length = sizeof(sender_address);
    const int received = ::recvfrom(socket_handle,
                                    buffer.data(),
                                    static_cast<int>(buffer.size()),
                                    0,
                                    reinterpret_cast<sockaddr*>(&sender_address),
                                    &sender_length);
    if (received < 0) {
        if (is_receive_timeout()) {
            return;
        }
        throw make_discovery_error("failed to receive discovery datagram");
    }
    if (received == 0) {
        return;
    }

    const std::string payload(buffer.data(), buffer.data() + received);
    DiscoveredDevice device = parse_discovery_payload(payload, extract_sender_ip(sender_address), pair_store);
    if (device.device_id == local_device.device_id) {
        return;
    }
    online_devices[device.device_id] = std::move(device);
}

void prune_expired_devices(std::unordered_map<std::string, DiscoveredDevice>& online_devices,
                           std::chrono::steady_clock::time_point now) {
    const auto stale_after = std::chrono::seconds(5);
    for (auto it = online_devices.begin(); it != online_devices.end();) {
        if (now - it->second.last_seen > stale_after) {
            it = online_devices.erase(it);
            continue;
        }
        ++it;
    }
}

}  // namespace

DeviceDiscoveryService::DeviceDiscoveryService(PairStore& pair_store) : pair_store_(pair_store) {}

std::vector<DiscoveredDevice> DeviceDiscoveryService::discover(
    std::uint16_t listen_port,
    std::chrono::seconds duration,
    std::chrono::milliseconds broadcast_interval) const {
    if (duration <= std::chrono::seconds::zero()) {
        throw std::runtime_error("discovery duration must be positive");
    }

    const LocalDeviceInfo local_device = pair_store_.get_or_create_local_device_info();
    SocketGuard receive_socket{create_receive_socket()};
    SocketGuard send_socket{create_send_socket()};
    const std::string payload = build_discovery_payload(local_device, listen_port);

    std::unordered_map<std::string, DiscoveredDevice> online_devices;
    const auto start_time = std::chrono::steady_clock::now();
    auto next_broadcast = start_time;

    while (std::chrono::steady_clock::now() - start_time < duration) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_broadcast) {
            broadcast_payload(send_socket.handle, payload);
            next_broadcast = now + broadcast_interval;
        }

        receive_announcements(receive_socket.handle, local_device, pair_store_, online_devices);
        prune_expired_devices(online_devices, std::chrono::steady_clock::now());
    }

    std::vector<DiscoveredDevice> devices;
    devices.reserve(online_devices.size());
    for (const auto& entry : online_devices) {
        devices.push_back(entry.second);
    }

    std::sort(devices.begin(), devices.end(), [](const DiscoveredDevice& left, const DiscoveredDevice& right) {
        if (left.paired != right.paired) {
            return left.paired && !right.paired;
        }
        if (left.device_name != right.device_name) {
            return left.device_name < right.device_name;
        }
        return left.device_id < right.device_id;
    });
    return devices;
}

}  // namespace lan_transfer

#include "discovery/presence_announcer.h"

#include <chrono>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "common/types.h"

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

std::string build_discovery_payload(const LocalDeviceInfo& local_device, std::uint16_t listen_port) {
    return "LFT_DISCOVERY|1|" + escape_field(local_device.device_id) + "|" +
           escape_field(local_device.device_name) + "|" + std::to_string(listen_port);
}

PlatformSocket create_broadcast_socket() {
    PlatformSocket socket_handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_handle == kInvalidSocket) {
        throw std::runtime_error("failed to create presence broadcast socket");
    }

    const int broadcast = 1;
    if (setsockopt(socket_handle,
                   SOL_SOCKET,
                   SO_BROADCAST,
                   reinterpret_cast<const char*>(&broadcast),
                   static_cast<int>(sizeof(broadcast))) != 0) {
        close_socket(socket_handle);
        throw std::runtime_error("failed to enable UDP broadcast");
    }
    return socket_handle;
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
        throw std::runtime_error("failed to send presence broadcast");
    }
}

}  // namespace

PresenceAnnouncer::PresenceAnnouncer(PairStore& pair_store) : pair_store_(pair_store) {}

PresenceAnnouncer::~PresenceAnnouncer() {
    stop();
}

void PresenceAnnouncer::start(std::uint16_t listen_port) {
    stop();
    running_ = true;
    worker_ = std::thread(&PresenceAnnouncer::run, this, listen_port);
}

void PresenceAnnouncer::stop() {
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

void PresenceAnnouncer::run(std::uint16_t listen_port) {
    try {
        const LocalDeviceInfo local_device = pair_store_.get_or_create_local_device_info();
        const std::string payload = build_discovery_payload(local_device, listen_port);
        const PlatformSocket socket_handle = create_broadcast_socket();

        while (running_) {
            broadcast_payload(socket_handle, payload);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        close_socket(socket_handle);
    } catch (...) {
        running_ = false;
    }
}

}  // namespace lan_transfer

#include "discovery/code_lookup_service.h"

#include <array>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "common/types.h"
#include "discovery/connection_code.h"

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
constexpr std::size_t kMaxDatagramSize = 1024;

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

PlatformSocket create_lookup_socket() {
    PlatformSocket socket_handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_handle == kInvalidSocket) {
        throw std::runtime_error("failed to create code lookup socket");
    }

    int reuse_addr = 1;
    if (setsockopt(socket_handle,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse_addr),
                   static_cast<int>(sizeof(reuse_addr))) != 0) {
        close_socket(socket_handle);
        throw std::runtime_error("failed to enable lookup address reuse");
    }

#ifdef _WIN32
    const DWORD timeout_ms = 500;
    if (setsockopt(socket_handle,
                   SOL_SOCKET,
                   SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout_ms),
                   static_cast<int>(sizeof(timeout_ms))) != 0) {
        close_socket(socket_handle);
        throw std::runtime_error("failed to set lookup receive timeout");
    }
#else
    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    if (setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
        close_socket(socket_handle);
        throw std::runtime_error("failed to set lookup receive timeout");
    }
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(kDiscoveryPort);
    if (::bind(socket_handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        close_socket(socket_handle);
        throw std::runtime_error("failed to bind code lookup socket");
    }
    return socket_handle;
}

bool is_receive_timeout() {
#ifdef _WIN32
    const int error = WSAGetLastError();
    return error == WSAETIMEDOUT || error == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
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
        throw std::runtime_error("lookup payload ends with dangling escape");
    }
    fields.push_back(current);
    return fields;
}

std::string build_lookup_response(const std::string& code,
                                  const LocalDeviceInfo& local_device,
                                  std::uint16_t listen_port) {
    return "LFT_CODE_MATCH|1|" + code + "|" + escape_field(local_device.device_id) + "|" +
           escape_field(local_device.device_name) + "|" + std::to_string(listen_port);
}

void handle_lookup_request(PlatformSocket socket_handle,
                           const LocalDeviceInfo& local_device,
                           std::uint16_t listen_port) {
    std::array<char, kMaxDatagramSize> buffer{};
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
        throw std::runtime_error("failed to receive code lookup request");
    }
    if (received == 0) {
        return;
    }

    const std::string payload(buffer.data(), buffer.data() + received);
    const auto fields = split_fields(payload);
    if (fields.size() != 3 || fields[0] != "LFT_CODE_LOOKUP" || fields[1] != "1") {
        return;
    }

    const std::string local_code = make_connection_code(local_device.device_id);
    if (fields[2] != local_code) {
        return;
    }

    const std::string response = build_lookup_response(local_code, local_device, listen_port);
    const int sent = ::sendto(socket_handle,
                              response.data(),
                              static_cast<int>(response.size()),
                              0,
                              reinterpret_cast<const sockaddr*>(&sender_address),
                              sender_length);
    if (sent < 0) {
        throw std::runtime_error("failed to send code lookup response");
    }
}

}  // namespace

CodeLookupService::CodeLookupService(PairStore& pair_store) : pair_store_(pair_store) {}

CodeLookupService::~CodeLookupService() {
    stop();
}

void CodeLookupService::start(std::uint16_t listen_port) {
    stop();
    running_ = true;
    worker_ = std::thread(&CodeLookupService::run, this, listen_port);
}

void CodeLookupService::stop() {
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

void CodeLookupService::run(std::uint16_t listen_port) {
    try {
        const LocalDeviceInfo local_device = pair_store_.get_or_create_local_device_info();
        const PlatformSocket socket_handle = create_lookup_socket();
        while (running_) {
            handle_lookup_request(socket_handle, local_device, listen_port);
        }
        close_socket(socket_handle);
    } catch (...) {
        running_ = false;
    }
}

}  // namespace lan_transfer

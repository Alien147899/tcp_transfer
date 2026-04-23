#include "net/socket.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
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

PlatformSocket to_platform_socket(Socket::NativeHandle handle) {
    return static_cast<PlatformSocket>(handle);
}

Socket::NativeHandle to_internal_handle(PlatformSocket handle) {
    return static_cast<Socket::NativeHandle>(handle);
}

void close_platform_socket(PlatformSocket handle) {
#ifdef _WIN32
    closesocket(handle);
#else
    close(handle);
#endif
}

std::runtime_error make_socket_error(const std::string& message) {
#ifdef _WIN32
    return std::runtime_error(message + " (winsock error " + std::to_string(WSAGetLastError()) + ")");
#else
    return std::runtime_error(message + ": " + std::strerror(errno));
#endif
}

std::array<std::uint8_t, 8> encode_u64_be(std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (int i = 7; i >= 0; --i) {
        bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value & 0xFFU);
        value >>= 8U;
    }
    return bytes;
}

std::uint64_t decode_u64_be(const std::array<std::uint8_t, 8>& bytes) {
    std::uint64_t value = 0;
    for (std::uint8_t byte : bytes) {
        value = (value << 8U) | static_cast<std::uint64_t>(byte);
    }
    return value;
}

}  // namespace

SocketSystem::SocketSystem() {
#ifdef _WIN32
    WSADATA wsa_data{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        throw make_socket_error("WSAStartup failed");
    }
#endif
}

SocketSystem::~SocketSystem() {
#ifdef _WIN32
    WSACleanup();
#endif
}

Socket::Socket() : handle_(to_internal_handle(kInvalidSocket)) {}

Socket::Socket(NativeHandle native_handle) : handle_(native_handle) {}

Socket::~Socket() {
    close();
}

Socket::Socket(Socket&& other) noexcept : handle_(other.handle_) {
    other.handle_ = to_internal_handle(kInvalidSocket);
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        other.handle_ = to_internal_handle(kInvalidSocket);
    }
    return *this;
}

void Socket::connect_to(const std::string& host, std::uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    const std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result) != 0) {
        throw make_socket_error("failed to resolve address");
    }

    for (addrinfo* current = result; current != nullptr; current = current->ai_next) {
        PlatformSocket candidate = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (candidate == kInvalidSocket) {
            continue;
        }

        if (::connect(candidate, current->ai_addr, static_cast<int>(current->ai_addrlen)) == 0) {
            close();
            handle_ = to_internal_handle(candidate);
            freeaddrinfo(result);
            return;
        }

        close_platform_socket(candidate);
    }

    freeaddrinfo(result);
    throw make_socket_error("failed to connect");
}

void Socket::bind_and_listen(std::uint16_t port, int backlog) {
    PlatformSocket server = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == kInvalidSocket) {
        throw make_socket_error("failed to create server socket");
    }

    int reuse_addr = 1;
    if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse_addr),
                   static_cast<int>(sizeof(reuse_addr))) != 0) {
        close_platform_socket(server);
        throw make_socket_error("failed to set socket options");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        close_platform_socket(server);
        throw make_socket_error("failed to bind server socket");
    }

    if (::listen(server, backlog) != 0) {
        close_platform_socket(server);
        throw make_socket_error("failed to listen on server socket");
    }

    close();
    handle_ = to_internal_handle(server);
}

Socket Socket::accept_connection() const {
    PlatformSocket accepted = ::accept(to_platform_socket(handle_), nullptr, nullptr);
    if (accepted == kInvalidSocket) {
        throw make_socket_error("failed to accept incoming connection");
    }
    return Socket(to_internal_handle(accepted));
}

void Socket::send_all(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const char*>(data);
    std::size_t total_sent = 0;
    while (total_sent < size) {
        const int sent = ::send(to_platform_socket(handle_),
                                bytes + total_sent,
                                static_cast<int>(size - total_sent),
                                0);
        if (sent <= 0) {
            throw make_socket_error("failed to send data");
        }
        total_sent += static_cast<std::size_t>(sent);
    }
}

void Socket::receive_exact(void* data, std::size_t size) {
    auto* bytes = static_cast<char*>(data);
    std::size_t total_received = 0;
    while (total_received < size) {
        const int received = ::recv(to_platform_socket(handle_),
                                    bytes + total_received,
                                    static_cast<int>(size - total_received),
                                    0);
        if (received <= 0) {
            throw make_socket_error("failed to receive data");
        }
        total_received += static_cast<std::size_t>(received);
    }
}

void Socket::send_uint32(std::uint32_t value) {
    const std::uint32_t network_value = htonl(value);
    send_all(&network_value, sizeof(network_value));
}

std::uint32_t Socket::receive_uint32() {
    std::uint32_t network_value = 0;
    receive_exact(&network_value, sizeof(network_value));
    return ntohl(network_value);
}

void Socket::send_uint64(std::uint64_t value) {
    const auto bytes = encode_u64_be(value);
    send_all(bytes.data(), bytes.size());
}

std::uint64_t Socket::receive_uint64() {
    std::array<std::uint8_t, 8> bytes{};
    receive_exact(bytes.data(), bytes.size());
    return decode_u64_be(bytes);
}

std::string Socket::get_peer_ip() const {
    sockaddr_in address{};
    int address_length = sizeof(address);
    if (getpeername(to_platform_socket(handle_), reinterpret_cast<sockaddr*>(&address), &address_length) != 0) {
        throw make_socket_error("failed to get peer address");
    }

    char buffer[INET_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET, &address.sin_addr, buffer, sizeof(buffer)) == nullptr) {
        throw make_socket_error("failed to convert peer address");
    }
    return std::string(buffer);
}

bool Socket::is_valid() const {
    return to_platform_socket(handle_) != kInvalidSocket;
}

void Socket::close() {
    if (is_valid()) {
        close_platform_socket(to_platform_socket(handle_));
        handle_ = to_internal_handle(kInvalidSocket);
    }
}

}  // namespace lan_transfer

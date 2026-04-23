#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lan_transfer {

class SocketSystem {
public:
    SocketSystem();
    ~SocketSystem();

    SocketSystem(const SocketSystem&) = delete;
    SocketSystem& operator=(const SocketSystem&) = delete;
};

class Socket {
public:
    using NativeHandle = std::intptr_t;

    Socket();
    explicit Socket(NativeHandle native_handle);
    ~Socket();

    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    void connect_to(const std::string& host, std::uint16_t port);
    void bind_and_listen(std::uint16_t port, int backlog = 1);
    Socket accept_connection() const;

    void send_all(const void* data, std::size_t size);
    void receive_exact(void* data, std::size_t size);

    void send_uint32(std::uint32_t value);
    std::uint32_t receive_uint32();

    void send_uint64(std::uint64_t value);
    std::uint64_t receive_uint64();

    std::string get_peer_ip() const;

    bool is_valid() const;
    void close();

private:
    NativeHandle handle_;
};

}  // namespace lan_transfer

#pragma once 

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
using SocketHandle = std::uintptr_t;
#else
using SocketHandle = int;
#endif

struct Endpoint {
    std::uint32_t address; // IPv4, сетевой порядок
    std::uint16_t port;    // обычный порядок
};

inline bool operator==(const Endpoint& a, const Endpoint& b) {
    return a.address == b.address && a.port == b.port;
}

std::optional<Endpoint> MakeEndpoint(const std::string& ip, uint16_t port);
std::string ToString(const Endpoint& endpoint);

class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool IsOpen() const;
    bool Bind(std::uint16_t port);
    bool SendTo(const Endpoint& to, const std::vector<std::uint8_t>& data);

    std::optional<std::size_t> ReceiveFrom(
        std::uint8_t* buffer,
        std::size_t capacity,
        Endpoint& from,
        std::chrono::milliseconds timeout);

private:
    SocketHandle handle_;
};
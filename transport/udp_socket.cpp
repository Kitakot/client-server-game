#include "transport/udp_socket.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

const SocketHandle kInvalidHandle = static_cast<SocketHandle>(-1);

sockaddr_in toSockaddr(const Endpoint& endpoint) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = endpoint.address;
    address.sin_port = htons(endpoint.port);
    return address;
}

}

std::optional<Endpoint> MakeEndpoint(const std::string& ip, uint16_t port) {
    in_addr address{};
    if (inet_pton(AF_INET, ip.c_str(), &address) != 1) {
        return std::nullopt;
    }
    return Endpoint{address.s_addr, port};
}

std::string ToString(const Endpoint& endpoint) {
    char ip[INET_ADDRSTRLEN] = {};
    in_addr address{};
    address.s_addr = endpoint.address;
    inet_ntop(AF_INET, &address, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(endpoint.port);
}

UdpSocket::UdpSocket() : handle_(kInvalidHandle) {
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return;
    }
#endif
    handle_ = static_cast<SocketHandle>(socket(AF_INET, SOCK_DGRAM, 0));
}

UdpSocket::~UdpSocket() {
#ifdef _WIN32
    if (IsOpen()) {
        closesocket(static_cast<SOCKET>(handle_));
    }
    WSACleanup();
#else
    if (IsOpen()) {
        close(handle_);
    }
#endif
}

bool UdpSocket::IsOpen() const {
    return handle_ != kInvalidHandle;
}

bool UdpSocket::Bind(std::uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    return bind(handle_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
}

bool UdpSocket::SendTo(const Endpoint& to, const std::vector<std::uint8_t>& data) {
    sockaddr_in address = toSockaddr(to);
    int sent = sendto(handle_, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0,
                     reinterpret_cast<sockaddr*>(&address), sizeof(address));
    return sent == static_cast<int>(data.size());
}

std::optional<std::size_t> UdpSocket::ReceiveFrom(
    std::uint8_t* buffer,
    std::size_t capacity,
    Endpoint& from,
    std::chrono::milliseconds timeout) {
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(handle_, &readSet);

    timeval tv{};
    tv.tv_sec = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);

    // select ждёт пока в сокете появятся данные для чтения или пока не истечёт таймаут.
    int result = select(static_cast<int>(handle_) + 1, &readSet, nullptr, nullptr, &tv);
    if (result <= 0) {
        return std::nullopt; // timeout or error
    }

    sockaddr_in address{};
#ifdef _WIN32
    int addressLength = sizeof(address);
#else
    socklen_t addressLength = sizeof(address);
#endif

    // ошибка сокета или таймаут уже проверены выше, поэтому recvfrom не должен блокировать.
    int received = recvfrom(handle_, reinterpret_cast<char*>(buffer), static_cast<int>(capacity), 0,
                            reinterpret_cast<sockaddr*>(&address), &addressLength);
    if (received < 0) {
        return std::nullopt; // error
    }

    from = Endpoint{address.sin_addr.s_addr, ntohs(address.sin_port)};
    return static_cast<std::size_t>(received);
}
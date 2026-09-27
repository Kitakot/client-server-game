#include "common.hpp"

#include <iostream>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
#endif

constexpr int PORT = 54000;

void closeSocket(Socket socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

std::string endpointToString(const sockaddr_in& address) {
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &address.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(address.sin_port));
}

int main() {
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif

    Socket serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSocket < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(PORT);

    if (bind(
            serverSocket,
            reinterpret_cast<sockaddr*>(&serverAddress),
            sizeof(serverAddress)) < 0) {
        std::cerr << "Failed to bind socket\n";
        closeSocket(serverSocket);
        return 1;
    }

    std::ofstream log("server.log", std::ios::app);

    std::cout << "UDP server started on port " << PORT << "\n";

    auto dropDatagram = [&log](const sockaddr_in& from, int size, ParseError error) {
        std::cerr << "[DROP] from=" << endpointToString(from)
                  << " size=" << size
                  << " reason=" << toString(error) << "\n";
        if (log) {
            log << "dropped from=" << endpointToString(from)
                << " size=" << size
                << " reason=\"" << toString(error) << "\"\n";
        }
    };

    while (true) {
        uint8_t buffer[MAX_DATAGRAM_SIZE];

        sockaddr_in clientAddress{};
#ifdef _WIN32
        int addressLength = sizeof(clientAddress);
#else
        socklen_t addressLength = sizeof(clientAddress);
#endif

        int received = recvfrom(
            serverSocket,
            reinterpret_cast<char*>(buffer),
            sizeof(buffer),
            0,
            reinterpret_cast<sockaddr*>(&clientAddress),
            &addressLength);

        // Ошибка сокета (например, ICMP port unreachable на Windows или
        // датаграмма больше буфера) — пропускаем, сервер продолжает работу.
        if (received < 0) {
            continue;
        }

        auto parsed = parsePacket(buffer, static_cast<size_t>(received));
        if (auto* error = std::get_if<ParseError>(&parsed)) {
            dropDatagram(clientAddress, received, *error);
            continue;
        }
        const Packet& packet = std::get<Packet>(parsed);
        const uint16_t sequence = packet.header.sequence;

        std::string response;

        if (packet.header.type == CommandType::UNIT_MOVE) {
            auto command = parseUnitMove(packet);
            if (auto* error = std::get_if<ParseError>(&command)) {
                dropDatagram(clientAddress, received, *error);
                continue;
            }
            const UnitMove& move = std::get<UnitMove>(command);

            response =
                "UNIT_MOVED unit=" + std::to_string(move.unitId) +
                " position=(" + std::to_string(move.targetX) +
                ", " + std::to_string(move.targetY) + ")";

            std::cout << "[UNIT_MOVE] seq=" << sequence
                      << " unit=" << move.unitId
                      << " target=(" << move.targetX
                      << ", " << move.targetY << ")\n";
        }
        else if (packet.header.type == CommandType::UNIT_ATTACK) {
            auto command = parseUnitAttack(packet);
            if (auto* error = std::get_if<ParseError>(&command)) {
                dropDatagram(clientAddress, received, *error);
                continue;
            }
            const UnitAttack& attack = std::get<UnitAttack>(command);

            response =
                "UNIT_ATTACK attacker=" + std::to_string(attack.attackerId) +
                " target=" + std::to_string(attack.targetId);

            std::cout << "[UNIT_ATTACK] seq=" << sequence
                      << " attacker=" << attack.attackerId
                      << " target=" << attack.targetId << "\n";
        }

        if (log) {
            log << "sequence=" << sequence
                << " type=" << static_cast<int>(packet.header.type)
                << " payloadSize=" << packet.header.payloadSize
                << " response=\"" << response << "\"\n";
        }

        auto responsePacket = serializeTextResponse(packet.header.type, sequence, response);

        sendto(
            serverSocket,
            reinterpret_cast<const char*>(responsePacket.data()),
            static_cast<int>(responsePacket.size()),
            0,
            reinterpret_cast<sockaddr*>(&clientAddress),
            addressLength);
    }

    closeSocket(serverSocket);

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}

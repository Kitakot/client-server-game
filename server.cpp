#include "common.hpp"

#include <iostream>
#include <fstream>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
using Socket = SOCKET;
#else
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
#endif

constexpr int PORT = 54000;
constexpr int BUFFER_SIZE = 1024;

void closeSocket(Socket socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
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

    int32_t playerX = 0;
    int32_t playerY = 0;

    while (true) {
        char buffer[BUFFER_SIZE];

        sockaddr_in clientAddress{};
#ifdef _WIN32
        int addressLength = sizeof(clientAddress);
#else
        socklen_t addressLength = sizeof(clientAddress);
#endif

        int received = recvfrom(
            serverSocket,
            buffer,
            BUFFER_SIZE,
            0,
            reinterpret_cast<sockaddr*>(&clientAddress),
            &addressLength);

        if (received < static_cast<int>(sizeof(PacketHeader))) {
            continue;
        }

        PacketHeader header{};
        std::memcpy(&header, buffer, sizeof(header));

        auto type = static_cast<CommandType>(ntohs(header.type));
        uint32_t sequence = ntohl(header.sequence);
        uint32_t payloadSize = ntohl(header.payloadSize);

        if (sizeof(PacketHeader) + payloadSize > static_cast<size_t>(received)) {
            continue;
        }

        std::string response;

        if (type == CommandType::UNIT_MOVE &&
        payloadSize == sizeof(UnitMove)) {
            UnitMove command{};
            std::memcpy(&command, buffer + sizeof(PacketHeader), sizeof(command));

            response =
                "UNIT_MOVED unit=" + std::to_string(command.unitId) +
                " position=(" + std::to_string(command.targetX) +
                ", " + std::to_string(command.targetY) + ")";

            std::cout << "[UNIT_MOVE] seq=" << sequence
                    << " unit=" << command.unitId
                    << " target=(" << command.targetX
                    << ", " << command.targetY << ")\n";
        }
        else if (type == CommandType::UNIT_ATTACK &&
                payloadSize == sizeof(UnitAttack)) {
            UnitAttack command{};
            std::memcpy(&command, buffer + sizeof(PacketHeader), sizeof(command));

            response =
                "UNIT_ATTACK attacker=" + std::to_string(command.attackerId) +
                " target=" + std::to_string(command.targetId);

            std::cout << "[UNIT_ATTACK] seq=" << sequence
                    << " attacker=" << command.attackerId
                    << " target=" << command.targetId << "\n";
        }
        else {
            response = "ERROR unknown command";
        }

        if (log) {
            log << "sequence=" << sequence
                << " type=" << static_cast<uint16_t>(type)
                << " payloadSize=" << payloadSize
                << " response=\"" << response << "\"\n";
        }

        auto responsePacket = makePacket(
            type,
            sequence,
            response.data(),
            static_cast<uint32_t>(response.size()));

        sendto(
            serverSocket,
            responsePacket.data(),
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
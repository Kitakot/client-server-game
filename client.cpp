#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "common.hpp"

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
using Socket = SOCKET;
#else
using Socket = int;
#endif

constexpr int SERVER_PORT = 54000;
constexpr int BUFFER_SIZE = 1024;
constexpr int RESPONSE_TIMEOUT_SECONDS = 2;

void closeSocket(Socket socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

bool receiveResponse(Socket clientSocket) {
    char buffer[BUFFER_SIZE];

    sockaddr_in serverAddress{};
#ifdef _WIN32
    int addressLength = sizeof(serverAddress);
#else
    socklen_t addressLength = sizeof(serverAddress);
#endif

    int received = recvfrom(
        clientSocket,
        buffer,
        BUFFER_SIZE,
        0,
        reinterpret_cast<sockaddr*>(&serverAddress),
        &addressLength);

    if (received < static_cast<int>(sizeof(PacketHeader))) {
        std::cerr << "Invalid or empty server response\n";
        return false;
    }

    PacketHeader header{};
    std::memcpy(&header, buffer, sizeof(PacketHeader));

    uint32_t payloadSize = ntohl(header.payloadSize);

    if (payloadSize > BUFFER_SIZE - sizeof(PacketHeader) ||
        sizeof(PacketHeader) + payloadSize > static_cast<size_t>(received)) {
        std::cerr << "Invalid response payload size\n";
        return false;
    }

    std::string response(
        buffer + sizeof(PacketHeader),
        payloadSize);

    std::cout
        << "Server response"
        << " [sequence=" << ntohl(header.sequence) << "]: "
        << response
        << "\n";

    return true;
}

bool sendCommand(
    Socket clientSocket,
    const sockaddr_in& serverAddress,
    CommandType commandType,
    uint32_t sequence,
    const void* payload,
    uint32_t payloadSize) {
    auto packet = makePacket(
        commandType,
        sequence,
        payload,
        payloadSize);

    int sent = sendto(
        clientSocket,
        packet.data(),
        static_cast<int>(packet.size()),
        0,
        reinterpret_cast<const sockaddr*>(&serverAddress),
        sizeof(serverAddress));

    if (sent < 0) {
        std::cerr << "Failed to send command\n";
        return false;
    }

    return receiveResponse(clientSocket);
}

int main() {
#ifdef _WIN32
    WSADATA data{};

    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        std::cerr << "Failed to initialize Winsock\n";
        return 1;
    }
#endif

    Socket clientSocket = socket(AF_INET, SOCK_DGRAM, 0);

#ifdef _WIN32
    if (clientSocket == INVALID_SOCKET) {
#else
    if (clientSocket < 0) {
#endif
        std::cerr << "Failed to create UDP socket\n";

#ifdef _WIN32
        WSACleanup();
#endif

        return 1;
    }

#ifdef _WIN32
    DWORD timeout = RESPONSE_TIMEOUT_SECONDS * 1000;
    setsockopt(
        clientSocket,
        SOL_SOCKET,
        SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout),
        sizeof(timeout));
#else
    timeval timeout{};
    timeout.tv_sec = RESPONSE_TIMEOUT_SECONDS;

    setsockopt(
        clientSocket,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        sizeof(timeout));
#endif

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(SERVER_PORT);

    serverAddress.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (serverAddress.sin_addr.s_addr == INADDR_NONE) {
        std::cerr << "Invalid server address\n";
        closeSocket(clientSocket);
        return 1;
    }

    std::cout << "Strategy game client started\n";
    std::cout << "Connected to UDP server on port "
              << SERVER_PORT << "\n";

    uint32_t sequence = 1;
    int currentTurn = 1;

    constexpr int playerUnitId = 1;
    constexpr int enemyUnitId = 42;

    while (true) {
        std::cout << "\n--- Turn " << currentTurn << " ---\n";

        UnitMove moveCommand{
            playerUnitId,
            currentTurn * 2,
            currentTurn
        };

        std::cout
            << "Moving unit "
            << moveCommand.unitId
            << " to ("
            << moveCommand.targetX
            << ", "
            << moveCommand.targetY
            << ")\n";

        sendCommand(
            clientSocket,
            serverAddress,
            CommandType::UNIT_MOVE,
            sequence++,
            &moveCommand,
            sizeof(moveCommand));

        if (currentTurn % 3 == 0) {
            UnitAttack attackCommand{
                playerUnitId,
                enemyUnitId
            };

            std::cout
                << "Unit "
                << attackCommand.attackerId
                << " attacks unit "
                << attackCommand.targetId
                << "\n";

            sendCommand(
                clientSocket,
                serverAddress,
                CommandType::UNIT_ATTACK,
                sequence++,
                &attackCommand,
                sizeof(attackCommand));
        }

        ++currentTurn;

        std::this_thread::sleep_for(
            std::chrono::seconds(1));
    }

    closeSocket(clientSocket);

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
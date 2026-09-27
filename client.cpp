#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "common.hpp"

#include <chrono>
#include <iostream>
#include <map>
#include <string>

#ifdef _WIN32
using Socket = SOCKET;
#else
using Socket = int;
#endif

using Clock = std::chrono::steady_clock;

constexpr int SERVER_PORT = 54000;
constexpr auto RESPONSE_TIMEOUT = std::chrono::seconds(2);
constexpr auto TURN_DURATION = std::chrono::seconds(1);

// Команда, отправленная серверу и ещё не получившая ответа.
struct PendingCommand {
    CommandType type;
    Clock::time_point sentAt;
};

void closeSocket(Socket socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

// Ждёт входящую датаграмму не дольше timeout. Не блокирует отправку:
// клиент сам решает, когда вернуться к отправке команд следующего хода.
bool waitForData(Socket socket, std::chrono::milliseconds timeout) {
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(socket, &readSet);

    timeval tv{};
    tv.tv_sec = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);

    int ready = select(static_cast<int>(socket) + 1, &readSet, nullptr, nullptr, &tv);
    return ready > 0;
}

bool sameEndpoint(const sockaddr_in& a, const sockaddr_in& b) {
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

void receiveResponse(
    Socket clientSocket,
    const sockaddr_in& expectedServer,
    std::map<uint16_t, PendingCommand>& pending) {
    uint8_t buffer[MAX_DATAGRAM_SIZE];

    sockaddr_in serverAddress{};
#ifdef _WIN32
    int addressLength = sizeof(serverAddress);
#else
    socklen_t addressLength = sizeof(serverAddress);
#endif

    int received = recvfrom(
        clientSocket,
        reinterpret_cast<char*>(buffer),
        sizeof(buffer),
        0,
        reinterpret_cast<sockaddr*>(&serverAddress),
        &addressLength);

    if (received < 0) {
        std::cerr << "Failed to receive server response\n";
        return;
    }

    if (!sameEndpoint(serverAddress, expectedServer)) {
        std::cerr << "Ignored datagram from unexpected sender\n";
        return;
    }

    auto parsed = parsePacket(buffer, static_cast<size_t>(received));
    if (auto* error = std::get_if<ParseError>(&parsed)) {
        std::cerr << "Dropped invalid server response"
                  << " (size=" << received
                  << ", reason=" << toString(*error) << ")\n";
        return;
    }
    const Packet& packet = std::get<Packet>(parsed);

    auto it = pending.find(packet.header.sequence);
    if (it == pending.end()) {
        std::cerr << "Unexpected response [sequence="
                  << packet.header.sequence << "]\n";
        return;
    }
    if (it->second.type != packet.header.type) {
        std::cerr << "Response type mismatch [sequence="
                  << packet.header.sequence << "]\n";
        return;
    }

    std::cout
        << "Server response"
        << " [sequence=" << packet.header.sequence << "]: "
        << payloadText(packet)
        << "\n";

    pending.erase(it);
}

void expireTimedOut(std::map<uint16_t, PendingCommand>& pending, Clock::time_point now) {
    for (auto it = pending.begin(); it != pending.end();) {
        if (now - it->second.sentAt > RESPONSE_TIMEOUT) {
            std::cerr << "No response [sequence=" << it->first << "]\n";
            it = pending.erase(it);
        } else {
            ++it;
        }
    }
}

bool sendPacket(
    Socket clientSocket,
    const sockaddr_in& serverAddress,
    const std::vector<uint8_t>& packet) {
    int sent = sendto(
        clientSocket,
        reinterpret_cast<const char*>(packet.data()),
        static_cast<int>(packet.size()),
        0,
        reinterpret_cast<const sockaddr*>(&serverAddress),
        sizeof(serverAddress));

    if (sent < 0) {
        std::cerr << "Failed to send command\n";
        return false;
    }
    return true;
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

    uint16_t sequence = 1;
    int currentTurn = 1;
    std::map<uint16_t, PendingCommand> pending;

    constexpr int playerUnitId = 1;
    constexpr int enemyUnitId = 42;

    while (true) {
        const auto turnStart = Clock::now();
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

        if (sendPacket(clientSocket, serverAddress, serializeUnitMove(sequence, moveCommand))) {
            pending[sequence] = {CommandType::UNIT_MOVE, Clock::now()};
        }
        ++sequence;

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

            if (sendPacket(clientSocket, serverAddress, serializeUnitAttack(sequence, attackCommand))) {
                pending[sequence] = {CommandType::UNIT_ATTACK, Clock::now()};
            }
            ++sequence;
        }

        // До начала следующего хода принимаем ответы по мере их прихода.
        const auto turnEnd = turnStart + TURN_DURATION;
        for (auto now = Clock::now(); now < turnEnd; now = Clock::now()) {
            auto remaining = std::chrono::ceil<std::chrono::milliseconds>(turnEnd - now);
            if (waitForData(clientSocket, remaining)) {
                receiveResponse(clientSocket, serverAddress, pending);
            }
            expireTimedOut(pending, Clock::now());
        }

        ++currentTurn;
    }

    closeSocket(clientSocket);

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}

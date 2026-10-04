#include "protocol/protocol.hpp"
#include "transport/clock.hpp"
#include "transport/udp_socket.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

// UDP-сервер игры. Принимает команды клиентов (формат — в protocol.hpp),
// проверяет их и отвечает тому, кто прислал: на UNIT_MOVE / UNIT_ATTACK —
// текстовым подтверждением, на PING — пакетом PONG. Состояния игры и списка
// клиентов сервер не хранит: каждая датаграмма обрабатывается независимо.

constexpr std::uint16_t PORT = 54000;

int main() {
    UdpSocket socket;
    if (!socket.IsOpen()) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }
    if (!socket.Bind(PORT)) {
        std::cerr << "Failed to bind socket\n";
        return 1;
    }

    std::ofstream log("server.log", std::ios::app);

    std::cout << "UDP server started on port " << PORT << "\n";

    // Некорректная датаграмма только журналируется; ответ на неё не
    // отправляется.
    auto dropDatagram = [&log](const Endpoint& from, std::size_t size, ParseError error) {
        std::cerr << "[DROP] from=" << ToString(from)
                  << " size=" << size
                  << " reason=" << toString(error) << "\n";
        if (log) {
            log << "dropped from=" << ToString(from)
                << " size=" << size
                << " reason=\"" << toString(error) << "\"" << std::endl;
        }
    };

    while (true) {
        uint8_t buffer[MAX_DATAGRAM_SIZE];

        // Адрес отправителя: на него уйдёт ответ.
        Endpoint client;
        auto received = socket.ReceiveFrom(
            buffer, sizeof(buffer), client, std::chrono::milliseconds(1000));
        if (!received) {
            continue;
        }
        // Метка снимается сразу после приёма, до разбора пакета.
        const uint64_t receivedAtUs = NowUs();

        // Сначала проверяется заголовок, затем в ветке по типу — payload.
        auto parsed = parsePacket(buffer, *received);
        if (auto* error = std::get_if<ParseError>(&parsed)) {
            dropDatagram(client, *received, *error);
            continue;
        }
        const Packet& packet = std::get<Packet>(parsed);
        const uint16_t sequence = packet.header.sequence;

        if (packet.header.type == CommandType::PING) {
            auto ping = parsePing(packet);
            if (auto* error = std::get_if<ParseError>(&ping)) {
                dropDatagram(client, *received, *error);
                continue;
            }

            Pong pong{};
            pong.clientSendTimeUs = std::get<Ping>(ping).clientSendTimeUs;
            pong.serverReceiveTimeUs = receivedAtUs;
            pong.serverSendTimeUs = NowUs();
            socket.SendTo(client, serializePong(sequence, pong));

            if (log) {
                log << "sequence=" << sequence
                    << " type=PING processingUs="
                    << pong.serverSendTimeUs - pong.serverReceiveTimeUs << "\n";
            }
            continue;
        }

        std::string response;

        if (packet.header.type == CommandType::UNIT_MOVE) {
            auto command = parseUnitMove(packet);
            if (auto* error = std::get_if<ParseError>(&command)) {
                dropDatagram(client, *received, *error);
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
                dropDatagram(client, *received, *error);
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
        else {
            // PONG сервер не принимает: это ответ, а не запрос.
            dropDatagram(client, *received, ParseError::UnexpectedType);
            continue;
        }

        if (log) {
            log << "sequence=" << sequence
                << " type=" << static_cast<int>(packet.header.type)
                << " payloadSize=" << packet.header.payloadSize
                << " response=\"" << response << "\"\n";
        }

        // Ответ повторяет тип и sequence запроса, чтобы клиент мог найти
        // команду, на которую он пришёл.
        socket.SendTo(client, serializeTextResponse(packet.header.type, sequence, response));
    }
}
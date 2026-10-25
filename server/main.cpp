#include "protocol/protocol.hpp"
#include "reliability/dedup_window.hpp"
#include "transport/clock.hpp"
#include "transport/udp_socket.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

// UDP-сервер игры. Принимает команды клиентов (формат — в protocol.hpp),
// проверяет их и отвечает тому, кто прислал: на UNIT_MOVE / UNIT_ATTACK —
// ACK (если requiresAck = 1) и текстовым результатом, на PING — пакетом PONG.
// Для каждого клиента сервер хранит окно обработанных sequenceNumber, чтобы
// повторно присланный приказ не применялся дважды, и здоровье юнитов, чтобы
// повторное применение атаки было видно в журнале.

constexpr std::uint16_t PORT = 54000;
constexpr int UNIT_START_HP = 1000;
constexpr int ATTACK_DAMAGE = 1;

// Соединения нет, поэтому клиент определяется адресом и портом отправителя.
std::uint64_t ClientKey(const Endpoint& endpoint) {
    return (static_cast<std::uint64_t>(endpoint.address) << 16) | endpoint.port;
}

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

    std::unordered_map<std::uint64_t, DedupWindow> processed;  // клиент -> окно номеров
    std::unordered_map<std::int32_t, int> unitHp;

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

        // Надёжный приказ: ACK уходит сразу после проверки payload, до
        // применения. На дубликат ACK тоже отправляется: раз клиент повторил
        // пакет, прошлый ACK потерялся, и без нового клиент будет слать пакет
        // до исчерпания попыток. Сам эффект дубликата не применяется.
        auto acceptReliable = [&]() {
            if (!packet.header.requiresAck) {
                return true;
            }
            socket.SendTo(client, serializeAck(sequence));
            if (processed[ClientKey(client)].MarkProcessed(sequence)) {
                return true;
            }
            std::cout << "[DUPLICATE] seq=" << sequence
                      << " from=" << ToString(client) << " ack resent, effect skipped\n";
            if (log) {
                log << "duplicate sequence=" << sequence
                    << " from=" << ToString(client) << "\n";
            }
            return false;
        };

        std::string response;

        if (packet.header.type == CommandType::UNIT_MOVE) {
            auto command = parseUnitMove(packet);
            if (auto* error = std::get_if<ParseError>(&command)) {
                dropDatagram(client, *received, *error);
                continue;
            }
            if (!acceptReliable()) {
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
            if (!acceptReliable()) {
                continue;
            }
            const UnitAttack& attack = std::get<UnitAttack>(command);

            int& hp = unitHp.try_emplace(attack.targetId, UNIT_START_HP).first->second;
            hp = (std::max)(0, hp - ATTACK_DAMAGE);

            response =
                "UNIT_ATTACK attacker=" + std::to_string(attack.attackerId) +
                " target=" + std::to_string(attack.targetId) +
                " hp=" + std::to_string(hp);

            std::cout << "[UNIT_ATTACK] seq=" << sequence
                      << " attacker=" << attack.attackerId
                      << " target=" << attack.targetId
                      << " hp=" << hp << "\n";
        }
        else {
            // PONG и ACK сервер не принимает: это ответы, а не запросы.
            dropDatagram(client, *received, ParseError::UnexpectedType);
            continue;
        }

        if (log) {
            log << "sequence=" << sequence
                << " type=" << static_cast<int>(packet.header.type)
                << " payloadSize=" << packet.header.payloadSize
                << " response=\"" << response << "\"\n";
        }

        // Ответ повторяет тип и sequence запроса. Он ненадёжен: доставку
        // приказа подтверждает ACK, а это игровой результат.
        socket.SendTo(client, serializeTextResponse(packet.header.type, sequence, response));
    }
}
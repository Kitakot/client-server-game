#include "client/order_book.hpp"
#include "protocol/protocol.hpp"
#include "reliability/adaptive_timeout.hpp"
#include "reliability/delivery_log.hpp"
#include "reliability/reliable_channel.hpp"
#include "transport/clock.hpp"
#include "transport/udp_socket.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

// Клиент надёжной доставки (ПР №3). Отправляет серию приказов UNIT_MOVE и
// UNIT_ATTACK с requiresAck = 1, повторяет неподтверждённые по адаптивному
// RTO и дописывает судьбу каждого приказа в docs/reliability_samples.csv.
// Перед серией несколько PING дают первую оценку RTT для RTO.
//
// Запуск: reliable_client <experiment_id> [count] [interval_ms] [max_attempts]

constexpr std::uint16_t SERVER_PORT = 54000;
constexpr const char* SERVER_IP = "127.0.0.1";
constexpr const char* CSV_PATH = "docs/reliability_samples.csv";
constexpr int WARMUP_PINGS = 5;
constexpr std::uint64_t WARMUP_INTERVAL_US = 100'000;
constexpr std::uint64_t TICK_US = 10'000;  // шаг игрового цикла

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: reliable_client <experiment_id> [count] [interval_ms] [max_attempts]\n";
        return 1;
    }
    const std::string experimentId = argv[1];
    const int count = argc > 2 ? std::stoi(argv[2]) : 50;
    const int intervalMs = argc > 3 ? std::stoi(argv[3]) : 200;
    const auto maxAttempts = static_cast<std::uint32_t>(argc > 4 ? std::stoi(argv[4]) : 5);

    UdpSocket socket;
    if (!socket.IsOpen()) {
        std::cerr << "Failed to create UDP socket\n";
        return 1;
    }
    auto server = MakeEndpoint(SERVER_IP, SERVER_PORT);
    if (!server) {
        std::cerr << "Invalid server address\n";
        return 1;
    }

    std::cout << "Series " << experimentId << ": " << count << " orders every "
              << intervalMs << " ms, max_attempts=" << maxAttempts << "\n";

    ReliableChannel channel(maxAttempts);
    AdaptiveTimeout timeout;
    OrderBook orders;

    std::vector<DeliveryRecord> records;                   // в порядке отправки
    std::unordered_map<std::uint16_t, std::size_t> open;   // sequence -> индекс в records
    std::size_t retransmissions = 0;
    std::size_t staleAcks = 0;  // повторные ACK и ACK после failed

    // Фиксирует итог приказа: доставлен или окончательно не доставлен.
    auto finish = [&](std::uint16_t sequence, DeliveryStatus status,
                      std::uint32_t attempts, std::uint64_t nowUs) {
        auto it = open.find(sequence);
        if (it == open.end()) {
            return;
        }
        DeliveryRecord& record = records[it->second];
        record.status = status;
        record.attempts = attempts;
        record.finishedUs = nowUs;
        record.rtoMs = timeout.RtoMs();
        record.srttMs = timeout.SrttMs();
        open.erase(it);
    };

    auto handleDatagram = [&](const std::uint8_t* data, std::size_t size,
                              const Endpoint& from, std::uint64_t nowUs) {
        if (!(from == *server)) {
            std::cerr << "Ignored datagram from unexpected sender " << ToString(from) << "\n";
            return;
        }
        auto parsed = parsePacket(data, size);
        if (auto* error = std::get_if<ParseError>(&parsed)) {
            std::cerr << "Dropped invalid datagram (size=" << size
                      << ", reason=" << toString(*error) << ")\n";
            return;
        }
        const Packet& packet = std::get<Packet>(parsed);

        switch (packet.header.type) {
            case CommandType::ACK: {
                auto ack = parseAck(packet);
                if (auto* error = std::get_if<ParseError>(&ack)) {
                    std::cerr << "Dropped invalid ACK (reason=" << toString(*error) << ")\n";
                    return;
                }
                const std::uint16_t acked = std::get<Ack>(ack).acknowledgedSequence;
                const PendingPacket* pending = channel.Find(acked);
                if (pending == nullptr) {
                    ++staleAcks;
                    return;
                }
                const std::uint32_t attempts = pending->attempts;
                // Правило Карна: у переотправленного пакета неизвестно, на
                // какую попытку пришёл ACK, поэтому RTT берётся только у
                // пакетов, подтверждённых с первой попытки.
                if (attempts == 1) {
                    timeout.OnSample((nowUs - pending->firstSentAtUs) / 1000.0);
                }
                channel.OnAckReceived(acked);
                finish(acked, DeliveryStatus::Delivered, attempts, nowUs);
                orders.OnDelivered(acked);
                return;
            }
            case CommandType::PONG: {
                auto pong = parsePong(packet);
                if (auto* p = std::get_if<Pong>(&pong); p && nowUs >= p->clientSendTimeUs) {
                    timeout.OnSample((nowUs - p->clientSendTimeUs) / 1000.0);
                }
                return;
            }
            case CommandType::UNIT_MOVE:
            case CommandType::UNIT_ATTACK:
                // Игровой результат приказа. Доставку подтверждает ACK, а этот
                // ответ ненадёжен и может потеряться.
                std::cout << "[SERVER] " << payloadText(packet) << "\n";
                return;
            default:
                std::cerr << "Dropped unexpected packet [type="
                          << static_cast<int>(packet.header.type) << "]\n";
                return;
        }
    };

    // Шаг игрового цикла: повторная отправка просроченных пакетов и
    // уведомление игрового слоя об окончательных недоставках.
    auto tick = [&](std::uint64_t nowUs) {
        for (PendingPacket* packet : channel.CollectForRetransmission(nowUs, timeout.RtoUs())) {
            socket.SendTo(*server, packet->rawBytes);
            ++retransmissions;
        }
        for (std::uint16_t sequence : channel.TakeNewFailures()) {
            finish(sequence, DeliveryStatus::Failed, maxAttempts, nowUs);
            orders.OnFailed(sequence);
        }
    };

    std::uint8_t buffer[MAX_DATAGRAM_SIZE];
    // Принимает датаграммы до момента untilUs, вызывая tick не реже раза в TICK_US.
    auto runUntil = [&](std::uint64_t untilUs) {
        while (true) {
            const std::uint64_t nowUs = NowUs();
            if (nowUs >= untilUs) {
                break;
            }
            const std::uint64_t waitUs = (std::min)(untilUs - nowUs, TICK_US);

            Endpoint from;
            auto size = socket.ReceiveFrom(
                buffer, sizeof(buffer), from, std::chrono::milliseconds((waitUs + 999) / 1000));
            const std::uint64_t receivedAtUs = NowUs();
            if (size) {
                handleDatagram(buffer, *size, from, receivedAtUs);
            }
            tick(NowUs());
        }
    };

    std::uint16_t sequence = 1;

    // Разогрев: PING ненадёжны, но дают RTO стартовую оценку вместо 1 с.
    for (int i = 0; i < WARMUP_PINGS; ++i) {
        socket.SendTo(*server, serializePing(sequence++, Ping{NowUs()}));
        runUntil(NowUs() + WARMUP_INTERVAL_US);
    }

    const std::uint64_t startUs = NowUs();
    const std::uint64_t intervalUs = static_cast<std::uint64_t>(intervalMs) * 1000;

    for (int i = 0; i < count; ++i) {
        runUntil(startUs + i * intervalUs);

        // Чётные приказы — перемещение разведчика по клеткам карты, нечётные —
        // атака по юниту противника.
        std::vector<std::uint8_t> bytes;
        std::string command;
        if (i % 2 == 0) {
            const UnitMove move{1, (i / 2) % 10, (i / 2) / 10};
            bytes = serializeUnitMove(sequence, move);
            orders.OnMoveSent(sequence, move);
            command = "UNIT_MOVE";
        } else {
            const UnitAttack attack{1, 2};
            bytes = serializeUnitAttack(sequence, attack);
            orders.OnAttackSent(sequence, attack);
            command = "UNIT_ATTACK";
        }

        const std::uint64_t sentUs = NowUs();
        if (!socket.SendTo(*server, bytes)) {
            // Пакет всё равно регистрируется: его отправит повтор.
            std::cerr << "Failed to send " << command << " [sequence=" << sequence << "]\n";
        }
        channel.OnSent(sequence, bytes, sentUs);

        DeliveryRecord record;
        record.experimentId = experimentId;
        record.sequence = sequence;
        record.command = command;
        record.firstSentUs = sentUs;
        records.push_back(record);
        open[sequence] = records.size() - 1;
        ++sequence;
    }

    // Ждём, пока каждый приказ подтвердится или исчерпает попытки.
    const std::uint64_t deadlineUs = NowUs() +
        maxAttempts * static_cast<std::uint64_t>(AdaptiveTimeout::kMaxRtoMs) * 1000 + 1'000'000;
    while (channel.PendingCount() > 0 && NowUs() < deadlineUs) {
        runUntil(NowUs() + TICK_US);
    }

    if (!AppendDeliveryCsv(CSV_PATH, records)) {
        std::cerr << "Failed to write " << CSV_PATH << "\n";
    }

    std::size_t delivered = 0, firstTry = 0, failed = 0;
    double attemptsSum = 0, ackTimeSum = 0;
    for (const DeliveryRecord& record : records) {
        attemptsSum += record.attempts;
        if (record.status == DeliveryStatus::Delivered) {
            ++delivered;
            if (record.attempts == 1) {
                ++firstTry;
            }
            ackTimeSum += (record.finishedUs - record.firstSentUs) / 1000.0;
        } else if (record.status == DeliveryStatus::Failed) {
            ++failed;
        }
    }
    std::cout << "sent=" << records.size() << " delivered=" << delivered
              << " first_try=" << firstTry << " failed=" << failed
              << " retransmissions=" << retransmissions
              << " stale_acks=" << staleAcks << "\n"
              << "avg_attempts=" << (records.empty() ? 0.0 : attemptsSum / records.size())
              << " avg_time_to_ack=" << (delivered ? ackTimeSum / delivered : 0.0) << " ms"
              << " final_rto=" << timeout.RtoMs() << " ms\n";
    return 0;
}
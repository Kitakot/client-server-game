#include "protocol/protocol.hpp"
#include "telemetry/csv_log.hpp"
#include "telemetry/telemetry.hpp"
#include "transport/clock.hpp"
#include "transport/udp_socket.hpp"

#include <chrono>
#include <iostream>
#include <string>

// UDP-клиент измерения задержки. Отправляет серию PING с постоянным
// интервалом, по ответам PONG считает RTT и дописывает результаты серии в
// docs/latency_samples.csv. Потерянные PING повторно не отправляются.
//
// Запуск: client <experiment_id> [count] [interval_ms]

constexpr std::uint16_t SERVER_PORT = 54000;
constexpr const char* SERVER_IP = "127.0.0.1";
constexpr std::uint64_t TIMEOUT_US = 1'000'000;
constexpr const char* CSV_PATH = "docs/latency_samples.csv";

// Разбирает одну датаграмму и, если это корректный PONG от сервера,
// передаёт его в телеметрию. Всё остальное отбрасывается с записью в лог.
void handleDatagram(
    const uint8_t* data,
    std::size_t size,
    const Endpoint& from,
    const Endpoint& server,
    std::uint64_t receivedAtUs,
    Telemetry& telemetry) {
    if (!(from == server)) {
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

    auto pong = parsePong(packet);
    if (auto* error = std::get_if<ParseError>(&pong)) {
        std::cerr << "Dropped non-PONG datagram [sequence=" << packet.header.sequence
                  << ", reason=" << toString(*error) << "]\n";
        return;
    }

    // RTT считается только по часам клиента; серверные метки из PONG —
    // диагностика.
    PongResult result = telemetry.OnPong(packet.header.sequence, receivedAtUs);
    if (result != PongResult::Received) {
        std::cerr << toString(result) << " [sequence=" << packet.header.sequence << "]\n";
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: client <experiment_id> [count] [interval_ms]\n";
        return 1;
    }
    const std::string experimentId = argv[1];
    const int count = argc > 2 ? std::stoi(argv[2]) : 50;
    const int intervalMs = argc > 3 ? std::stoi(argv[3]) : 200;

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

    std::cout << "Series " << experimentId << ": " << count
              << " PING every " << intervalMs << " ms to " << ToString(*server) << "\n";

    Telemetry telemetry(experimentId, TIMEOUT_US);
    uint8_t buffer[MAX_DATAGRAM_SIZE];

    // Принимает ответы до момента untilUs и отмечает просроченные измерения.
    auto receiveUntil = [&](std::uint64_t untilUs) {
        while (true) {
            const std::uint64_t nowUs = NowUs();
            if (nowUs >= untilUs) {
                break;
            }
            const auto wait = std::chrono::milliseconds((untilUs - nowUs + 999) / 1000);

            Endpoint from;
            auto size = socket.ReceiveFrom(buffer, sizeof(buffer), from, wait);
            const std::uint64_t receivedAtUs = NowUs();
            if (size) {
                handleDatagram(buffer, *size, from, *server, receivedAtUs, telemetry);
            }
            telemetry.Expire(NowUs());
        }
    };

    const std::uint64_t startUs = NowUs();
    const std::uint64_t intervalUs = static_cast<std::uint64_t>(intervalMs) * 1000;
    uint16_t sequence = 1;

    for (int i = 0; i < count; ++i) {
        // Момент отправки считается от начала серии, чтобы интервал не
        // накапливал погрешность.
        receiveUntil(startUs + i * intervalUs);

        const std::uint64_t sentUs = NowUs();
        if (socket.SendTo(*server, serializePing(sequence, Ping{sentUs}))) {
            telemetry.OnPingSent(sequence, sentUs);
        } else {
            std::cerr << "Failed to send PING [sequence=" << sequence << "]\n";
        }
        ++sequence;
    }

    // Ждём ответы на последние PING: тайм-аут и небольшой запас.
    receiveUntil(NowUs() + TIMEOUT_US + 200'000);
    telemetry.Expire(NowUs());

    if (!AppendCsvFile(CSV_PATH, telemetry.Samples())) {
        std::cerr << "Failed to write " << CSV_PATH << "\n";
    }

    const Summary s = telemetry.Summarize();
    std::cout << "sent=" << s.sent << " received=" << s.received
              << " timeout=" << s.timeouts << " late=" << s.late
              << " duplicate=" << s.duplicate << " unknown=" << s.unknown << "\n"
              << "rtt min/mean/median/max = " << s.minMs << " / " << s.meanMs
              << " / " << s.medianMs << " / " << s.maxMs << " ms\n"
              << "srtt=" << s.srttMs << " ms  jitter=" << s.jitterMs
              << " ms  loss=" << s.lossPercent << " %\n";
    return 0;
}
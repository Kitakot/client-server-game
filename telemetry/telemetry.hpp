#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

enum class SampleStatus { Pending, Received, Timeout };
enum class PongResult { Received, Late, Duplicate, Unknown };

const char* toString(SampleStatus status);
const char* toString(PongResult result);

// Одно измерение: один отправленный PING и то, что с ним стало.
struct Sample {
    std::string experimentId;      // идентификатор серии
    std::size_t index = 0;         // номер измерения в серии, с 1
    std::uint16_t sequence = 0;
    std::uint64_t sentUs = 0;      // время отправки по часам клиента
    int attempts = 1;              // в ПР №2 всегда 1: повторов нет
    SampleStatus status = SampleStatus::Pending;
    bool answered = false;         // PONG уже приходил (вовремя или поздно)
    std::optional<double> rttMs;   // заполнены только у Received
    std::optional<double> srttMs;
};

struct Summary {
    std::size_t sent = 0;
    std::size_t received = 0;
    std::size_t timeouts = 0;
    std::size_t late = 0;
    std::size_t duplicate = 0;
    std::size_t unknown = 0;
    double minMs = 0;
    double maxMs = 0;
    double meanMs = 0;
    double medianMs = 0;
    double srttMs = 0;
    double jitterMs = 0;
    double lossPercent = 0;
};

// Статистика по набору RTT. Вынесена в функции, чтобы тестировать отдельно.
double Mean(const std::vector<double>& values);
double Median(std::vector<double> values);
// J = 1/(n-1) * sum |RTT_i - RTT_{i-1}|
double MeanJitter(const std::vector<double>& values);

// Учёт измерений одной серии. Не работает с сетью и часами: текущее время
// передаётся параметром nowUs, поэтому класс тестируется без сокетов.
class Telemetry {
public:
    explicit Telemetry(
        std::string experimentId,
        std::uint64_t timeoutUs = 1'000'000,
        std::size_t maxInFlight = 256);

    void OnPingSent(std::uint16_t sequence, std::uint64_t nowUs);
    PongResult OnPong(std::uint16_t sequence, std::uint64_t nowUs);
    // Переводит в Timeout измерения без ответа дольше timeoutUs.
    // Возвращает, сколько их стало.
    std::size_t Expire(std::uint64_t nowUs);

    const std::vector<Sample>& Samples() const { return samples_; }
    Summary Summarize() const;

private:
    std::string experimentId_;
    std::uint64_t timeoutUs_;
    std::size_t maxInFlight_;

    std::vector<Sample> samples_;  // журнал серии в порядке отправки
    // inFlight: sequence -> индекс в samples_. Ограничен maxInFlight_,
    // самые старые номера вытесняются (order_ хранит порядок добавления).
    std::unordered_map<std::uint16_t, std::size_t> inFlight_;
    std::deque<std::uint16_t> order_;

    double srttMs_ = 0;
    bool hasSrtt_ = false;
    std::size_t late_ = 0;
    std::size_t duplicate_ = 0;
    std::size_t unknown_ = 0;
};
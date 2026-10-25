#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// Учёт неподтверждённых надёжных пакетов на стороне отправителя. С сетью и
// часами не работает: время передаётся параметром nowUs, а пакеты для
// повторной отправки возвращаются вызывающему коду. Поэтому класс
// тестируется без сокетов.

struct PendingPacket {
    std::vector<std::uint8_t> rawBytes;  // сериализованный пакет для повторной отправки
    std::uint64_t firstSentAtUs = 0;     // первая отправка: от неё считается время до ACK
    std::uint64_t lastSentAtUs = 0;
    std::uint32_t attempts = 1;
};

class ReliableChannel {
public:
    explicit ReliableChannel(std::uint32_t maxAttempts = 5) : maxAttempts_(maxAttempts) {}

    // Регистрирует отправленный надёжный пакет.
    void OnSent(std::uint16_t sequence, std::vector<std::uint8_t> rawBytes, std::uint64_t nowUs);

    // Обрабатывает входящий ACK. Возвращает true, если pending-запись найдена
    // и снята. Повторный ACK и ACK на пакет, уже ушедший в failed_, дают false.
    bool OnAckReceived(std::uint16_t sequence);

    // Неподтверждённый пакет или nullptr. Нужен, чтобы до OnAckReceived узнать
    // число попыток и время первой отправки.
    const PendingPacket* Find(std::uint16_t sequence) const;

    // Пакеты, которые нужно отправить повторно прямо сейчас (adaptiveTimeoutUs
    // истёк), с увеличенными attempts. Пакеты, исчерпавшие maxAttempts_,
    // переносятся в failed_ и не возвращаются. Указатели действительны до
    // следующего OnSent.
    std::vector<PendingPacket*> CollectForRetransmission(
        std::uint64_t nowUs, std::uint64_t adaptiveTimeoutUs);

    // Номера, ушедшие в failed_ после предыдущего вызова: для игрового слоя.
    std::vector<std::uint16_t> TakeNewFailures();

    std::size_t PendingCount() const;
    std::size_t FailedCount() const;

private:
    std::uint32_t maxAttempts_;
    std::unordered_map<std::uint16_t, PendingPacket> pending_;
    std::vector<std::uint16_t> failed_;
    std::size_t reportedFailures_ = 0;
};
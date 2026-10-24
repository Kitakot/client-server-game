#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_set>

// Окно недавно обработанных sequenceNumber одного клиента. Нужно серверу,
// чтобы пакет, повторно присланный из-за потерянного ACK, не применился
// дважды: UNIT_ATTACK не должен нанести урон второй раз.
//
// Хранит последние capacity номеров, самые старые вытесняются. Переход номера
// через 65535 → 0 окно не ломает: сравнения «больше/меньше» нет, только
// проверка наличия. Окно должно покрывать все пакеты, отправленные за время
// жизни одного повтора: maxAttempts * maxRTO = 5 * 3 с = 15 с; при 5 приказах
// в секунду это 75 номеров, меньше 256.
class DedupWindow {
public:
    explicit DedupWindow(std::size_t capacity = 256) : capacity_(capacity) {}

    // true — номер новый и запомнен; false — уже обрабатывался.
    bool MarkProcessed(std::uint16_t sequence);

private:
    std::size_t capacity_;
    std::unordered_set<std::uint16_t> seen_;
    std::deque<std::uint16_t> order_;
};
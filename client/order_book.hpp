#pragma once

#include "protocol/protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <unordered_map>

// Игровой слой клиента: что игрок видит на карте, пока приказ в пути.
// Перемещение показывается сразу (оптимистично), а клетка, подтверждённая
// доставкой приказа, хранится отдельно. Если ReliableChannel исчерпал
// попытки, юнит возвращается на последнюю подтверждённую клетку, и игрок
// может отдать приказ заново в этом же ходу.
//
// Ограничение: «не доставлен» значит «клиент не знает». Сервер мог применить
// приказ, если потерялись все ACK; истину покажет следующая синхронизация
// состояния с сервера.
struct UnitView {
    std::int32_t x = 0, y = 0;                    // где юнит нарисован
    std::int32_t confirmedX = 0, confirmedY = 0;  // по последнему доставленному приказу
    std::uint16_t lastMoveSequence = 0;           // последний отданный приказ перемещения
};

class OrderBook {
public:
    void OnMoveSent(std::uint16_t sequence, const UnitMove& move) {
        UnitView& unit = units_[move.unitId];
        unit.x = move.targetX;
        unit.y = move.targetY;
        unit.lastMoveSequence = sequence;
        orders_[sequence] = Order{CommandType::UNIT_MOVE, move, UnitAttack{}};
    }

    void OnAttackSent(std::uint16_t sequence, const UnitAttack& attack) {
        orders_[sequence] = Order{CommandType::UNIT_ATTACK, UnitMove{}, attack};
    }

    void OnDelivered(std::uint16_t sequence) {
        auto it = orders_.find(sequence);
        if (it == orders_.end()) {
            return;
        }
        if (it->second.type == CommandType::UNIT_MOVE) {
            UnitView& unit = units_[it->second.move.unitId];
            unit.confirmedX = it->second.move.targetX;
            unit.confirmedY = it->second.move.targetY;
        }
        orders_.erase(it);
    }

    void OnFailed(std::uint16_t sequence) {
        auto it = orders_.find(sequence);
        if (it == orders_.end()) {
            return;
        }
        const Order& order = it->second;
        if (order.type == CommandType::UNIT_MOVE) {
            UnitView& unit = units_[order.move.unitId];
            std::cout << "[GAME] Move order for unit " << order.move.unitId
                      << " to (" << order.move.targetX << ", " << order.move.targetY
                      << ") not confirmed by server";
            // Откат, только если после этого приказа юниту не отдавали новых.
            if (unit.lastMoveSequence == sequence) {
                unit.x = unit.confirmedX;
                unit.y = unit.confirmedY;
                std::cout << ": unit returned to (" << unit.x << ", " << unit.y << ")";
            }
            std::cout << "\n";
        } else {
            std::cout << "[GAME] Attack of unit " << order.attack.attackerId
                      << " on unit " << order.attack.targetId
                      << " not confirmed by server and cancelled\n";
        }
        orders_.erase(it);
    }

    const UnitView& Unit(std::int32_t unitId) { return units_[unitId]; }
    std::size_t PendingOrders() const { return orders_.size(); }

private:
    struct Order {
        CommandType type;
        UnitMove move;
        UnitAttack attack;
    };
    std::unordered_map<std::int32_t, UnitView> units_;
    std::unordered_map<std::uint16_t, Order> orders_;
};